#!/usr/bin/env python3
# Copyright (C) 2026 The Android Open Source Project
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

"""FP16 storage reference tests; GPU shader regression is tested separately."""
import itertools
import math
import struct
import unittest


def half(value):
    return struct.unpack('e', struct.pack('e', value))[0]


def accumulate(layers):
    total = [0.0] * 3
    denominator, transmittance = 0.0, 1.0
    for rgb, alpha, depth in layers:
        weight = (0.5 + 0.5 * depth) ** 3 / 16.0
        for channel in range(3):
            contribution = min(max(rgb[channel], 0), alpha * 65504) * weight / 256
            total[channel] = half(total[channel] + contribution)
        denominator = half(denominator + alpha * weight)
        transmittance = half(transmittance * (1 - alpha))
    coverage = 1 - transmittance
    if denominator == 0:
        return (0.0, 0.0, 0.0, 0.0)
    return (*[min(c * 256 / denominator, 65504) * coverage for c in total], coverage)


class NumericTests(unittest.TestCase):
    def test_empty_and_zero_coverage(self):
        self.assertEqual(accumulate([]), (0, 0, 0, 0))
        self.assertEqual(accumulate([((1, 2, 3), 0, 1)]), (0, 0, 0, 0))

    def test_single_layer_matches_premultiplied_over(self):
        for alpha in (0.01, 0.1, 0.5, 1):
            for depth in (0, 0.5, 1):
                rgb = tuple(alpha * c for c in (0.25, 1, 16))
                actual = accumulate([(rgb, alpha, depth)])
                for expected, value in zip((*rgb, alpha), actual):
                    self.assertAlmostEqual(value, expected, delta=max(0.003, expected * 0.035))

    def test_order_invariance_with_rounding(self):
        layers = [((0.25, 0, 0), 0.25, 0.1), ((0, 0.5, 0), 0.5, 0.8),
                  ((0, 0, 0.75), 0.75, 0.4)]
        reference = accumulate(layers)
        for order in itertools.permutations(layers):
            for expected, actual in zip(reference, accumulate(order)):
                self.assertAlmostEqual(expected, actual, delta=0.002)

    def test_hdr_overlap_remains_finite(self):
        for count in (1, 16, 64, 256, 1024):
            actual = accumulate([((65504, 65504, 65504), 1, 1)] * count)
            self.assertTrue(all(math.isfinite(c) for c in actual))
            self.assertEqual(actual[3], 1)


if __name__ == '__main__':
    unittest.main()
