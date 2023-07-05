"""

"""

from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
import sys
import os
import tempfile
import decimal
import functools
import typing

import pysg as sg


OSP_FLOAT = 6000


quant = typing.NewType('quant', decimal.Context(
    prec=4,
    Emin=-4,
    Emax=4,
).create_decimal)


@dataclass(eq=True, frozen=True, kw_only=True)
class VolumeConfig:
    name: str
    path: Path
    shape: Tuple[int, int, int]
    type: ClassVar[int] = OSP_FLOAT
    origin: ClassVar[Tuple[quant, quant, quant]] = (quant(-0.5), quant(-0.5), quant(-0.5))
    spacing: Tuple[quant, quant, quant]

    @functools.lru_cache
    def load(self) -> sg.Node:
        root = sg.createNode(self.name, "Node")

        importer = sg.getImporter(root, sg.FileName(str(self.path)))
        if importer is None:
            raise KeyError(f"No importer exists for path: {self.path!r}")

        # importer.setLightsManager(lights)
        # importer.setMaterialRegistry(materials)

        volumeParams = sg.VolumeParams()
        volumeParams.createChild("voxelType", "int",
            sg.Any(self.type))
        volumeParams.createChild("dimensions", "vec3i",
            sg.Any(sg.vec3i(*self.shape)))
        volumeParams.createChild("gridOrigin", "vec3f",
            sg.Any(sg.vec3f(*map(float, self.origin))))
        volumeParams.createChild("gridSpacing", "vec3f",
            sg.Any(sg.vec3f(*map(float, self.spacing))))

        importer.importScene()

        return root


@dataclass(eq=True, frozen=True, kw_only=True)
class ImageConfig:
    size: Tuple[int, int]
    format: ClassVar[str] = 'jpg'


@dataclass(eq=True, frozen=True, kw_only=True)
class CameraConfig:
    position: Tuple[quant, quant, quant]
    direction: Tuple[quant, quant, quant]


@dataclass(eq=True, frozen=True, kw_only=True)
class RenderingRequest:
    volume: VolumeConfig
    camera: CameraConfig
    frame: FrameConfig


@dataclass(frozen=True)
class RenderingResponse:
    image: bytes


def make_render_worker() -> Generator[RenderingResponse, RenderingRequest, None]:
    frame = sg.Frame()
    frame.createChild("scaleNav", "float",
        sg.Any(1.0))
    frame.createChild("camera", "camera_perspective")
    frame.createChild("renderer", "renderer_ao")

    world = frame.child("world")
    lights = frame.child("lights")
    materials = frame.child("baseMaterialRegistry")
    camera = frame.child("camera")
    renderer = frame.child("renderer")

    volume: Optional[sg.Node] = None

    prev_request = None
    response = None
    while True:
        prev_request, request = request, yield response

        if prev_request is None or request.image != prev_request.image:
            frame.createChild("windowSize", "vec2i",
                sg.Any(sg.vec2i(request.image.size, request.image.size)))
        
        if prev_request is None or request.camera != prev_request.camera:
            camera.createChild("position", "vec3f",
                sg.Any(sg.vec3f(*map(float, request.camera.position))))
            camera.createChild("direction", "vec3f",
                sg.Any(sg.vec3f(*map(float, request.camera.direction))))
        
        if prev_request is None or request.volume != prev_request.volume:
            world.remove(prev_request.volume.name)
            volume = request.volume.load()
            world.add(volume)

        # Time to start rendering.
        #
        frame.startNewFrame()

        # XXX(th): This is slow because OSPRay Studio doesn't release the GIL during
        # this call, so the entire process is blocked while waiting on the frame to
        # finish rendering.
        #
        frame.waitOnFrame()

        # XXX(th): This is slow because it has to write to disk just to get the
        # image data into memory. OSPRay Studio doesn't expose the
        # `ospray::sg::Exporter::doExport` method. With that, we could directly
        # create an `exporter_jpg` node, and set its `child("file")` to some
        # `/dev/fd` path.
        #
        # Another option would be to map the framebuffer directly, but
        # `ospray::sg::FrameBuffer::map` also isn't exported. With that, it would be
        # possible to use Pillow to convert the image ourselves.
        #
        with tempfile.NamedTemporaryFile(suffix=f'.{request.image.format}') as f:
            frame.saveFrame(f.name, 0)
            image = f.read()
        
        response = RenderingResponse(
            image=request.frame.render(frame),
        )


def make_http_worker():
    pass


def main():
    renderer = make_render_worker()
    next(renderer)

    renderer.send(RenderingRequest(
        frame=FrameConfig(
        ),
        camera=CameraConfig(
        ),
        volume=VolumeConfig(
            name="myvolume",
            path=Path.cwd() / 'supernova.raw',
            shape=(432, 432, 432),
            type=OSP_FLOAT,
            origin=(-0.5, -0.5, -0.5),
            spacing=(1./432, 1./432, 1./432),
        ),
    ))
        
    volume = _make_volume(
        name="myvolume",
        # path=Path("/mnt/seenas2/data/standalone/data/teapot.raw"),
        # shape=(256, 256, 178),
        path=Path.cwd() / 'supernova.raw',
        shape=(432, 432, 432),
        type=OSP_FLOAT,
        origin=(-0.5, -0.5, -0.5),
        spacing=(1./432, 1./432, 1./432),
        lights=lights,
        materials=materials,
    )
    world.add(volume)




def cli(args: Optional[List[str]]):
    import argparse
    
    parser = argparse.ArgumentParser()
    args = vars(parser.parse_args(args))

    main(**args)


if __name__ == '__main__':
    cli(sg.init(sys.argv))
