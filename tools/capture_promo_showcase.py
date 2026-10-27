#!/usr/bin/env python3
"""Build/run only the expanded promo capture; never invokes a regression suite.
Existing outputs are refused. For another take, choose --output or manually archive
its directory beside it; this script never removes recordings or prior captures.
"""
import argparse
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', default='promo/expanded-01/capture')
    p.add_argument('--binary', default='/tmp/seance_promo_showcase_capture')
    p.add_argument('--build-only', action='store_true')
    args = p.parse_args()
    output = (ROOT / args.output).resolve()
    if output.exists() and not args.build_only:
        p.error(f'preserving existing output: {output}; choose a new --output')
    recipe = subprocess.check_output(['make', 'NEEDS_DEVKIT=', '-nB', '/tmp/seance_chorus_ui_test'], cwd=ROOT, text=True)
    commands = [shlex.split(line) for line in recipe.splitlines() if line.startswith('g++ ')]
    if len(commands) != 1:
        raise RuntimeError('expected one existing native UI harness compiler recipe')
    command = commands[0]
    command[command.index('tools/sequencer_test.cpp')] = 'tools/promo_showcase_capture.cpp'
    command[command.index('-o') + 1] = args.binary
    subprocess.run(command, cwd=ROOT, check=True)
    if not args.build_only:
        subprocess.run([args.binary, str(output)], cwd=ROOT, check=True)
        (output / 'build-command.txt').write_text(shlex.join(command) + '\n')

if __name__ == '__main__':
    main()
