#!/usr/bin/env python3
"""Host tests for tools/compare_gs_scene.py."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from compare_gs_scene import compare_rgb, read_ppm, write_diff_ppm


class CompareGsSceneTests(unittest.TestCase):
    def test_identical_frames_have_zero_error(self) -> None:
        pixels = bytes([255, 0, 0, 0, 255, 0])
        metrics = compare_rgb(pixels, pixels, tolerance=0)
        self.assertEqual(metrics["mae"], 0.0)
        self.assertEqual(metrics["rmse"], 0.0)
        self.assertEqual(metrics["matching_pixels_percent"], 100.0)

    def test_tolerance_is_evaluated_per_pixel(self) -> None:
        reference = bytes([10, 20, 30, 100, 110, 120])
        actual = bytes([14, 20, 30, 100, 110, 121])
        metrics = compare_rgb(reference, actual, tolerance=4)
        self.assertEqual(metrics["matching_pixels_percent"], 100.0)
        self.assertGreater(metrics["mae"], 0.0)

    def test_ppm_comments_crlf_and_scaled_maxval(self) -> None:
        header = b"P6\r\n# fixture\r\n1 1\r\n15\r\n"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.ppm"
            path.write_bytes(header + bytes([15, 7, 0]))
            width, height, pixels = read_ppm(path)
        self.assertEqual((width, height), (1, 1))
        self.assertEqual(pixels, bytes([255, 119, 0]))

    def test_diff_writer_emits_rgb_ppm(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "diff.ppm"
            write_diff_ppm(path, bytes([0, 0, 0]), bytes([10, 0, 0]), 1, 1)
            width, height, pixels = read_ppm(path)
        self.assertEqual((width, height), (1, 1))
        self.assertEqual(pixels, bytes([40, 0, 0]))


if __name__ == "__main__":
    unittest.main()
