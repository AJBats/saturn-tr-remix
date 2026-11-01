"""Assembles a program for the sound CPU (a 68000 running from sound RAM) into
a header the SH-2 side includes: GNU as and ld for the 68000
(tools/setup_toolchain.py), the program linked at sound RAM address 0 (its
vectors first), flattened, and written as `static const uint8_t NAME[]`
with NAME_SIZE.

usage: python tools/m68kasm.py IN.68k OUT.h NAME [M68K_PREFIX]
"""
import os
import subprocess
import sys

LINK_SCRIPT = "SECTIONS { . = 0; .text : { *(.text) *(.data) } /DISCARD/ : { *(*) } }\n"


def tool(prefix, name):
    path = prefix + name
    if os.path.exists(path + ".exe"):
        return os.path.abspath(path + ".exe")
    return path


def main():
    src, out, name = sys.argv[1:4]
    prefix = sys.argv[4] if len(sys.argv) > 4 else "toolchain/m68k/m68k-apple-macos-"
    base = os.path.splitext(out)[0]
    obj, elf, flat, script = base + ".o", base + ".elf", base + ".bin", base + ".ld"
    with open(script, "w", newline="\n") as f:
        f.write(LINK_SCRIPT)
    subprocess.run([tool(prefix, "as"), "-m68000", "-I", os.path.dirname(src) or ".", src, "-o", obj], check=True)
    subprocess.run([tool(prefix, "ld.bfd"), "-T", script, "-o", elf, obj], check=True)
    subprocess.run([tool(prefix, "objcopy"), "-O", "binary", elf, flat], check=True)
    data = open(flat, "rb").read()
    with open(out, "w", newline="\n") as f:
        f.write(f"// {os.path.basename(src)}, assembled by tools/m68kasm.py: load at sound RAM 0\n")
        f.write(f"#define {name.upper()}_SIZE {len(data)}\n")
        f.write(f"static const uint8_t {name}[] __attribute__((aligned(4))) = {{\n")
        for i in range(0, len(data), 16):
            f.write("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n")
        f.write("};\n")


if __name__ == "__main__":
    main()
