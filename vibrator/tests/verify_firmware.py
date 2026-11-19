#!/usr/bin/env python3
"""Reject incompatible RAM firmware before trusting the M2381 effect mapping."""
import hashlib
import pathlib
import struct
import sys


def main():
    data = pathlib.Path(sys.argv[1]).read_bytes()
    expected = "63017f8303ef5872299931dea40df46700a00416af9f7e6c94abf4ba19142e37"
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("Firmware changed: audit RAM mapping and timing before deployment")
    checksum, base = struct.unpack_from(">HH", data)
    if sum(data[2:]) & 0xFFFF != checksum:
        raise ValueError("Invalid Awinic firmware checksum")
    first, = struct.unpack_from(">H", data, 5)
    count = (first - base - 1) // 4
    if count != 20:
        raise ValueError(f"Unexpected RAM wave count {count}")
    for index, expected_samples in ((8, 139), (11, 231), (13, 231), (3, 374)):
        start, end = struct.unpack_from(">HH", data, 5 + (index - 1) * 4)
        offset = 4 + start - base
        size = end - start + 1
        samples = data[offset:offset + size]
        if len(samples) != expected_samples or not any(samples) or samples[-1] != 0:
            raise ValueError(f"Wave {index} is truncated, empty, or has a different duration/tail")
        print(f"PASS wave={index} samples={size} duration={size / 24:.3f}ms at 24kHz")
    print(f"PASS checksum=0x{checksum:04x} RAM waves={count}")


if __name__ == "__main__":
    main()
