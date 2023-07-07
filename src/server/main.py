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
import math
import threading
import pkgutil
import time

from flask import Flask


app = Flask(__name__)
_g_renderer: Renderer
_g_renderer_lock: threading.Lock = threading.Lock()


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
    imageWidth: int
    imageHeight: int
    volumeName: str
    colorMapName: str
    opacityMapName: str
    cameraPosition: Tuple[float, float, float]
    cameraUp: Tuple[float, float, float]
    cameraDirection: Tuple[float, float, float]
    cameraRowIndex: int
    cameraRowCount: int
    cameraColIndex: int
    cameraColCount: int
    backgroundColor: Tuple[float, float, float, float]

    @property
    def cameraImageStart(self) -> Tuple[float, float]:
        return (
            (self.cameraColIndex + 0.0) / self.cameraColCount,  # left
            1.0 - (self.cameraRowIndex + 0.0) / self.cameraRowCount,  # bottom
        )
    
    @property
    def cameraImageEnd(self) -> Tuple[float, float]:
        return (
            (self.cameraColIndex + 1.0) / self.cameraColCount,  # right
            1.0 - (self.cameraRowIndex + 1.0) / self.cameraRowCount,  # top
        )

    def write(self, fileobj: BinaryIO):
        def write(s: str):
            s = s + '\n'
            s = s.encode('utf-8')
            fileobj.write(s)

        write('renderer')
        write(' '.join([
            f'{x}'
            for x in self.backgroundColor
        ]))

        write('world')
        write(f'{self.volumeName}')
        write(f'{self.colorMapName}')
        write(f'{self.opacityMapName}')

        write('camera')
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
            for x in self.cameraImageStart
        ]))
        write(' '.join([
            f'{x}'
            for x in self.cameraImageEnd
        ]))

        write('render')
        write(f'{self.imageWidth}')
        write(f'{self.imageHeight}')

        fileobj.flush()


@dataclass(eq=True, frozen=True)
class RenderingResponse:
    renderDuration: int
    encodeDuration: int
    imageLength: int
    imageData: bytes

    @classmethod
    def read(cls, fileobj: BinaryIO) -> Self:
        def read(format: str) -> Tuple[Any, ...]:
            size = struct.calcsize(format)
            data = fileobj.read(size)
            assert len(data) == size
            return struct.unpack(format, data)
        
        renderDuration ,= read('N')
        encodeDuration ,= read('N')
        imageLength ,= read('N')
        imageData ,= read(f'{imageLength}s')

        return cls(
            renderDuration=renderDuration,
            encodeDuration=encodeDuration,
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
    colormap = options.get('colormap', 'spectralReverse')
    opacitymap = options.get('opacitymap', 'reverseRamp')
    tile, ntiles = map(int, options.get('tiling', '0-1').split('-'))

    nrows = int(math.sqrt(ntiles))
    row = tile // nrows

    ncols = ntiles // nrows
    col = tile % nrows

    row, nrows = map(int, options.get('row', f'{row}/{nrows}').split('/'))
    col, ncols = map(int, options.get('col', f'{col}/{ncols}').split('/'))

    beforeLock = time.time()

    with _g_renderer_lock:
        afterLock = time.time()

        beforeSend = time.time()

        response = _g_renderer.send(RenderingRequest(
            imageWidth=resolution,
            imageHeight=resolution,
            volumeName=dataset,
            colorMapName=colormap,
            opacityMapName=opacitymap,
            cameraPosition=(px, py, pz),
            cameraUp=(ux, uy, uz),
            cameraDirection=(dx, dy, dz),
            cameraRowIndex=row,
            cameraRowCount=nrows,
            cameraColIndex=col,
            cameraColCount=ncols,
            backgroundColor=(br, bg, bb, ba),
        ))

        afterSend = time.time()

    lockDuration = int((afterLock - beforeLock) * 1e6)
    sendDuration = int((afterSend - beforeSend) * 1e6)

    print(' '.join([
        f'Render: {response.renderDuration:>6d}',
        f'Encode: {response.encodeDuration:>6d}',
        f'Lock: {lockDuration:>6d}',
        f'Send: {sendDuration:>6d}',
    ]))
    return response.imageData, {
        'Content-Type': 'image/jpg',
        'Content-Length': response.imageLength,
        # 'Connection': 'keep-alive',
    }


@app.route('/')
def index():
    if __name__ == '__main__':
        html = Path(__file__).parent.joinpath('static', 'index.html').read_text()
    else:
        html = pkgutil.get_data(
            __name__,
            'static/index.html',
        )
    
    return html, { 'Content-Type': 'text/html' }


def main(engineExecutable: Path, bind: str, port: int, debug: bool):
    renderer = make_renderer(engineExecutable)
    next(renderer)

    global _g_renderer
    _g_renderer = renderer

    app.run(
        host=bind,
        port=port,
        debug=debug,
    )


def cli(args: Optional[List[str]]=None):
    import argparse
    
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--engine-executable',
        dest='engineExecutable',
        type=Path,
        default=Path('tapestryEngine'),
    )
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', default=8080, type=int)
    parser.add_argument('--debug', action='store_true')
    args = vars(parser.parse_args(args))

    main(**args)


if __name__ == '__main__':
    cli()

if __name__ == 'wsgi':
    _g_renderer = make_renderer(os.environ['ENGINE_EXECUTABLE'])
    next(_g_renderer)
