"""The player's disc builder, made for a release: the downloads for GitHub's Releases, Windows and Linux.

    python -I builder/release.py "Engineering Sample 1" [--saturn CUE] [--ps1 CUE] [--no-linux] [--distro NAME]

Run from the root of the tree the release is of (a release branch's worktree), in Git Bash on Windows (make needs
sh). --saturn and --ps1 are the reference rips (the repository's external_resources/ by default). Into
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
  5. the program run without a window on the reference rips, both ways: each must make the results' disc;
  6. the Windows zip: that folder with README.txt (its Windows parts, the release's name, branch and commit
     filled in), LICENSE.txt and THIRD-PARTY-NOTICES.txt;
  7. the Linux download, made in WSL (--distro) by this file again with --linux: the same stage packaged by
     PyInstaller with a standalone Python (python-build-standalone, made to run on old Linux systems; its Tk
     draws with X's core fonts), run both ways on the reference rips, the newest glibc its libraries ask
     for checked against what the read-me promises, and a .tar.gz.

The Linux side works in ~/trs_release in WSL: the downloads (cache/), the Python and its virtual environment,
and its own copy of the stage, PyInstaller's output and the test discs, which each run replaces.
"""
import argparse
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.parse
import urllib.request
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

# Linux
LNAME = "tomb-raider-saturn-builder"
LHOME = os.path.expanduser("~/trs_release")
PBS = "https://github.com/astral-sh/python-build-standalone/releases/download/20261026/"
PBS_PYTHON = ("cpython-3.12.16+20261026-x86_64-unknown-linux-gnu-install_only.tar.gz",
              "6a79e922e50006b601f29e688407659da1c2b86ebcd4e9c7254cf234be8b89de")
# the same build with its licence files, which the install-only archive leaves out (zstandard reads it)
PBS_FULL = ("cpython-3.12.16+20261026-x86_64-unknown-linux-gnu-pgo+lto-full.tar.zst",
            "789924432efcaaf05fec4bd3e4d8798733326c2ac0540eda33e84913c37a231e")
ZSTANDARD = "zstandard==0.25.0"
GLIBC_MAX = (2, 28)     # the read-me's promise
GLIBCXX_MAX = (3, 4, 25)
SYSTEM_RUNTIMES = ("libstdc++.so.6", "libgcc_s.so.1")


