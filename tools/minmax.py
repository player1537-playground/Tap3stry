"""

"""

from __future__ import annotations
from pathlib import Path

import numpy as np


def main(inp: Path):
    data = np.fromfile(inp, dtype=np.float32)

    lo = np.min(data)
    hi = np.max(data)

    print(f'{lo}, {hi}')


def cli():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('--input', '-i', dest='inp', type=Path, required=True)
    args = vars(parser.parse_args())

    main(**args)


if __name__ == '__main__':
    cli()
