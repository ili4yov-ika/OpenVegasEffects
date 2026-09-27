"""Extract the named VEGAS Effects Qt resources from VegasEffects.exe.

The older byte-signature extractor intentionally de-duplicated PNG payloads,
which lost the resource name -> payload relationship.  VegasEffects.exe also
contains the original Qt RCC tree; parsing it gives an exact mapping, including
aliases where several resource names share one PNG payload.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import struct
from pathlib import Path


IMAGE_TREE_VA = 0x1412792F0
IMAGE_NAMES_VA = 0x14128B5F0
IMAGE_DATA_VA = 0x140D1D850
IMAGE_BASE = 0x140000000
TREE_ENTRY_SIZE = 22
DIRECTORY_FLAG = 0x02


def pe_va_to_offset(data: bytes, va: int) -> int:
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    sections = pe_offset + 24 + optional_size
    rva = va - IMAGE_BASE
    for index in range(section_count):
        entry = sections + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, entry + 8
        )
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_offset + rva - virtual_address
    raise ValueError(f"VA 0x{va:X} is outside the PE sections")


def resource_name(data: bytes, names: int, offset: int) -> str:
    length = struct.unpack_from(">H", data, names + offset)[0]
    begin = names + offset + 6  # uint16 length + uint32 Qt hash
    return data[begin : begin + length * 2].decode("utf-16-be")


def resource_node(data: bytes, tree: int, index: int) -> tuple[int, int, int, int]:
    begin = tree + index * TREE_ENTRY_SIZE
    name_offset, flags = struct.unpack_from(">IH", data, begin)
    first, second = struct.unpack_from(">II", data, begin + 6)
    return name_offset, flags, first, second


def collect_files(data: bytes, tree: int, names: int) -> list[tuple[str, int]]:
    files: list[tuple[str, int]] = []

    def visit(index: int, parent: tuple[str, ...]) -> None:
        name_offset, flags, first, second = resource_node(data, tree, index)
        name = "" if index == 0 else resource_name(data, names, name_offset)
        path = parent + ((name,) if name else ())
        if flags & DIRECTORY_FLAG:
            child_count, child_offset = first, second
            for child in range(child_offset, child_offset + child_count):
                visit(child, path)
        else:
            files.append(("/".join(path), second))

    visit(0, ())
    return files


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--exe", type=Path,
        default=Path("SAMPLES/VEGAS_Effects/VegasEffects.exe"),
    )
    parser.add_argument(
        "--output", type=Path,
        default=Path("SAMPLES/icons-VegesEffects/named_png"),
    )
    parser.add_argument(
        "--manifest", type=Path,
        default=Path("SAMPLES/icons-VegesEffects/named_manifest.tsv"),
    )
    args = parser.parse_args()

    image = args.exe.read_bytes()
    tree = pe_va_to_offset(image, IMAGE_TREE_VA)
    names = pe_va_to_offset(image, IMAGE_NAMES_VA)
    payloads = pe_va_to_offset(image, IMAGE_DATA_VA)
    rows = []
    for resource_path, data_offset in collect_files(image, tree, names):
        payload = payloads + data_offset
        size = struct.unpack_from(">I", image, payload)[0]
        blob = image[payload + 4 : payload + 4 + size]
        target = args.output.joinpath(*resource_path.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
        width = height = 0
        if blob.startswith(b"\x89PNG\r\n\x1a\n"):
            width, height = struct.unpack_from(">II", blob, 16)
        rows.append((resource_path, target.as_posix(), data_offset, size,
                     width, height, hashlib.sha1(blob).hexdigest()))

    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    with args.manifest.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(("resource", "file", "data_offset", "length",
                         "width", "height", "sha1"))
        writer.writerows(rows)
    print(f"Mapped {len(rows)} resources to {args.output}")


if __name__ == "__main__":
    main()