def run(cmd, **kw):
    print("$", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=kw.pop("cwd", TREE), capture_output=True, text=True, **kw)
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
    for f in ("builder.py", "discbuild.py", "art.py", "dialogs.py"):
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
    branch = run(["git", "rev-parse", "--abbrev-ref", "HEAD"]).strip()
    known = dict(
        release=release, commit=commit, branch=branch,
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


def readme(platform, known, archive):
    """README.txt for one platform: its [windows] or [linux] lines and the [both] ones, the names filled in."""
    keep, out = True, []
    for line in open(os.path.join(HERE, "README.txt"), encoding="utf-8").read().splitlines():
        if line in ("[windows]", "[linux]", "[both]"):
            keep = line[1:-1] in (platform, "both")
        elif keep:
            out.append(line)
    text = re.sub(r"\n{3,}", "\n\n\n", "\n".join(out)).strip() + "\n"
    for k, v in dict(release=known["release"], branch=known["branch"], commit=known["commit"],
                     archive=archive).items():
        text = text.replace("{" + k + "}", v)
    return text


def libyaul_notice():
    """libyaul's MIT notice, as saturn/cd.cpp carries it."""
    lines = open(os.path.join(TREE, "saturn", "cd.cpp"), encoding="utf-8").read().splitlines()
    first = next(i for i, l in enumerate(lines) if "Copyright (c) 2012-2023 Israel Jacquez" in l)
    last = next(i for i, l in enumerate(lines) if "DEALINGS IN THE SOFTWARE" in l)
    return "\n".join(l[2:].strip() for l in lines[first:last + 1])


def notices(site, python):
    """THIRD-PARTY-NOTICES.txt: the licences of the code the game and the builder are made with. site: the
    virtual environment's site-packages; python: (title, licence file) for Python and what its build bundles."""
    def dist(name, *rel):
        info = next(d for d in os.listdir(site) if d.lower().startswith(name + "-") and d.endswith(".dist-info"))
        return os.path.join(site, info, *rel)

    parts = [
        ("OpenLara (the game's logic), BSD-2-Clause", os.path.join(TREE, "openlara", "LICENSE")),
        ("libyaul (the CD block commands in saturn/cd.cpp, the font in saturn/font.cpp), MIT", None),
        *python,
        ("NumPy, BSD-3-Clause, with the libraries its wheel bundles", dist("numpy", "LICENSE.txt")),
        ("SciPy, BSD-3-Clause, with the libraries its wheel bundles", dist("scipy", "LICENSE.txt")),
        ("Pillow, MIT-CMU, with the libraries its wheel bundles", dist("pillow", "licenses", "LICENSE")),
        ("PyInstaller's bootloader, GPL-2.0-or-later with its bootloader exception", dist("pyinstaller", "licenses", "COPYING.txt")),
    ]
    if any(d.startswith("jeepney-") for d in os.listdir(site)):     # Linux only: how it talks to the desktop's dialog
        parts.append(("Jeepney (D-Bus, for the desktop's file dialog), MIT", dist("jeepney", "licenses", "LICENSE")))
    out = ["The engine and the builder are GPL-3.0-only (LICENSE.txt). The engine's creature AI and traps follow",
           "TR1X/TRX by LostArtefacts, GPL-3. Both are made with the following, under these licences.", ""]
    for title, path in parts:
        text = libyaul_notice() if path is None else open(path, encoding="utf-8", errors="replace").read()
        out += ["=" * 78, title, "=" * 78, "", text.strip(), "", ""]
    return "\n".join(out)


def builder_run(exe, saturn, ps1, out, log, cwd):
    cmd = exe + ["--saturn", saturn, "--out", out, "--log", log] + (["--ps1", ps1] if ps1 else [])
    t0 = time.time()
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    text = open(log, encoding="utf-8").read() if os.path.exists(log) else r.stdout + r.stderr
    if r.returncode or "Done:" not in text:
        sys.exit(f"the builder failed ({r.returncode}):\n{text[-4000:]}\n{r.stderr[-2000:]}")
    print(f"  built in {time.time() - t0:.0f} s: {out}")
    return sha1_file(os.path.join(out, "tomb_track01.iso"))


def program_runs(exe, saturn, ps1, known, where):
    """The packaged program run without a window on the reference rips, both ways: the results' discs."""
    for rip, tag, want in ((ps1, "ps1", "with_ps1"), (None, "saturn", "without_ps1")):
        got = builder_run(exe, saturn, rip, os.path.join(where, f"frozen_{tag}"),
                          os.path.join(where, f"frozen_{tag}.log"), where)
        if got != known["results"][want]:
            sys.exit(f"the program's disc ({tag}) isn't the results': {got}, not {known['results'][want]}")
        print(f"  program, {tag}: {got}, the results'")


def pyinstaller(exe, name, workdir, extra):
    sep = os.pathsep
    data = [f"{os.path.join(workdir, 'stage', d)}{sep}{d}" for d in ("tools", "openlara", "saturn", "payload")]
    data = [d for d in data if os.path.isdir(d.split(sep)[0])]
    data.append(f"{os.path.join(workdir, 'stage', 'discs.json')}{sep}.")
    cmd = [exe, "--noconfirm", "--clean", "--onedir", "--name", name, "--paths", os.path.join(workdir, "stage"),
           "--distpath", os.path.join(workdir, "dist"), "--workpath", os.path.join(workdir, "pyinstaller"),
           "--specpath", workdir] + extra
    for d in data:
        cmd += ["--add-data", d]
    run(cmd + [os.path.join(workdir, "stage", "builder.py")], cwd=workdir)
    return os.path.join(workdir, "dist", name)


def windows(args):
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
                           os.path.join(OUT, "check_ps1.log"), OUT)
    if with_ps1 != make_iso:
        sys.exit(f"the builder's disc isn't make's: data track {with_ps1}, make's {make_iso}")
    without = builder_run(staged, args.saturn, None, os.path.join(OUT, "check_saturn"),
                          os.path.join(OUT, "check_saturn.log"), OUT)
    known["results"] = dict(with_ps1=with_ps1, without_ps1=without)
    write_known(known)
    print(f"results: with the PlayStation disc {with_ps1} (make's), without {without}")

    sys.path.insert(0, STAGE)
    import art
    art.save_icon(os.path.join(OUT, "icon.ico"))
    root = pyinstaller(os.path.join(VENV, "Scripts", "pyinstaller.exe"), NAME, OUT,
                       ["--windowed", "--icon", os.path.join(OUT, "icon.ico")])
    program_runs([os.path.join(root, NAME + ".exe")], args.saturn, args.ps1, known, OUT)

    base = open(os.path.join(VENV, "pyvenv.cfg")).read()
    home = re.search(r"^home = (.+)$", base, re.M).group(1).strip()
    version = re.search(r"^version(?:_info)? = (\d+\.\d+)", base, re.M).group(1)
    python = [(f"Python {version}, PSF License, with what its Windows build bundles (Tcl/Tk for the builder's "
               f"window among them)", os.path.join(home, "LICENSE.txt"))]
    slug = re.sub(r"[^A-Za-z0-9]+", "-", args.release).strip("-")
    zpath = os.path.join(OUT, f"TombRaiderSaturn-{slug}-windows.zip")
    crlf = lambda text: text.replace("\r\n", "\n").replace("\n", "\r\n")     # Notepad's line ends
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for folder, _, names in os.walk(root):
            for n in sorted(names):
                p = os.path.join(folder, n)
                z.write(p, os.path.join(NAME, os.path.relpath(p, root)))
        z.writestr(f"{NAME}/README.txt", crlf(readme("windows", known, os.path.basename(zpath))))
        z.writestr(f"{NAME}/LICENSE.txt", crlf(open(os.path.join(TREE, "LICENSE"), encoding="utf-8").read()))
        z.writestr(f"{NAME}/THIRD-PARTY-NOTICES.txt", crlf(notices(os.path.join(VENV, "Lib", "site-packages"),
                                                                   python)))
    print(f"{zpath}: {os.path.getsize(zpath) / 1e6:.1f} MB")

    if args.no_linux:
        return
    wsl = lambda p: "/mnt/" + p[0].lower() + p[2:].replace("\\", "/")      # C:\a\b -> /mnt/c/a/b
    cmd = ["wsl", "-d", args.distro, "--cd", wsl(TREE), "-e", "python3", "-I", "builder/release.py",
           args.release, "--linux", "--saturn", wsl(os.path.abspath(args.saturn)),
           "--ps1", wsl(os.path.abspath(args.ps1))]
    print("$", " ".join(cmd), flush=True)
    if subprocess.run(cmd).returncode:
        sys.exit("the Linux download failed (its output is above)")


