"""The disc build the player's builder runs (builder.py): their retail discs read and checked, the levels
packed by the game's own tools, the disc written. The same tools and steps as `make disc`, so the same disc,
byte for byte, and the builder checks it came out so (discs.json "results").

The release (release.py) lays out beside this file, as in the repository: tools/ (the packer and the tools it
imports, as files: they find saturn/ and openlara/ from their own place), the headers and tables they read
(openlara/common.h, saturn/vram.h, saturn/sound.h, saturn/planes.txt, saturn/plane_paint/), payload/ (our
0MAIN.BIN and the disc's text files) and discs.json, which release.py writes from the reference rips:

    saturn.files    SHA-1 of every file on the Saturn disc's data track, and its IP.BIN
    saturn.tracks   the disc's track count
    ps1.files       SHA-1 of each PlayStation level file the packer reads (PSXDATA/*.PSX)
    results         SHA-1 of the data track built with and without the PlayStation disc

A rip is a .cue with one file a track beside it, or a .zip holding those. A data track may be raw (MODE1/2352,
MODE2/2352) or user data only (MODE1/2048).

    python discbuild.py --saturn RIP [--ps1 RIP] --out DIR      (builder.py takes the same, without a window)
"""
import concurrent.futures
import contextlib
import hashlib
import io
import json
import multiprocessing
import os
import re
import runpy
import shutil
import sys
import traceback
import zipfile

# What the tools import: they ship as files, which PyInstaller's analysis doesn't read, so their modules are
# named here for it to bundle.
import argparse  # noqa: F401
import bisect  # noqa: F401
import copy  # noqa: F401
import math  # noqa: F401
import random  # noqa: F401
import struct  # noqa: F401
import numpy as np
import scipy.ndimage  # noqa: F401
import PIL.Image  # noqa: F401

HERE = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(HERE, "tools")
PAYLOAD = os.path.join(HERE, "payload")

LEVELS = ("TITLE GYM LEVEL1 LEVEL2 LEVEL3A LEVEL3B LEVEL4 LEVEL5 LEVEL6 LEVEL7A LEVEL7B "
          "LEVEL8A LEVEL8B LEVEL8C LEVEL10A LEVEL10B LEVEL10C CUT1 CUT2 CUT3 CUT4").split()
LEVEL_FILES = ("PKD", "TEX", "SUB", "SFX", "ITX", "MSH")
# the Makefile's PICTURES and VIDEOS: taken from the retail disc as they are
PICTURES = "USATIT USAOPT GYM224 AZTEC224 GREEK224 EGYPT224 ATLAN224 EIDOS224 CRED224 ENDPG224".split()
VIDEOS = "CORELOGO CAFE MANSION SNOW LIFT VISION CANYON PYRAMID PRISON END".split()
# the test builds' switches the tools read from the environment: none of them in a player's disc
SWITCHES = ("LARA_START", "ITEMS_AT", "START_TRIGGER", "TRIGGER_AT", "SHIMMERS", "PLANES", "PLANE_LIGHT",
            "OBJECT_ART", "DIET", "VINE_MERGE")
SECTOR = 2048
USER_AT = {"MODE1/2352": (2352, 16), "MODE2/2352": (2352, 24), "MODE1/2048": (2048, 0)}


class Failed(Exception):
    """A reason the build can't go on, in words for the player."""


def manifest():
    with open(os.path.join(HERE, "discs.json")) as f:
        return json.load(f)


def sha1(data):
    return hashlib.sha1(data).hexdigest()


# --- a rip ------------------------------------------------------------------

