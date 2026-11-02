"""Download the pinned SH-2 toolchain into toolchain/ (gitignored).

GCC 14.2.0 for sh2eb-elf (big-endian SH-2, newlib, C/C++), the build
SaturnRingLib uses, from github.com/willll/Saturn-SDK-GCC-SH2. The release
zip holds Windows, Linux and macOS builds; only the one for this host is
extracted. See docs/compiler-bakeoff.md for why this version. Its macOS build
is GCC 14.1.0 for Apple silicon, which this project has never been built with.

The sound CPU's program (saturn/sound.68k) needs GNU binutils for the 68000.
On Windows they come from MSYS2's package of Retro68's binutils (GPL-3,
statically linked: the programs alone are extracted into toolchain/m68k),
unpacked with the zstandard package (requirements.txt) or a zstd program.
Elsewhere, install a 68000 binutils (Debian, Ubuntu: binutils-m68k-linux-gnu)
and, if its prefix isn't m68k-linux-gnu-, pass M68K_PREFIX to make.

usage: python tools/setup_toolchain.py
"""
import hashlib
import io
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "toolchain")
URL = "https://github.com/willll/Saturn-SDK-GCC-SH2/releases/download/gcc_14.2.0_2/sh-gcc-14.2.0.zip"
SHA256 = "e3f35f0e17b1fbd93b0de426f93cfe15b488bbb30b7426d7f373bd4982d37a68"
ZIP = os.path.join(DEST, "sh-gcc-14.2.0.zip")
OUT = os.path.join(DEST, "gcc-14.2.0")

# zip folder holding each host's build (matches SH_BIN in the Makefile)
HOST_DIRS = {"Windows": "sh2eb-elf/", "Linux": "linux/", "Darwin": "mac/"}
# the machine a host's build runs on, where no other can run it
HOST_MACHINES = {"Linux": "x86_64", "Darwin": "arm64"}

M68K_URL = ("https://mirror.msys2.org/mingw/ucrt64/"
            "mingw-w64-ucrt-x86_64-m68k-apple-macos-binutils-r808.8d32c583ae-2-any.pkg.tar.zst")
M68K_SHA256 = "9bd89afba00895e2924c8b1af48e15a80f4430477500205f31b3298f726c319e"
M68K_PKG = os.path.join(DEST, "m68k-binutils.pkg.tar.zst")
M68K_OUT = os.path.join(DEST, "m68k")     # M68K_PREFIX in the Makefile
M68K_TOOLS = ("as", "ld.bfd", "objcopy", "objdump")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url, path, digest):
    """The file at url, checked, at path; a download cut short leaves no file."""
    os.makedirs(DEST, exist_ok=True)
    if not os.path.exists(path):
        print(f"downloading {url}")
        part = path + ".part"
        try:
            urllib.request.urlretrieve(url, part)
        except OSError as e:
            sys.exit(f"could not download {url}: {e} (README.md, Troubleshooting)")
        os.replace(part, path)
    found = sha256(path)
    if found != digest:
        sys.exit(f"checksum mismatch for {path}: {found}")


def unzstd(path):
    try:
        import zstandard
    except ImportError:
        zstd = shutil.which("zstd") or "C:/msys64/usr/bin/zstd.exe"
        if not os.path.exists(zstd):
            sys.exit("68000 binutils: nothing to unpack them with: pip install -r requirements.txt (README.md)")
        return subprocess.run([zstd, "-d", "-c", path], capture_output=True, check=True).stdout
    with open(path, "rb") as f, zstandard.ZstdDecompressor().stream_reader(f) as reader:
        return reader.read()


def setup_m68k():
    if platform.system() != "Windows":
        if not shutil.which("m68k-linux-gnu-as"):
            print("68000 binutils: install them (Debian, Ubuntu: binutils-m68k-linux-gnu);"
                  " with another prefix than m68k-linux-gnu-, pass M68K_PREFIX to make")
        return
    if all(os.path.exists(os.path.join(M68K_OUT, f"m68k-apple-macos-{t}.exe")) for t in M68K_TOOLS):
        print(f"68000 binutils already present: {M68K_OUT}")
        return
    fetch(M68K_URL, M68K_PKG, M68K_SHA256)
    tar = unzstd(M68K_PKG)
    os.makedirs(M68K_OUT, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(tar)) as t:
        for tool in M68K_TOOLS:
            name = f"m68k-apple-macos-{tool}.exe"
            with t.extractfile(f"ucrt64/bin/{name}") as src, open(os.path.join(M68K_OUT, name), "wb") as dst:
                shutil.copyfileobj(src, dst)
    print(f"extracted {len(M68K_TOOLS)} 68000 tools to {M68K_OUT}")


def main():
    system = platform.system()
    host_dir = HOST_DIRS.get(system)
    machine = HOST_MACHINES.get(system)
    if not host_dir or (machine and platform.machine() != machine):
        sys.exit(f"no prebuilt toolchain for {system} on {platform.machine()}: "
                 "build one (github.com/willll/Saturn-SDK-GCC-SH2) and pass SH_BIN to make")
    setup_m68k()
    if os.path.isdir(os.path.join(OUT, host_dir)):
        print(f"toolchain already present: {os.path.join(OUT, host_dir)}")
        return

    fetch(URL, ZIP, SHA256)
    with zipfile.ZipFile(ZIP) as z:
        members = [m for m in z.infolist() if m.filename.startswith(host_dir)]
        for m in members:
            path = z.extract(m, OUT)
            # The zip was made on Windows and records no usable modes: its Linux
            # programs say execute-only for the group, its macOS ones nothing
            if not m.is_dir():
                os.chmod(path, 0o755)
    print(f"extracted {len(members)} entries to {os.path.join(OUT, host_dir)}")


if __name__ == "__main__":
    main()