# --- Linux, in WSL ---------------------------------------------------------------

def fetch(name, sha256):
    """A python-build-standalone archive, downloaded once into the cache and checked."""
    path = os.path.join(LHOME, "cache", name)
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        print(f"downloading {name}", flush=True)
        with urllib.request.urlopen(PBS + urllib.parse.quote(name)) as r, open(path + ".part", "wb") as f:
            shutil.copyfileobj(r, f, 1 << 20)
        os.replace(path + ".part", path)
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    if h.hexdigest() != sha256:
        sys.exit(f"{path}: SHA-256 {h.hexdigest()}, not {sha256}")
    return path


def linux_python():
    """The standalone Python, its virtual environment with the pinned packages, and its licence files:
    (python, licences folder). Made again when the pins change."""
    venv = os.path.join(LHOME, "venv")
    pins = open(os.path.join(HERE, "requirements.txt")).read() + PBS_PYTHON[1]
    mark = os.path.join(venv, "made-from.txt")
    if not os.path.exists(mark) or open(mark).read() != pins:
        for d in ("python", "venv"):
            shutil.rmtree(os.path.join(LHOME, d), ignore_errors=True)     # this function's own output
        with tarfile.open(fetch(*PBS_PYTHON)) as t:
            t.extractall(LHOME, filter="tar")
        run([os.path.join(LHOME, "python", "bin", "python3"), "-m", "venv", venv])
        run([os.path.join(venv, "bin", "python"), "-m", "pip", "install", "-q", "-r",
             os.path.join(HERE, "requirements.txt")])
        with open(mark, "w") as f:
            f.write(pins)
    licences = os.path.join(LHOME, "licenses")
    if not os.path.isdir(licences):
        zvenv = os.path.join(LHOME, "zvenv")
        if not os.path.exists(os.path.join(zvenv, "bin", "python")):
            run([sys.executable, "-m", "venv", zvenv])
            run([os.path.join(zvenv, "bin", "python"), "-m", "pip", "install", "-q", ZSTANDARD])
        script = ("import sys, tarfile, zstandard\n"
                  "with open(sys.argv[1], 'rb') as f, tarfile.open(fileobj=zstandard.ZstdDecompressor()"
                  ".stream_reader(f), mode='r|') as t:\n"
                  "    for m in t:\n"
                  "        if m.name.startswith('python/licenses/') and m.isfile():\n"
                  "            m.name = m.name[len('python/licenses/'):]\n"
                  "            t.extract(m, sys.argv[2], filter='data')\n")
        run([os.path.join(zvenv, "bin", "python"), "-c", script, fetch(*PBS_FULL), licences + ".part"])
        os.replace(licences + ".part", licences)
    return os.path.join(venv, "bin", "python"), licences


def elf_files(root):
    for folder, _, names in os.walk(root):
        for n in sorted(names):
            p = os.path.join(folder, n)
            if not os.path.islink(p):
                with open(p, "rb") as f:
                    if f.read(4) == b"\x7fELF":
                        yield p


