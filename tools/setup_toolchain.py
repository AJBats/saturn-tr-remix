"""Download the pinned SH-2 toolchain into toolchain/ (gitignored).

GCC 14.2.0 for sh2eb-elf (big-endian SH-2, newlib, C/C++), the build
SaturnRingLib uses, from github.com/willll/Saturn-SDK-GCC-SH2. The release
zip holds Windows, Linux and macOS builds; only the one for this host is
extracted. See docs/compiler-bakeoff.md for why this version.

usage: python tools/setup_toolchain.py
"""
import hashlib
import os
import platform
import sys
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


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    host_dir = HOST_DIRS.get(platform.system())
    if not host_dir:
        sys.exit(f"no prebuilt toolchain for {platform.system()}")
    if os.path.isdir(os.path.join(OUT, host_dir)):
        print(f"toolchain already present: {os.path.join(OUT, host_dir)}")
        return

    os.makedirs(DEST, exist_ok=True)
    if not os.path.exists(ZIP):
        print(f"downloading {URL}")
        urllib.request.urlretrieve(URL, ZIP)
    digest = sha256(ZIP)
    if digest != SHA256:
        sys.exit(f"checksum mismatch for {ZIP}: {digest}")

    with zipfile.ZipFile(ZIP) as z:
        members = [m for m in z.infolist() if m.filename.startswith(host_dir)]
        for m in members:
            path = z.extract(m, OUT)
            mode = m.external_attr >> 16
            if mode and not m.is_dir():
                os.chmod(path, mode)
    print(f"extracted {len(members)} entries to {os.path.join(OUT, host_dir)}")


if __name__ == "__main__":
    main()
