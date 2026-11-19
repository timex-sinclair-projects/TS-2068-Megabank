#!/usr/bin/env python3
"""Builds mbmem.tap: the BASIC front end (mbmem.bas, zmakebas) followed by the
test's code (mbmem.asm, sjasmplus) as a CODE block at 32256.

    python3 firmware/controller/memtest/build.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")


def block(flag, body):
    data = bytes([flag]) + body
    x = 0
    for b in data:
        x ^= b
    data += bytes([x])
    return len(data).to_bytes(2, "little") + data


def code_tap(name, start, code):
    hdr = bytes([3]) + name.ljust(10)[:10].encode() + len(code).to_bytes(2, "little") \
        + start.to_bytes(2, "little") + (32768).to_bytes(2, "little")
    return block(0, hdr) + block(0xFF, code)


def main():
    os.makedirs(BUILD, exist_ok=True)
    subprocess.run(["sjasmplus", "--nologo", "mbmem.asm"], cwd=HERE, check=True)
    bas = os.path.join(BUILD, "mbmem-bas.tap")
    subprocess.run(["zmakebas", "-a", "10", "-n", "mbmem", "-o", bas, os.path.join(HERE, "mbmem.bas")], check=True)
    with open(os.path.join(BUILD, "mbmem.bin"), "rb") as f:
        code = f.read()
    with open(bas, "rb") as f:
        tap = f.read() + code_tap("mbmemc", 0x7E00, code)
    out = os.path.join(HERE, "mbmem.tap")
    with open(out, "wb") as f:
        f.write(tap)
    print("%s: %d bytes (code %d bytes at 32256)" % (out, len(tap), len(code)))


if __name__ == "__main__":
    sys.exit(main())
