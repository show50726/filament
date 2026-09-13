#!/usr/bin/env python3
# Copyright (C) 2026 The Android Open Source Project
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

"""Check the ABI mapping of OIT statuses and JNI / Embind entry points."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def members(text, name):
    body = re.search(name + r'[^\{]*\{([^}]+)\}', text).group(1)
    return re.findall(r'\b[A-Z][A-Z_]+\b', body)


class BindingTests(unittest.TestCase):
    def test_status_ordinals(self):
        cpp = members(read('filament/include/filament/View.h'), 'enum class OitStatus')
        java = members(read('android/filament-android/src/main/java/com/google/android/filament/View.java'),
                       'enum OitStatus')
        ts = members(read('web/filament-js/filament.d.ts'), r'enum View\$OitStatus')
        js = re.findall(r'\.value\("([A-Z_]+)", View::OitStatus::([A-Z_]+)\)',
                        read('web/filament-js/jsenums.cpp'))
        self.assertEqual(cpp, java)
        self.assertEqual(cpp, ts)
        self.assertEqual([(name, name) for name in cpp], js)

    def test_entry_points(self):
        jni = read('android/filament-android/src/main/cpp/View.cpp')
        java = read('android/filament-android/src/main/java/com/google/android/filament/View.java')
        js = read('web/filament-js/jsbindings.cpp')
        for method in ('setOitEnabled', 'isOitEnabled', 'getOitStatus'):
            native = 'n' + method[0].upper() + method[1:]
            self.assertEqual(jni.count('Java_com_google_android_filament_View_' + native), 1)
            self.assertRegex(java, r'private static native \w+ ' + native + r'\(')
            self.assertEqual(js.count(f'.function("{method}", &View::{method})'), 1)


if __name__ == '__main__':
    unittest.main()