class Rip:
    """A disc rip: its cue sheet and a file a track, in a folder or in a zip."""

    def __init__(self, path):
        self.path = path
        self.zip = None
        if path.lower().endswith(".zip"):
            try:
                self.zip = zipfile.ZipFile(path)
            except (OSError, zipfile.BadZipFile) as e:
                raise Failed(f"{os.path.basename(path)} can't be opened as a zip ({e}).")
            cues = [n for n in self.zip.namelist() if n.lower().endswith(".cue")]
            if len(cues) != 1:
                raise Failed(f"{os.path.basename(path)} should hold one .cue file, and holds {len(cues)}.")
            self.cue_name = cues[0]
            self.dir = os.path.dirname(cues[0])
            text = self.zip.read(cues[0]).decode("latin-1")
        else:
            if path.lower().endswith(".bin"):
                path = self._cue_beside(path)
            if not path.lower().endswith(".cue"):
                raise Failed("Choose the rip's .cue file (or a .zip holding the rip).")
            self.cue_name = path
            self.dir = os.path.dirname(os.path.abspath(path))
            with open(path, encoding="latin-1") as f:
                text = f.read()
        self.tracks = self._parse(text)
        for t in self.tracks:
            if not self.exists(t["file"]):
                near = [n for n in self._names() if n.lower() == t["file"].lower()]
                raise Failed(f"The cue sheet names {t['file']}, which isn't beside it."
                             + (f" There is a {near[0]}: the names must match, capitals too." if near else ""))

    @staticmethod
    def _cue_beside(path):
        folder = os.path.dirname(os.path.abspath(path))
        name = os.path.basename(path)
        for n in sorted(os.listdir(folder)):
            if n.lower().endswith(".cue"):
                with open(os.path.join(folder, n), encoding="latin-1") as f:
                    if name in f.read():
                        return os.path.join(folder, n)
        raise Failed(f"No .cue file beside {name} names it: choose the rip's .cue file.")

    @staticmethod
    def _parse(text):
        tracks = []
        file = None
        for line in text.splitlines():
            words = line.strip().split()
            if not words:
                continue
            if words[0].upper() == "FILE":
                m = re.match(r'\s*FILE\s+"([^"]+)"', line) or re.match(r'\s*FILE\s+(\S+)', line)
                file = m.group(1)
                if tracks and tracks[-1]["file"] == file:
                    raise Failed("The cue sheet names one file twice.")
            elif words[0].upper() == "TRACK":
                if file is None or (tracks and tracks[-1]["file"] == file):
                    raise Failed("This rip keeps several tracks in one file: the builder takes a rip with one "
                                 "file a track (what redumper, DiscImageCreator and most rippers write).")
                tracks.append(dict(number=int(words[1]), mode=words[2].upper(), file=file))
        if not tracks:
            raise Failed("The cue sheet lists no tracks.")
        return tracks

    def _member(self, name):
        return f"{self.dir}/{name}" if self.dir else name

    def _names(self):
        if self.zip:
            return [n[len(self.dir) + 1:] if self.dir else n for n in self.zip.namelist()
                    if os.path.dirname(n) == self.dir]
        return os.listdir(self.dir)

    def exists(self, name):
        if self.zip:
            try:
                self.zip.getinfo(self._member(name))
                return True
            except KeyError:
                return False
        return os.path.exists(os.path.join(self.dir, name))

    def open(self, name):
        if self.zip:
            return self.zip.open(self._member(name))
        return open(os.path.join(self.dir, name), "rb")

    def data_track(self):
        """The first track's user data: 2048 bytes a sector."""
        t = self.tracks[0]
        if t["mode"] not in USER_AT:
            raise Failed(f"The first track is {t['mode']}, not a data track the builder reads.")
        size, at = USER_AT[t["mode"]]
        out = bytearray()
        with self.open(t["file"]) as f:
            while True:
                chunk = f.read(size * 4096)
                if not chunk:
                    break
                n = len(chunk) // size
                out += np.frombuffer(chunk, np.uint8, n * size).reshape(n, size)[:, at:at + SECTOR].tobytes()
        return bytes(out)

    def cue_in(self, folder):
        """The rip's cue sheet where its tracks can be linked from: its own, or for a zip, its audio tracks
        written to the folder beside a copy of it."""
        if not self.zip:
            return self.cue_name
        os.makedirs(folder, exist_ok=True)
        for t in self.tracks[1:]:
            with self.zip.open(self._member(t["file"])) as src, open(os.path.join(folder, t["file"]), "wb") as dst:
                shutil.copyfileobj(src, dst, 1 << 20)
        cue = os.path.join(folder, os.path.basename(self.cue_name))
        with open(cue, "wb") as f:
            f.write(self.zip.read(self.cue_name))
        return cue


