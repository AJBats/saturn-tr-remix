"""The player's disc builder, made for a release: the zip for GitHub's Releases.

    python -I builder/release.py "Engineering Sample 1" [--saturn CUE] [--ps1 CUE]

Run from the root of the tree the release is of (a release branch's worktree), in Git Bash (make needs sh).
--saturn and --ps1 are the reference rips (the repository's external_resources/ by default). Into
build/release/:

  1. make disc in build/release: our 0MAIN.BIN, and the disc make builds, to check the builder against;
  2. stage/: the builder (builder.py, discbuild.py, art.py), the tools the packer runs, the headers and tables
     they read, payload/ (0MAIN.BIN, the disc's text files) and discs.json: the reference rips' file hashes
     (discbuild.py has the layout);
  3. the staged builder run without a window on the reference rips, with and without the PlayStation disc:
     with it, its data track must be make's; both data tracks' hashes go in discs.json as the results every
     player's build is checked against;
  4. PyInstaller: dist/Tomb Raider Saturn Builder/, the program in a folder (a one-file program unpacks
     itself on every start, which antivirus programs like less), in a virtual environment with the pinned
     packages (builder/requirements.txt);
  5. the program run without a window on the reference rips, both ways: each must pass its check;
  6. the zip: that folder with README.txt, the release's name filled in.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

TREE = os.getcwd()
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(TREE, "build", "release")
STAGE = os.path.join(OUT, "stage")
VENV = os.path.join(TREE, "build", "builder_venv")
PY = os.path.join(VENV, "Scripts", "python.exe")
NAME = "Tomb Raider Saturn Builder"
ER = os.path.join(TREE, "external_resources")
TOOLS = ("pack_level texart diet trsat trpsx sfx meshstore facepack roomrank planepack mkdisc").split()
DATA = ["openlara/common.h", "saturn/vram.h", "saturn/sound.h", "saturn/planes.txt"]


def run(cmd, **kw):
    print("$", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=TREE, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit(f"failed ({r.returncode}):\n{r.stdout[-4000:]}\n{r.stderr[-4000:]}")
    return r.stdout + r.stderr


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)


def sha1_file(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def stage(release, saturn, ps1):
    if os.path.exists(STAGE):
        shutil.rmtree(STAGE)       # release.py's own output from an earlier run
    for f in ("builder.py", "discbuild.py", "art.py"):
        copy(os.path.join(HERE, f), os.path.join(STAGE, f))
    for t in TOOLS:
        copy(os.path.join(TREE, "tools", t + ".py"), os.path.join(STAGE, "tools", t + ".py"))
    painted = os.path.join(TREE, "saturn", "plane_paint")     # tools/floor_paint.py's, where there are any
    paint = [f"saturn/plane_paint/{f}" for f in sorted(os.listdir(painted))] if os.path.isdir(painted) else []
    for f in DATA + paint:
        copy(os.path.join(TREE, f), os.path.join(STAGE, f))
    copy(os.path.join(OUT, "0MAIN.BIN"), os.path.join(STAGE, "payload", "0MAIN.BIN"))
    for t in ("ABS.TXT", "BIB.TXT", "CPY.TXT"):
        copy(os.path.join(TREE, "disc", t), os.path.join(STAGE, "payload", t))
    sys.path.insert(0, STAGE)
    import discbuild
    rip = discbuild.Rip(saturn)
    files = discbuild.iso_files(rip.data_track())
    psx = discbuild.iso_files(discbuild.Rip(ps1).data_track())
    commit = run(["git", "rev-parse", "--short", "HEAD"]).strip()
    known = dict(
        release=release, commit=commit,
        saturn=dict(name="Tomb Raider (USA)", tracks=len(rip.tracks),
                    files={p: discbuild.sha1(d) for p, d in sorted(files.items())}),
        ps1=dict(name="Tomb Raider (USA) (Rev 1)",
                 files={f"PSXDATA/{lv}.PSX": discbuild.sha1(psx[f"PSXDATA/{lv}.PSX"])
                        for lv in discbuild.LEVELS if f"PSXDATA/{lv}.PSX" in psx}))
    write_known(known)
    print(f"staged: {len(files)} Saturn files, {len(known['ps1']['files'])} PlayStation levels, commit {commit}")
    return known


def write_known(known):
    with open(os.path.join(STAGE, "discs.json"), "w") as f:
        json.dump(known, f, indent=1)


def builder_run(exe, saturn, ps1, out, log):
    cmd = exe + ["--saturn", saturn, "--out", out, "--log", log] + (["--ps1", ps1] if ps1 else [])
    t0 = time.time()
    r = subprocess.run(cmd, cwd=OUT, capture_output=True, text=True)
    text = open(log, encoding="utf-8").read() if os.path.exists(log) else r.stdout + r.stderr
    if r.returncode or "Done:" not in text:
        sys.exit(f"the builder failed ({r.returncode}):\n{text[-4000:]}")
    print(f"  built in {time.time() - t0:.0f} s: {out}")
    return sha1_file(os.path.join(out, "tomb_track01.iso"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("release", help='its name, e.g. "Engineering Sample 1"')
    ap.add_argument("--saturn", default=os.path.join(ER, "tomb_raider_usa", "Tomb Raider (USA).cue"))
    ap.add_argument("--ps1", default=os.path.join(ER, "ps1", "Tomb Raider (USA) (Rev 1).cue"))
    args = ap.parse_args()
    if not os.path.exists(PY):
        sys.exit(f"no virtual environment at {VENV}: python -m venv build/builder_venv, then "
                 f"build/builder_venv/Scripts/python -m pip install -r builder/requirements.txt")

    out = run(["make", "BUILD=build/release", "disc", "-j8"])
    if re.search(r"\berror\b", out, re.I):
        sys.exit("make reported an error:\n" + out[-4000:])
    make_iso = sha1_file(os.path.join(OUT, "disc", "tomb_track01.iso"))

    known = stage(args.release, args.saturn, args.ps1)
    staged = [PY, "-I", os.path.join(STAGE, "builder.py")]
    with_ps1 = builder_run(staged, args.saturn, args.ps1, os.path.join(OUT, "check_ps1"),
                           os.path.join(OUT, "check_ps1.log"))
    if with_ps1 != make_iso:
        sys.exit(f"the builder's disc isn't make's: data track {with_ps1}, make's {make_iso}")
    without = builder_run(staged, args.saturn, None, os.path.join(OUT, "check_saturn"),
                          os.path.join(OUT, "check_saturn.log"))
    known["results"] = dict(with_ps1=with_ps1, without_ps1=without)
    write_known(known)
    print(f"results: with the PlayStation disc {with_ps1} (make's), without {without}")

    sys.path.insert(0, STAGE)
    import art
    art.save_icon(os.path.join(OUT, "icon.ico"))
    sep = ";"
    data = [f"{os.path.join(STAGE, d)}{sep}{d}" for d in ("tools", "openlara", "saturn", "payload")]
    data = [d for d in data if os.path.isdir(d.split(sep)[0])]
    data.append(f"{os.path.join(STAGE, 'discs.json')}{sep}.")
    cmd = [os.path.join(VENV, "Scripts", "pyinstaller.exe"), "--noconfirm", "--clean", "--onedir", "--windowed",
           "--name", NAME, "--icon", os.path.join(OUT, "icon.ico"), "--paths", STAGE,
           "--distpath", os.path.join(OUT, "dist"), "--workpath", os.path.join(OUT, "pyinstaller"),
           "--specpath", OUT]
    for d in data:
        cmd += ["--add-data", d]
    run(cmd + [os.path.join(STAGE, "builder.py")])

    exe = [os.path.join(OUT, "dist", NAME, NAME + ".exe")]
    for ps1, tag in ((args.ps1, "ps1"), (None, "saturn")):
        got = builder_run(exe, args.saturn, ps1, os.path.join(OUT, f"frozen_{tag}"), os.path.join(OUT, f"frozen_{tag}.log"))
        print(f"  program, {tag}: {got}")

    readme = open(os.path.join(HERE, "README.txt"), encoding="utf-8").read().replace("{release}", args.release)
    readme = readme.replace("\r\n", "\n").replace("\n", "\r\n")
    slug = re.sub(r"[^A-Za-z0-9]+", "-", args.release).strip("-")
    zpath = os.path.join(OUT, f"TombRaiderSaturn-{slug}-windows.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        root = os.path.join(OUT, "dist", NAME)
        for folder, _, names in os.walk(root):
            for n in sorted(names):
                p = os.path.join(folder, n)
                z.write(p, os.path.join(NAME, os.path.relpath(p, root)))
        z.writestr(f"{NAME}/README.txt", readme)
    print(f"{zpath}: {os.path.getsize(zpath) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
