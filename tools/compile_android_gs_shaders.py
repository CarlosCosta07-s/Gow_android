#!/usr/bin/env python3
"""Compile the small Android GS bring-up shaders into a private C++ header."""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from pathlib import Path


def compile_stage(
    compiler: Path, source: Path, output: Path, stage: str
) -> bytes:
    command = [
        str(compiler),
        "-V",
        "--target-env",
        "vulkan1.0",
        "-S",
        stage,
        "-o",
        str(output),
        str(source),
    ]
    print("$ " + " ".join(command), flush=True)
    subprocess.run(command, check=True)
    data = output.read_bytes()
    if len(data) < 20 or len(data) % 4 != 0:
        raise RuntimeError(f"Invalid SPIR-V byte count for {source.name}: {len(data)}")
    magic = struct.unpack_from("<I", data)[0]
    if magic != 0x07230203:
        raise RuntimeError(f"Invalid SPIR-V magic for {source.name}: 0x{magic:08x}")
    return data


def array_text(name: str, data: bytes) -> str:
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    rows = []
    for index in range(0, len(words), 8):
        rows.append("    " + ", ".join(f"0x{word:08X}u" for word in words[index:index + 8]))
    return (
        f"alignas(4) inline constexpr std::uint32_t {name}[] = {{\n"
        + ",\n".join(rows)
        + "\n};\n"
        + f"inline constexpr std::size_t {name}Size = sizeof({name});\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--vertex", required=True, type=Path)
    parser.add_argument("--fragment", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    compiler = args.compiler.expanduser().resolve()
    vertex = args.vertex.expanduser().resolve()
    fragment = args.fragment.expanduser().resolve()
    output = args.output.expanduser().resolve()
    for path in (compiler, vertex, fragment):
        if not path.is_file():
            raise FileNotFoundError(f"Required Vulkan shader input not found: {path}")

    output.parent.mkdir(parents=True, exist_ok=True)
    vertex_spv = output.with_name("gs_primitive.vert.spv")
    fragment_spv = output.with_name("gs_primitive.frag.spv")
    vertex_data = compile_stage(compiler, vertex, vertex_spv, "vert")
    fragment_data = compile_stage(compiler, fragment, fragment_spv, "frag")

    text = (
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n\n"
        "namespace gow::android::shaders {\n"
        + array_text("kGsPrimitiveVertexSpirv", vertex_data)
        + "\n"
        + array_text("kGsPrimitiveFragmentSpirv", fragment_data)
        + "} // namespace gow::android::shaders\n"
    )
    output.write_text(text, encoding="utf-8")
    print(
        f"Generated {output} "
        f"(vertex={len(vertex_data)} bytes, fragment={len(fragment_data)} bytes)."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"GS shader compilation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
