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
import struct
import subprocess
import decimal

from flask import Flask


app = Flask(__name__)
_g_renderer: Renderer


def pairwise(it: Iterable[Any]) -> Iterator[Tuple[Any, Any]]:
    it = iter(it)
    return zip(it, it)


quant = decimal.Context(
    prec=4,
    Emin=-4,
    Emax=+4,
).create_decimal


@dataclass(eq=True, frozen=True)
class RenderingRequest:
    imageResolution: int
    volumeName: str
    colorMapName: str
    opacityMapName: str
    cameraPosition: Tuple[float, float, float]
    cameraUp: Tuple[float, float, float]
    cameraDirection: Tuple[float, float, float]
    backgroundColor: Tuple[float, float, float, float]

    def write(self, fileobj: BinaryIO):
        def write(s: str):
            s = s + '\n'
            s = s.encode('utf-8')
            fileobj.write(s)

        write(f'{self.imageResolution}')
        write(f'{self.volumeName}')
        write(f'{self.colorMapName}')
        write(f'{self.opacityMapName}')
        write(' '.join([
            f'{x}'
            for x in self.cameraPosition
        ]))
        write(' '.join([
            f'{x}'
            for x in self.cameraUp
        ]))
        write(' '.join([
            f'{x}'
            for x in self.cameraDirection
        ]))
        write(' '.join([
            f'{x}'
            for x in self.backgroundColor
        ]))

        fileobj.flush()


@dataclass(eq=True, frozen=True)
class RenderingResponse:
    imageLength: int
    imageData: bytes

    @classmethod
    def read(cls, fileobj: BinaryIO) -> Self:
        def read(format: str) -> Tuple[Any, ...]:
            size = struct.calcsize(format)
            data = fileobj.read(size)
            assert len(data) == size
            return struct.unpack(format, data)
        
        imageLength ,= read('N')
        imageData ,= read(f'{imageLength}s')

        return cls(
            imageLength=imageLength,
            imageData=imageData,
        )


Renderer = typing.NewType('Renderer', typing.Generator[RenderingResponse, RenderingRequest, None])


def make_renderer(executable: Path) -> Renderer:
    process = subprocess.Popen([
        executable,
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    response = None
    while True:
        request = yield response

        request.write(process.stdin)

        response = RenderingResponse.read(process.stdout)


@app.route('/image/<path:options>', methods=['GET'])
def image(options: str):
    options: List[str] = options.split('/')
    dataset, *options = options
    px, py, pz, *options = options
    ux, uy, uz, *options = options
    dx, dy, dz, *options = options
    resolution, *options = options

    px, py, pz = map(quant, (px, py, pz))
    ux, uy, uz = map(quant, (ux, uy, uz))
    dx, dy, dz = map(quant, (dx, dy, dz))
    resolution = int(resolution)

    options: str = '/'.join(options)
    options: List[str] = options.split(',')
    _, *options = options
    options = dict(pairwise(options))

    br, bg, bb, ba = map(int, options.get('background', '0/0/0/0').split('/'))

    response = _g_renderer.send(RenderingRequest(
        imageResolution=resolution,
        volumeName=dataset,
        colorMapName='viridis',
        opacityMapName='ramp',
        cameraPosition=(px, py, pz),
        cameraUp=(ux, uy, uz),
        cameraDirection=(dx, dy, dz),
        backgroundColor=(br, bg, bb, ba),
    ))

    return response.imageData, { 'Content-Type': 'image/jpg' }


@app.route('/')
def index():
    return Path('index.html').read_text(), { 'Content-Type': 'text/html' }


def main(rendererExecutable):
    renderer = make_renderer(rendererExecutable)
    next(renderer)

    global _g_renderer
    _g_renderer = renderer

    app.run(
        host='0.0.0.0',
        port=8081,
        debug=True,
    )


def cli(args: Optional[List[str]]=None):
    import argparse
    
    parser = argparse.ArgumentParser()
    parser.add_argument('--renderer-executable', dest='rendererExecutable', type=Path, default=Path('osprayAsAService'))
    args = vars(parser.parse_args(args))

    main(**args)


if __name__ == '__main__':
    cli()
