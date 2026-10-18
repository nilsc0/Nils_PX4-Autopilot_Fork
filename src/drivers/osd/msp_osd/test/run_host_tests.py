#!/usr/bin/env python3
"""Test real MSP conversion/transport code using an existing SITL build's headers.

Requires a C++ compiler and system GoogleTest development libraries.
Address/undefined sanitizers are optional; leak detection is outside this test.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    test_dir = Path(__file__).resolve().parent
    root = test_dir.parents[4]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=root / 'build/px4_sitl_default')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    entries = json.loads((args.build_dir / 'compile_commands.json').read_text())
    entry = next(item for item in entries if item['file'].endswith('/msp_osd/uorb_to_msp.cpp'))
    command = shlex.split(entry['command'])
    compiler = command.pop(0)
    if Path(compiler).name == 'ccache':
        compiler = command.pop(0)
    flags = []
    i = 0
    while i < len(command):
        if command[i] in ('-o', '-c', '-MF', '-MT', '-MQ'):
            i += 2
            continue
        if command[i] in ('-MD', '-MMD'):
            i += 1
            continue
        flags.append(command[i])
        i += 1
    flags.append('-I' + str(test_dir.parent))
    sanitizer_flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer'] if args.sanitize else []
    with tempfile.TemporaryDirectory(prefix='px4-msp-tests-') as temp_dir:
        objects = []
        sources = [Path(entry['file']), test_dir.parent / 'MspV1.cpp', test_dir / 'MspSafetyTest.cpp']
        for source in sources:
            obj = str(Path(temp_dir) / (source.stem + '.o'))
            subprocess.run([compiler, *flags, *sanitizer_flags, '-c', str(source), '-o', obj],
                           cwd=entry['directory'], check=True)
            objects.append(obj)
        executable = str(Path(temp_dir) / 'msp-safety-test')
        subprocess.run([compiler, *objects, *sanitizer_flags, '-Wl,--gc-sections',
                        '-lgtest_main', '-lgtest', '-lpthread', '-o', executable], check=True)
        env = os.environ.copy()
        if args.sanitize:
            env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
        subprocess.run([executable], check=True, env=env)


if __name__ == '__main__':
    main()
