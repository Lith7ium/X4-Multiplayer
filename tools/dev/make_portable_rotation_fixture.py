"""Create a private bounded C sanitizer fixture from an existing saved-text census.

The output contains private saved-world angle texts; keep it under .local or an
equivalent private evidence directory. It is not a native save or game package.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_portable_rotation_c import batch_expected, text_rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--census',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    rows,census_sha=text_rows(args.census)
    expected=batch_expected(rows)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('xb') as stream:
        stream.write(b'X4ROT1\0\0'+struct.pack('<I',len(rows)))
        for (orientation,angles),(status,words) in zip(rows,expected):
            stream.write(struct.pack('<I',orientation))
            for angle in angles:
                text=b'' if angle is None else angle.encode('ascii')
                stream.write(struct.pack('<II',int(angle is not None),len(text)))
                stream.write(text)
            stream.write(struct.pack('<I9I',status,*words))
    print(json.dumps(dict(records=len(rows),census_sha256=census_sha,
                          fixture_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest())))


if __name__=='__main__':main()
