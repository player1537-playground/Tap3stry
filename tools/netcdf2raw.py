"""

"""

from __future__ import annotations
from pathlib import Path

import numpy as np
import netCDF4 as nc


def main(inp: Path, out: Optional[Path], variable_index: int, variable_name: Optional[str]):
    dataset = nc.Dataset(inp, mode='r')

    dimensions = []
    for dimension in dataset.dimensions.values():
        dimensions.append(dimension.size)

        print(dimension)
    
    dimensions = 'x'.join(map(str, dimensions))

    for i, variable in enumerate(dataset.variables.values()):
        if variable_name is None and i == variable_index:
            variable_name = variable.name

        print(variable)
    
    variable = dataset[variable_name]

    datatype = variable.datatype

    data = variable[:]

    if hasattr(data, 'mask') and not data.mask:
        data = data.data
    
    # print(data.order)

    data = data.astype(np.float32, order='C')

    if out is None:
        out = inp.with_suffix(f'.{dimensions}.{datatype}.raw')
    
    print(f'Writing {data.nbytes} bytes to {out}')

    with open(out, 'wb') as f:
        data.tofile(f)


def cli():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('--input', '-i', dest='inp', type=Path, required=True)
    parser.add_argument('--output', '-o', dest='out', type=Path, default=None)
    variable_parser = parser.add_mutually_exclusive_group(required=False)
    variable_parser.add_argument('--variable-index', type=int, default=0)
    variable_parser.add_argument('--variable-name')
    args = vars(parser.parse_args())

    main(**args)


if __name__ == '__main__':
    cli()
