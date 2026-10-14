#!/usr/bin/env python3
"""Validate an original ESP32 application and prepare an additive SD package."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct

SLOT_SIZE = 0x1B0000


def inspect(data):
    if not 320 <= len(data) <= SLOT_SIZE:
        raise ValueError("application does not fit an app slot")
    if data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise ValueError("not an ESP application")
    if struct.unpack_from("<H", data, 12)[0] != 0 or data[3] >> 4 > 2:
        raise ValueError("image is not for this original ESP32 with 4 MB flash")
    if data[23] != 1 or struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError("use an application .bin with appended SHA-256")
    minimum = struct.unpack_from("<H", data, 15)[0] or data[14] * 100
    maximum = struct.unpack_from("<H", data, 17)[0]
    if minimum > 301 or (maximum not in (0, 65535) and maximum < 301):
        raise ValueError("image does not support the tested rev 3.1 chip")
    fields = []
    for offset in (48, 80):
        raw = data[offset:offset + 32]
        if b"\0" not in raw or not raw[0]:
            raise ValueError("invalid application description")
        text = raw.split(b"\0", 1)[0].decode("ascii")
        if any(ord(c) < 32 or ord(c) > 126 for c in text):
            raise ValueError("invalid application description")
        fields.append(text)
    version, project = fields
    if project == "cyd_launcher":
        raise ValueError("the resident launcher is updated over USB")
    offset, checksum = 24, 0xEF
    for _ in range(data[1]):
        if offset + 8 > len(data):
            raise ValueError("truncated segment")
        length = struct.unpack_from("<I", data, offset + 4)[0]
        offset += 8
        if length % 4 or offset + length > len(data):
            raise ValueError("invalid segment length")
        for value in data[offset:offset + length]:
            checksum ^= value
        offset += length
    checksum_at = offset | 15
    if checksum_at + 33 != len(data) or data[checksum_at] != checksum:
        raise ValueError("invalid image length or checksum")
    if hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise ValueError("image SHA-256 mismatch")
    return dict(project=project, version=version, size_bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest(), chip="esp32", slot_size=SLOT_SIZE)


def add_verified(path, data):
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError(f"refusing to replace an existing different file: {path.name}")
        return
    temporary = path.with_name("." + path.name + ".copy")
    with temporary.open("xb") as file:
        file.write(data)
        file.flush()
        os.fsync(file.fileno())
    if temporary.read_bytes() != data:
        raise OSError("copy verification failed")
    temporary.rename(path)
    if path.read_bytes() != data:
        raise OSError("final copy verification failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = args.image.read_bytes()
    metadata = inspect(data)
    title = metadata["project"]
    name = f'{title}-{metadata["version"]}.bin'
    if len(name) > 100 or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 ._-]*\.bin", name):
        raise ValueError("application metadata cannot form a safe library filename")
    args.output.mkdir(parents=True, exist_ok=True)
    add_verified(args.output / name, data)
    metadata["image"] = name
    add_verified(args.output / (name[:-4] + ".json"), (json.dumps(metadata, indent=2) + "\n").encode())
    print(f'PACKAGED {name} bytes={len(data)} sha256={metadata["sha256"]} verified=YES')


if __name__ == "__main__":
    main()
