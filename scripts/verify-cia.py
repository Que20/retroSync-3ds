#!/usr/bin/env python3
"""Check this project's unencrypted single-content CIA and executable hashes.

Structural/hash validation only. CFW homebrew signatures are not Nintendo signatures.
"""
import hashlib
from pathlib import Path
import struct
import sys

TITLE_ID = 0x000400000F7A6300


def verify(path):
    data = Path(path).read_bytes()

    def require(condition, message):
        if not condition:
            raise ValueError(message)

    def align(value):
        return (value + 63) & ~63

    require(len(data) >= 0x2020, "CIA too short")
    header, kind, version, certs, ticket, tmd_size, meta, content_size = struct.unpack_from("<IHHIIIIQ", data)
    require(header == 0x2020 and kind == 0 and version == 0, "Unexpected CIA header")
    tmd_offset = align(align(align(header) + certs) + ticket)
    content_offset = align(tmd_offset + tmd_size)
    require(content_offset + content_size <= len(data), "Truncated CIA content")
    require(align(content_offset + content_size) + meta == len(data), "CIA size mismatch")
    # makerom's RSA2048-SHA256 TMD signature block is 0x140 bytes.
    require(struct.unpack_from(">I", data, tmd_offset)[0] == 0x10004, "Unexpected TMD signature type")
    body = tmd_offset + 0x140
    require(struct.unpack_from(">Q", data, body + 0x4C)[0] == TITLE_ID, "Wrong TMD title ID")
    title_version, count = struct.unpack_from(">HH", data, body + 0x9C)
    require(title_version == 16 and count == 1, "Expected version 0.1.0 and one content")
    record = body + 0x9C4
    _, index, flags, record_size = struct.unpack_from(">IHHQ", data, record)
    require(index == 0 and not flags & 1 and record_size == content_size, "Unexpected encrypted/multiple content")
    content = data[content_offset:content_offset + content_size]
    require(hashlib.sha256(content).digest() == data[record + 16:record + 48], "CIA content hash mismatch")
    require(content[0x100:0x104] == b"NCCH", "Executable NCCH missing")
    require(struct.unpack_from("<I", content, 0x104)[0] * 512 == len(content), "NCCH size mismatch")
    require(struct.unpack_from("<Q", content, 0x118)[0] == TITLE_ID, "Wrong executable program ID")
    require(content[0x18F] & 4, "Expected unencrypted homebrew NCCH")
    require(hashlib.sha256(content[0x200:0x600]).digest() == content[0x160:0x180], "Extended header hash mismatch")
    exefs_start, exefs_size, exefs_hash_size = struct.unpack_from("<III", content, 0x1A0)
    romfs_start, romfs_size, romfs_hash_size = struct.unpack_from("<III", content, 0x1B0)
    for name, start, size, hash_size, expected in [
        ("ExeFS", exefs_start, exefs_size, exefs_hash_size, content[0x1C0:0x1E0]),
        ("RomFS", romfs_start, romfs_size, romfs_hash_size, content[0x1E0:0x200]),
    ]:
        require(start >= 4 and size > 0 and (start + size) * 512 <= len(content), f"Invalid {name} bounds")
        require(0 < hash_size <= size, f"Invalid {name} hash region")
        require(hashlib.sha256(content[start * 512:(start + hash_size) * 512]).digest() == expected, f"{name} hash mismatch")
    exefs = content[exefs_start * 512:(exefs_start + exefs_size) * 512]
    names = set()
    for i in range(10):
        name, offset, size = struct.unpack_from("<8sII", exefs, i * 16)
        name = name.rstrip(b"\0")
        if not name:
            continue
        names.add(name)
        require(0x200 + offset + size <= len(exefs), "ExeFS file out of bounds")
        expected = exefs[0xC0 + (9 - i) * 32:0xE0 + (9 - i) * 32]
        require(hashlib.sha256(exefs[0x200 + offset:0x200 + offset + size]).digest() == expected, f"ExeFS {name!r} hash mismatch")
    require({b".code", b"icon", b"banner"} <= names, "Missing code/icon/banner")
    require(b"-----BEGIN CERTIFICATE-----" in content[romfs_start * 512:(romfs_start + romfs_size) * 512], "CA roots missing from RomFS")
    print(f"CIA verified: title {TITLE_ID:016X}, version 0.1.0, executable/icon/banner/RomFS, content hashes")


if __name__ == "__main__":
    try:
        verify(sys.argv[1])
    except (IndexError, ValueError, struct.error) as error:
        raise SystemExit(f"CIA verification failed: {error}")