def glibc_needs(root):
    """The newest glibc version any of the program's files asks for, which file, the newest GLIBCXX (the C++
    runtime's), and the libraries it takes from the system (those it doesn't bundle)."""
    newest, by, glibcxx, bundled, needed = (0, 0), None, (0,), set(), set()
    for p in elf_files(root):
        bundled.add(os.path.basename(p))
        dyn = subprocess.run(["objdump", "-T", "-p", p], capture_output=True, text=True).stdout
        for v in set(re.findall(r"\bGLIBC_(\d+)\.(\d+)", dyn)):
            v = (int(v[0]), int(v[1]))
            if v > newest:
                newest, by = v, os.path.relpath(p, root)
        glibcxx = max([glibcxx] + [tuple(map(int, v.split("."))) for v in re.findall(r"GLIBCXX_([\d.]+\d)", dyn)])
        needed.update(re.findall(r"^\s+NEEDED\s+(\S+)", dyn, re.M))
    return newest, by, glibcxx, sorted(needed - bundled)


def linux(args):
    known = json.load(open(os.path.join(STAGE, "discs.json")))
    python, licences = linux_python()
    work = os.path.join(LHOME, "build")
    shutil.rmtree(work, ignore_errors=True)       # this function's own output from an earlier run
    shutil.copytree(STAGE, os.path.join(work, "stage"), ignore=shutil.ignore_patterns("__pycache__"))
    root = pyinstaller(os.path.join(os.path.dirname(python), "pyinstaller"), LNAME, work, [])
    # The C++ and GCC runtimes are the player's system's, as the wheels expect (manylinux): PyInstaller copies
    # the build machine's, which ask for its own, newer glibc.
    for n in SYSTEM_RUNTIMES:
        os.remove(os.path.join(root, "_internal", n))
    program_runs([os.path.join(root, LNAME)], args.saturn, args.ps1, known, work)

    newest, by, glibcxx, system = glibc_needs(root)
    print(f"glibc: newest asked for {newest[0]}.{newest[1]} ({by}); GLIBCXX {'.'.join(map(str, glibcxx))}; "
          f"from the system: {', '.join(system)}")
    if newest > GLIBC_MAX or glibcxx > GLIBCXX_MAX:
        sys.exit(f"the program asks for glibc {newest[0]}.{newest[1]} or GLIBCXX {'.'.join(map(str, glibcxx))}, "
                 f"newer than glibc {GLIBC_MAX[0]}.{GLIBC_MAX[1]} (the read-me's promise) or GLIBCXX "
                 f"{'.'.join(map(str, GLIBCXX_MAX))} (GCC 8, as old as that glibc)")

    version = re.search(r"cpython-([\d.]+)\+(\d+)", PBS_PYTHON[0])
    python_parts = [(f"Python {version.group(1)} (python-build-standalone {version.group(2)}), PSF License",
                     os.path.join(licences, "LICENSE.cpython.txt"))]
    python_parts += [(f"{n[len('LICENSE.'):-len('.txt')]}, in the standalone Python's build", os.path.join(licences, n))
                     for n in sorted(os.listdir(licences)) if n != "LICENSE.cpython.txt"]
    site = os.path.join(LHOME, "venv", "lib", f"python{'.'.join(version.group(1).split('.')[:2])}",
                        "site-packages")
    slug = re.sub(r"[^A-Za-z0-9]+", "-", args.release).strip("-")
    archive = f"TombRaiderSaturn-{slug}-linux-x86_64.tar.gz"
    texts = {"README.txt": readme("linux", known, archive),
             "LICENSE.txt": open(os.path.join(TREE, "LICENSE"), encoding="utf-8").read(),
             "THIRD-PARTY-NOTICES.txt": notices(site, python_parts)}

    def owner(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        return info

    tpath = os.path.join(OUT, archive)
    with tarfile.open(tpath + ".part", "w:gz", compresslevel=9) as t:
        t.add(root, LNAME, filter=owner)
        for n, text in texts.items():
            data = text.replace("\r\n", "\n").encode("utf-8")
            info = owner(tarfile.TarInfo(f"{LNAME}/{n}"))
            info.size, info.mode, info.mtime = len(data), 0o644, int(time.time())
            t.addfile(info, io.BytesIO(data))
    os.replace(tpath + ".part", tpath)
    print(f"{tpath}: {os.path.getsize(tpath) / 1e6:.1f} MB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("release", help='its name, e.g. "Engineering Sample 1"')
    ap.add_argument("--saturn", default=os.path.join(ER, "tomb_raider_usa", "Tomb Raider (USA).cue"))
    ap.add_argument("--ps1", default=os.path.join(ER, "ps1", "Tomb Raider (USA) (Rev 1).cue"))
    ap.add_argument("--no-linux", action="store_true", help="the Windows zip only")
    ap.add_argument("--distro", default="Ubuntu", help="the WSL distribution the Linux download is made in")
    ap.add_argument("--linux", action="store_true", help="(in WSL, from the Windows run) the Linux download")
    args = ap.parse_args()
    (linux if args.linux else windows)(args)


if __name__ == "__main__":
    main()
