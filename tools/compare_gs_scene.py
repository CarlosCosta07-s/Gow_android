#!/usr/bin/env python3
"""Compare two RGB PPM (P6) captures from the PS2 GS scene-validation path."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path


def _read_token(data: bytes, offset: int) -> tuple[bytes, int]:
    size = len(data)
    while offset < size:
        value = data[offset]
        if value in b" \t\r\n\v\f":
            offset += 1
            continue
        if value == ord("#"):
            newline = data.find(b"\n", offset)
            if newline < 0:
                raise ValueError("Unexpected end of file inside PPM comment.")
            offset = newline + 1
            continue
        break

    start = offset
    while offset < size and data[offset] not in b" \t\r\n\v\f#":
        offset += 1
    if start == offset:
        raise ValueError("Missing PPM header token.")
    return data[start:offset], offset


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, offset = _read_token(data, 0)
    width_token, offset = _read_token(data, offset)
    height_token, offset = _read_token(data, offset)
    maxval_token, offset = _read_token(data, offset)

    if magic != b"P6":
        raise ValueError(f"{path}: expected binary PPM P6, got {magic!r}.")
    try:
        width = int(width_token)
        height = int(height_token)
        maxval = int(maxval_token)
    except ValueError as error:
        raise ValueError(f"{path}: invalid numeric PPM header.") from error
    if width <= 0 or height <= 0:
        raise ValueError(f"{path}: image dimensions must be positive.")
    if maxval <= 0 or maxval > 255:
        raise ValueError(f"{path}: only 8-bit PPM samples are supported.")

    # Consume the single whitespace delimiter after maxval (CRLF counts as one).
    if offset >= len(data) or data[offset] not in b" \t\r\n\v\f":
        raise ValueError(f"{path}: missing PPM pixel-data delimiter.")
    if data[offset:offset + 2] == b"\r\n":
        offset += 2
    else:
        offset += 1

    expected = width * height * 3
    pixels = data[offset:]
    if len(pixels) != expected:
        raise ValueError(
            f"{path}: expected {expected} RGB bytes, found {len(pixels)}."
        )
    if maxval != 255:
        pixels = bytes((sample * 255 + maxval // 2) // maxval for sample in pixels)
    return width, height, pixels


def compare_rgb(
    reference: bytes,
    actual: bytes,
    tolerance: int,
) -> dict[str, float | int | None]:
    if len(reference) != len(actual) or len(reference) % 3 != 0:
        raise ValueError("Image buffers must have equal RGB byte lengths.")
    if not 0 <= tolerance <= 255:
        raise ValueError("Tolerance must be between 0 and 255.")

    pixel_count = len(reference) // 3
    absolute_sum = 0
    squared_sum = 0
    max_error = 0
    matching_pixels = 0

    for offset in range(0, len(reference), 3):
        pixel_max = 0
        for channel in range(3):
            difference = abs(reference[offset + channel] - actual[offset + channel])
            absolute_sum += difference
            squared_sum += difference * difference
            max_error = max(max_error, difference)
            pixel_max = max(pixel_max, difference)
        if pixel_max <= tolerance:
            matching_pixels += 1

    sample_count = len(reference)
    mae = absolute_sum / sample_count if sample_count else 0.0
    mse = squared_sum / sample_count if sample_count else 0.0
    rmse = math.sqrt(mse)
    psnr = None if mse == 0.0 else 20.0 * math.log10(255.0 / rmse)
    return {
        "pixels": pixel_count,
        "mae": mae,
        "rmse": rmse,
        "psnr_db": psnr,
        "max_channel_error": max_error,
        "matching_pixels_percent": (
            100.0 * matching_pixels / pixel_count if pixel_count else 100.0
        ),
    }


def write_diff_ppm(path: Path, reference: bytes, actual: bytes,
                   width: int, height: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    diff = bytearray(len(reference))
    for offset in range(0, len(reference), 3):
        error = max(
            abs(reference[offset + channel] - actual[offset + channel])
            for channel in range(3)
        )
        intensity = min(255, error * 4)
        diff[offset] = intensity
        diff[offset + 1] = 0
        diff[offset + 2] = 0
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + diff)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare two same-size PPM P6 GS scene captures."
    )
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--actual", required=True, type=Path)
    parser.add_argument("--tolerance", type=int, default=8,
                        help="maximum per-channel error for a matching pixel (default: 8)")
    parser.add_argument("--min-matching-pixels", type=float, default=95.0,
                        help="minimum percentage of pixels within tolerance (default: 95)")
    parser.add_argument("--max-mae", type=float, default=12.0,
                        help="maximum mean absolute channel error (default: 12)")
    parser.add_argument("--diff", type=Path,
                        help="optional PPM heatmap output path")
    args = parser.parse_args()

    try:
        ref_width, ref_height, reference = read_ppm(args.reference)
        got_width, got_height, actual = read_ppm(args.actual)
        if (ref_width, ref_height) != (got_width, got_height):
            raise ValueError(
                "Capture dimensions differ: "
                f"reference={ref_width}x{ref_height}, actual={got_width}x{got_height}."
            )
        if not 0.0 <= args.min_matching_pixels <= 100.0:
            raise ValueError("--min-matching-pixels must be between 0 and 100.")
        if not math.isfinite(args.max_mae) or args.max_mae < 0.0:
            raise ValueError("--max-mae must be a finite non-negative number.")

        metrics = compare_rgb(reference, actual, args.tolerance)
        passed = (
            metrics["matching_pixels_percent"] >= args.min_matching_pixels
            and metrics["mae"] <= args.max_mae
        )
        report = {
            "result": "pass" if passed else "fail",
            "reference": str(args.reference),
            "actual": str(args.actual),
            "dimensions": {"width": ref_width, "height": ref_height},
            "tolerance": args.tolerance,
            "thresholds": {
                "min_matching_pixels_percent": args.min_matching_pixels,
                "max_mae": args.max_mae,
            },
            "metrics": metrics,
        }
        if args.diff:
            write_diff_ppm(args.diff, reference, actual, ref_width, ref_height)
            report["diff"] = str(args.diff)
        print(json.dumps(report, indent=2, allow_nan=False))
        return 0 if passed else 2
    except (OSError, ValueError) as error:
        print(f"Scene comparison failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
