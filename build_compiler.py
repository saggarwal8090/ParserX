#!/usr/bin/env python3
"""
build_compiler.py — Helper to compile tinylang_compiler on Windows/Linux/macOS.
Run this from the project root directory:
    python build_compiler.py
"""

import subprocess
import sys
import os
import glob

def find_gcc():
    """Try to locate gcc in common places."""
    candidates = ['gcc']  # First try PATH

    if sys.platform == 'win32':
        # Common MinGW/MSYS2 locations
        extra = [
            r'C:\mingw64\bin\gcc.exe',
            r'C:\mingw32\bin\gcc.exe',
            r'C:\msys64\mingw64\bin\gcc.exe',
            r'C:\msys64\mingw32\bin\gcc.exe',
            r'C:\msys64\usr\bin\gcc.exe',
            r'C:\Program Files\mingw-w64\x86_64-8.1.0-posix-seh-rt_v6-rev0\mingw64\bin\gcc.exe',
        ]
        # Also search via glob
        for pat in [r'C:\mingw*\bin\gcc.exe', r'C:\msys*\mingw*\bin\gcc.exe']:
            extra.extend(glob.glob(pat))
        candidates.extend(extra)

    for gcc in candidates:
        try:
            r = subprocess.run([gcc, '--version'], capture_output=True, text=True, timeout=5)
            if r.returncode == 0:
                return gcc
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    return None


def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(base_dir)

    gcc = find_gcc()
    if not gcc:
        print("ERROR: gcc not found.")
        print()
        print("Please install MinGW-w64 and add its bin folder to your PATH:")
        print("  https://www.mingw-w64.org/  or  https://www.msys2.org/")
        print()
        print("After installing, open a new terminal and run:")
        print("  python build_compiler.py")
        sys.exit(1)

    print(f"Found gcc: {gcc}")

    src_files = [
        'compiler/main.c',
        'compiler/lexer.c',
        'compiler/parser.c',
        'compiler/parse_table.c',
        'compiler/ast.c',
        'compiler/symbol_table.c',
        'compiler/optimizer.c',
        'compiler/json_output.c',
    ]

    output = 'tinylang_compiler.exe' if sys.platform == 'win32' else 'tinylang_compiler'
    cmd = [gcc, '-Wall', '-Wextra', '-O2', '-o', output] + src_files

    print(f"Building {output} ...")
    print(' '.join(cmd))
    r = subprocess.run(cmd, capture_output=False, text=True)

    if r.returncode == 0:
        print()
        print(f"SUCCESS: {output} built!")
        print()
        print("Quick test:")
        print(f'  echo "int x = 3 + 4;" | ./{output}')
    else:
        print()
        print("BUILD FAILED. See errors above.")
        sys.exit(1)


if __name__ == '__main__':
    main()