def iso_files(data):
    """An ISO9660 image's files, {path: memoryview into data}, with its first 16 sectors as IP.BIN."""
    data = memoryview(data)
    pvd = data[16 * SECTOR:17 * SECTOR]
    if bytes(pvd[1:6]) != b"CD001":
        raise Failed("The data track has no ISO9660 file system.")
    files = {"IP.BIN": data[:16 * SECTOR]}

    def walk(lba, size, prefix):
        d = data[lba * SECTOR:lba * SECTOR + size]
        pos = 0
        while pos < len(d):
            n = d[pos]
            if n == 0:
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            rec = d[pos:pos + n]
            pos += n
            name = bytes(rec[33:33 + rec[32]])
            if name in (b"\x00", b"\x01"):
                continue
            at, length = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
            path = prefix + name.decode("ascii").split(";")[0]
            if rec[25] & 2:
                walk(at, length, path + "/")
            else:
                files[path] = data[at * SECTOR:at * SECTOR + length]

    root = pvd[156:190]
    walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")
    return files


# --- checking a disc --------------------------------------------------------

def check_saturn(path):
    """The Saturn rip opened and its data track's files checked: (rip, files)."""
    rip = Rip(path)
    known = manifest()["saturn"]
    if len(rip.tracks) != known["tracks"]:
        raise Failed(f"This rip has {len(rip.tracks)} tracks; {known['name']} for the Saturn has {known['tracks']}.")
    files = iso_files(rip.data_track())
    wrong = [p for p, h in known["files"].items() if p not in files or sha1(files[p]) != h]
    if wrong:
        raise Failed(f"This isn't {known['name']} for the Saturn, or the rip is damaged: {len(wrong)} of its "
                     f"{len(known['files'])} files differ ({', '.join(wrong[:3])}).")
    return rip, files


def check_ps1(path):
    """The PlayStation rip's level files checked: {name: bytes}."""
    rip = Rip(path)
    known = manifest()["ps1"]
    files = iso_files(rip.data_track())
    wrong = [p for p, h in known["files"].items() if p not in files or sha1(files[p]) != h]
    if wrong:
        raise Failed(f"This isn't {known['name']} for the PlayStation, or the rip is damaged: {len(wrong)} of "
                     f"its {len(known['files'])} level files differ ({', '.join(wrong[:3])}).")
    return {p: bytes(files[p]) for p in known["files"]}


# --- the build --------------------------------------------------------------

def pack_level(level, data_dir, out_dir, psx_dir):
    """One level packed by tools/pack_level.py, in a worker process: its report."""
    for k in SWITCHES:
        os.environ.pop(k, None)
    sys.argv = [os.path.join(TOOLS, "pack_level.py"), data_dir, level, os.path.join(out_dir, level + ".PKD")]
    if psx_dir:
        sys.argv.append(psx_dir)
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            runpy.run_path(sys.argv[0], run_name="__main__")
    except BaseException:       # noqa: BLE001 (SystemExit too: the packer's sys.exit)
        raise RuntimeError(f"{level}:\n{out.getvalue()}\n{traceback.format_exc()}")
    return out.getvalue()


