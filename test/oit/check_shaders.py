#!/usr/bin/env python3
# Copyright (C) 2026 The Android Open Source Project
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

"""Compile backend interfaces and test real material-loader version rejection."""
import argparse
import fnmatch
from pathlib import Path
import re
import struct
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--matc', required=True)
parser.add_argument('--matinfo', required=True)
parser.add_argument('--loader', required=True, help='test_oit executable')
parser.add_argument('--output', required=True)
parser.add_argument('--case-filter', action='append', default=[],
                    help='Run matching case names only (glob; may be repeated)')
parser.add_argument('--skip-spirv-inspection', action='store_true',
                    help='For matinfo built without FILAMENT_DRIVER_SUPPORTS_VULKAN')
args = parser.parse_args()
output = Path(args.output)
output.mkdir(parents=True, exist_ok=True)


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=600)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


for level in (0, 1):
    cases = [('opaque', 'unlit', False), ('transparent', 'unlit', False),
             ('fade', 'unlit', False)]
    if level:
        cases += [('transparent', 'lit', False), ('transparent', 'lit', True)]
        cases += [(mode, 'unlit', False) for mode in ('masked', 'add', 'multiply', 'screen')]
    for blending, shading, refraction in cases:
        eligible = level > 0 and blending == 'transparent' and not refraction
        name = f'{blending}-{shading}-fl{level}' + ('-refractive' if refraction else '')
        if args.case_filter and not any(fnmatch.fnmatchcase(name, pattern) for pattern in args.case_filter):
            continue
        source = output / (name + '.mat')
        package = output / (name + '.filamat')
        source.write_text(f'''material {{
            name : "{name}", featureLevel : {level}, shadingModel : {shading},
            blending : {blending}, refractionMode : {'cubemap' if refraction else 'none'}
        }}
        fragment {{ void material(inout MaterialInputs m) {{
            prepareMaterial(m); m.baseColor = vec4(0.25, 0.0, 0.0, 0.5);
        }} }}''')
        run([args.matc, '-p', 'all', '-a', 'all' if level else 'opengl',
             '-l', str(level), '-o', str(package), str(source)])
        info = run([args.matinfo, str(package)])
        assert 'Version:                   79' in info
        variants = [int(key, 16) for key in re.findall(r'\b(?:vs|fs) (0x[0-9a-fA-F]+)', info)]
        assert variants and max(variants) < 128, info
        if level and not refraction:
            entries = set(re.findall(r'\b(vs|fs) (0x[0-9a-fA-F]+)', info))
            expected = (26 if shading == 'lit' else 18)
            assert len(entries) == expected, info
        # FL0 packages also contain upgraded GLSL shaders for FL1 engines.
        # The FL0 runtime actually selects the ESSL1 chunk.
        shader = run([args.matinfo, '--print-glsl=1' if level else '--print-essl1=1', str(package)])
        output_one = re.search(r'layout\s*\(\s*location\s*=\s*1\s*\)\s*out', shader)
        assert not output_one, shader  # Ordinary transparent shaders also have only output 0.
        assert 'oitEnabled' not in shader, shader
        if eligible:
            # The ordinary fragment entry supports all three runtime specializations.
            index = re.search(r'#(\d+)\s+mobile\s+fs\s+0x00\b', info).group(1)
            shader = run([args.matinfo, '--print-glsl=' + index, str(package)])
            assert re.search(r'layout\s*\(\s*location\s*=\s*0\s*\)\s*out\s+highp\s+vec4', shader), shader
            assert not re.search(r'layout\s*\(\s*location\s*=\s*1\s*\)\s*out', shader), shader
            assert 'RUNTIME_CONFIG_OIT_ACCUMULATION' in shader, shader
            assert 'RUNTIME_CONFIG_OIT_WEIGHT' in shader, shader
        if level:
            metal = run([args.matinfo, '--print-metal=1', str(package)])
            assert not re.search(r'\[\[color\(1\)\]\]', metal)
            if eligible:
                oit_metal = run([args.matinfo, '--print-metal=' + index, str(package)])
                assert not re.search(r'\[\[color\(1\)\]\]', oit_metal), oit_metal
            if args.skip_spirv_inspection:
                print(name, 'SPIR-V inspection SKIPPED (tool capability)', flush=True)
            else:
                vk = run([args.matinfo, '--print-spirv=1', str(package)])
                assert 'Validation failure' not in vk and 'OpEntryPoint Fragment' in vk
                outputs = re.findall(r'(%\S+)\s*=\s*OpVariable\s+%\S+\s+Output', vk)
                assert not any(re.search(r'OpDecorate\s+' + re.escape(var) + r'\s+Location 1\b', vk)
                               for var in outputs)
                if eligible:
                    vk = run([args.matinfo, '--print-spirv=' + index, str(package)])
                    assert 'Validation failure' not in vk and 'OpEntryPoint Fragment' in vk
                    outputs = re.findall(r'(%\S+)\s*=\s*OpVariable\s+%\S+\s+Output', vk)
                    assert not any(re.search(r'OpDecorate\s+' + re.escape(var) + r'\s+Location 1\b', vk)
                               for var in outputs), vk
        run([args.loader, '--load-material', str(package)])
        if eligible and shading == 'unlit':
            data = bytearray(package.read_bytes())
            assert data[:8] == b'SREV_TAM'
            for version in (71, 72, 73, 77, 78):
                struct.pack_into('<I', data, 12, version)
                old = output / f'old-v{version}.filamat'
                old.write_bytes(data)
                rejected = subprocess.run([args.loader, '--load-material', str(old)],
                                          capture_output=True, text=True, timeout=60)
                message = rejected.stdout + rejected.stderr
                assert rejected.returncode != 0 and 'Material version mismatch' in message, message
        print(name, 'PASS', flush=True)
