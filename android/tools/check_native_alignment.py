#!/usr/bin/env python3
"""Check every packaged ELF LOAD segment and uncompressed APK library offset.
This checks binary/package alignment, not execution on a 16 KiB device.
"""
import argparse
import struct
import zipfile
from pathlib import Path


def check(apk):
    with zipfile.ZipFile(apk) as archive, open(apk, 'rb') as source:
        libraries = [entry for entry in archive.infolist() if entry.filename.startswith('lib/') and entry.filename.endswith('.so')]
        if {entry.filename.split('/')[1] for entry in libraries} != {'arm64-v8a', 'x86_64'}:
            raise ValueError('Both arm64-v8a and x86_64 libraries are required')
        for entry in libraries:
            if entry.compress_type != zipfile.ZIP_STORED:
                raise ValueError(f'{entry.filename}: library must be uncompressed')
            source.seek(entry.header_offset)
            header = source.read(30)
            filename_length, extra_length = struct.unpack_from('<HH', header, 26)
            if (entry.header_offset + 30 + filename_length + extra_length) % 16384:
                raise ValueError(f'{entry.filename}: APK library offset is not 16 KiB aligned')
            elf = archive.read(entry)
            if elf[:6] != b'\x7fELF\x02\x01':
                raise ValueError(f'{entry.filename}: expected little-endian ELF64')
            offset = struct.unpack_from('<Q', elf, 32)[0]
            entry_size, count = struct.unpack_from('<HH', elf, 54)
            loads = 0
            for index in range(count):
                kind, _, file_offset, virtual, _, _, _, alignment = struct.unpack_from('<IIQQQQQQ', elf, offset + index * entry_size)
                if kind == 1:
                    loads += 1
                    if alignment < 16384 or file_offset % 16384 != virtual % 16384:
                        raise ValueError(f'{entry.filename}: ELF LOAD is not 16 KiB aligned')
            if not loads:
                raise ValueError(f'{entry.filename}: ELF has no LOAD segments')
            print(f'PASS: {apk.name}: {entry.filename}: ELF and APK 16 KiB alignment')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('apk', type=Path, nargs='+')
    for apk in parser.parse_args().apk:
        check(apk)