def write_files(files, folder):
    for path, data in files.items():
        dst = os.path.join(folder, *path.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(data)


def build(saturn, ps1, out, say=print, step=lambda done, total: None, detail=None, workers=None):
    """The disc built into the folder out: tomb.cue and its tracks. say(text) reports, step(done, total)
    counts the work, detail(text) takes the tools' own reports (say's if not given). Returns the cue's path."""
    detail = detail or say
    known = manifest()
    out = os.path.abspath(out)
    work = os.path.join(out, "building")
    if os.path.exists(work):
        shutil.rmtree(work)
    os.makedirs(work)
    total = 3 + len(LEVELS) + (1 if ps1 else 0)
    done = 0

    def advance(text=None):
        nonlocal done
        done += 1
        if text:
            say(text)
        step(done, total)

    try:
        say("Reading the Saturn disc")
        rip, files = check_saturn(saturn)
        write_files(files, os.path.join(work, "disc"))
        del files
        advance(f"Saturn disc: {known['saturn']['name']}, every file as it should be")

        psx_dir = None
        if ps1:
            say("Reading the PlayStation disc")
            write_files(check_ps1(ps1), os.path.join(work, "ps1"))
            psx_dir = os.path.join(work, "ps1", "PSXDATA")
            advance(f"PlayStation disc: {known['ps1']['name']}, its sound effects taken")

        data_dir = os.path.join(work, "disc", "DATA")
        levels = os.path.join(work, "levels")
        os.makedirs(levels)
        say(f"Packing the {len(LEVELS)} levels: textures, meshes, rooms and sounds")
        n = workers or max(1, min(os.cpu_count() or 1, 6))
        # spawned, as Windows always does: forking the window's process, threads and all, isn't safe on Linux
        with concurrent.futures.ProcessPoolExecutor(n, mp_context=multiprocessing.get_context("spawn")) as pool:
            jobs = {pool.submit(pack_level, lv, data_dir, levels, psx_dir): lv for lv in LEVELS}
            for i, job in enumerate(concurrent.futures.as_completed(jobs), 1):
                detail(job.result().strip())
                advance(f"  {jobs[job]} packed ({i} of {len(LEVELS)})")

        say("Writing the disc")
        entries = ["0MAIN.BIN=" + os.path.join(PAYLOAD, "0MAIN.BIN")]
        entries += [f"{t}=" + os.path.join(PAYLOAD, t) for t in ("ABS.TXT", "BIB.TXT", "CPY.TXT")]
        entries += [f"DATA/{lv}.{e}=" + os.path.join(levels, f"{lv}.{e}") for lv in LEVELS for e in LEVEL_FILES]
        with open(os.path.join(HERE, "saturn", "planes.txt")) as f:   # the Makefile's PLANE_LEVELS
            planes = re.findall(r"^LEVEL[0-9A-Z]*", f.read(), re.M)
        entries += [f"DATA/{lv}.{e}=" + os.path.join(levels, f"{lv}.{e}") for lv in planes for e in ("PLC", "PLN")]
        entries += [f"BINDATA/{p}.BIN=" + os.path.join(work, "disc", "BINDATA", p + ".BIN") for p in PICTURES]
        entries += [f"FMV/{v}.CPK=" + os.path.join(work, "disc", "FMV", v + ".CPK") for v in VIDEOS]
        cue = rip.cue_in(os.path.join(work, "audio"))
        mkdisc = os.path.join(TOOLS, "mkdisc.py")
        sys.argv = [mkdisc, "--ip", os.path.join(work, "disc", "IP.BIN"), "--cue", cue, "--out", out,
                    "--name", "tomb"] + entries
        report = io.StringIO()
        with contextlib.redirect_stdout(report):
            runpy.run_path(mkdisc, run_name="__main__")
        detail(report.getvalue().strip())
        advance()

        with open(os.path.join(out, "tomb_track01.iso"), "rb") as f:
            got = sha1(f.read())
        want = known.get("results", {}).get("with_ps1" if ps1 else "without_ps1")    # release.py's first runs: none
        if want and got != want:
            raise Failed(f"The disc came out different from the one this release was tested with "
                         f"(data track SHA-1 {got}, not {want}). Please report it, with builder.log.")
        advance("Checked: the disc is the one this release was tested with" if want else f"Data track SHA-1 {got}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return os.path.join(out, "tomb.cue")


def main():
    ap = argparse.ArgumentParser(description="Builds the disc from the player's own Tomb Raider discs.")
    ap.add_argument("--saturn", required=True, help="the Saturn disc's rip: its .cue, or a .zip of it")
    ap.add_argument("--ps1", help="the PlayStation disc's rip (optional): its sound effects")
    ap.add_argument("--out", required=True, help="the folder the disc goes to")
    ap.add_argument("--log", help="a file for the report as well")
    args = ap.parse_args(sys.argv[2:] if sys.argv[1:2] == ["--cli"] else sys.argv[1:])
    log = open(args.log, "w", encoding="utf-8") if args.log else None

    def say(text):
        print(text, flush=True)
        if log:
            log.write(text + "\n")
            log.flush()

    try:
        cue = build(args.saturn, args.ps1, args.out, say)
        say(f"Done: {cue}")
        return 0
    except Failed as e:
        say(f"Stopped: {e}")
    except Exception:       # noqa: BLE001
        say("Stopped by an error:\n" + traceback.format_exc())
    return 1


if __name__ == "__main__":
    sys.exit(main())
