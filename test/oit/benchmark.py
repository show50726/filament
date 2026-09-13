#!/usr/bin/env python3
# Copyright (C) 2026 The Android Open Source Project
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

"""Run identical offscreen workloads; preserve raw logs and explicit missing timers."""
import argparse
import csv
import itertools
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
parser.add_argument('--legacy-executable')
parser.add_argument('--output', required=True)
parser.add_argument('--width', type=int, default=1280)
parser.add_argument('--height', type=int, default=720)
parser.add_argument('--frames', type=int, default=240)
parser.add_argument('--quick', action='store_true', help='Four unlit 8-layer smoke workloads')
args = parser.parse_args()
output = Path(args.output)
output.mkdir(parents=True, exist_ok=True)
executables = [('current', args.executable)]
if args.legacy_executable:
    executables.append(('legacy', args.legacy_executable))
else:
    print('Original variant baseline: NOT MEASURED', flush=True)
with (output / 'results.csv').open('w', newline='') as table:
    writer = csv.writer(table)
    wrote_header = False
    for label, executable in executables:
        cases = itertools.product([8] if args.quick else [1, 8, 32], [25, 100], [0, 1],
                                  [0] if args.quick else [0, 1])
        for layers, coverage, oit, lit in cases:
            name = f'{label}-layers{layers}-coverage{coverage}-oit{oit}-lit{lit}'
            command = [executable, str(args.width), str(args.height), str(layers),
                       str(coverage), str(args.frames), str(oit), str(lit)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=300)
            (output / (name + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'{name} exited {result.returncode}; see raw log')
            records = [line.split(',')[1:] for line in result.stdout.splitlines()
                       if line.startswith('OIT_CSV,')]
            if len(records) != 2:
                raise RuntimeError(f'{name}: missing result, see raw log')
            if label == 'current' and oit and records[1][6] != '11':
                raise RuntimeError(f'{name}: OIT fell back (status {records[1][6]})')
            if not wrote_header:
                writer.writerow(['build', *records[0]])
                wrote_header = True
            writer.writerow([label, *records[1]])
            table.flush()
            print(name, ','.join(records[1]), flush=True)
