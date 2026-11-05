"""Keeps test discs and trace dumps from eating the disk. A disc build runs it by itself.

    python -I tools/housekeep.py              what's there, then clears what the rules allow
    python -I tools/housekeep.py --dry-run    what's there and what would go, clearing nothing
    python -I tools/housekeep.py --if-due     clears, at most once an hour, quiet unless it removed something
    python -I tools/housekeep.py drop NAME    takes a worktree away: see drop() below

The rules, over the main checkout and every worktree, for anything older than MAX_AGE_DAYS:
  - A test disc goes: build/NAME holding a data track and cue, or the disc/ folder of a build
    in build/NAME (its objects stay, and make remakes the disc). A disc is a build output: to
    see an old one again, rebuild it from its commit.
  - A trace dump goes: a .txt of over BIG_TRACE_MB anywhere under build/.
  - A Saturn recording goes: build/night/TAG.mkv, once TAG.json holds what was read from it.

What always stays:
  - build/disc itself, each tree's current disc.
  - A folder with a file called KEEP in it: `touch build/disc_NAME/KEEP`.
  - A folder holding anything a build can't remake, such as save states or pictures.
  - Anything a running program names on its command line or has open.
  - builds/ (the video's discs) and everything outside build/.

Every removal is written to build/housekeep.log in the main checkout. No link is ever followed:
worktrees hold junctions into the main checkout, including the retail discs.
"""
import argparse
import glob
import os
import re
import shutil
import stat
import subprocess
import sys
import time

MAX_AGE_DAYS = 2
BIG_TRACE_MB = 100
DUE_SECONDS = 3600
IDLE_DAYS = 7
LEFTOVER_MAX_MB = 10
DISC_FILES = {'.iso', '.bin', '.cue', '.elf', '.map'}
DELETING = '.housekeep-del'
HERE = os.path.dirname(os.path.abspath(__file__))
GB = 1024 ** 3
MB = 1024 ** 2


def git(*args, cwd=HERE):
    return subprocess.run(['git', '-C', cwd] + list(args), capture_output=True, text=True, errors='replace')


def worktrees():
    """Every checkout as (path, head, branch or None), the main checkout first."""
    trees = []
    for block in git('worktree', 'list', '--porcelain').stdout.split('\n\n'):
        fields = dict(line.split(' ', 1) for line in block.splitlines() if ' ' in line)
        if 'worktree' in fields:
            trees.append((os.path.normpath(fields['worktree']), fields.get('HEAD'), fields.get('branch')))
    return trees


def running_commands():
    """All running programs' command lines, or None if they can't be read. This tool's own are left out: they name what it's asked to drop."""
    if os.name == 'nt':
        cmd = ['powershell', '-NoProfile', '-Command', 'Get-CimInstance Win32_Process | ForEach-Object { $_.CommandLine }']
    else:
        cmd = ['ps', '-eo', 'args']
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=120)
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode or not r.stdout.strip():
        return None
    lines = r.stdout.replace('\\', '/').lower().splitlines()
    return '\n'.join(line for line in lines if 'housekeep.py' not in line)


def is_link(st):
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def walk(path):
    """Yields (full path, stat) of everything under path, without entering links."""
    try:
        entries = list(os.scandir(path))
    except OSError:
        return
    for e in entries:
        try:
            st = os.lstat(e.path)
        except OSError:
            continue
        yield e.path, st
        if stat.S_ISDIR(st.st_mode) and not is_link(st):
            yield from walk(e.path)


def remove_tree(path):
    """Removes a folder; a link inside it loses the link alone."""
    for e in list(os.scandir(path)):
        st = os.lstat(e.path)
        if stat.S_ISDIR(st.st_mode) and not is_link(st):
            remove_tree(e.path)
        else:
            if not st.st_mode & stat.S_IWRITE:
                os.chmod(e.path, stat.S_IWRITE)
            os.unlink(e.path)
    os.rmdir(path)


def remove_unless_open(path):
    """Renames first: Windows refuses that while a program has anything inside open, so nothing is left half gone."""
    doomed = path + DELETING
    try:
        os.rename(path, doomed)
    except OSError:
        return False
    remove_tree(doomed)
    return True


def named_by(commands, folder):
    tail = folder.replace('\\', '/').lower().rsplit('/build/', 1)[-1]
    return re.search(r'build/' + re.escape(tail) + r'(?![a-z0-9_\-])', commands) is not None


def disc_folders(tree):
    """Every test disc's folder under a tree's build/, build/disc itself left out."""
    build = os.path.join(tree, 'build')
    try:
        if is_link(os.lstat(build)):
            return
        entries = list(os.scandir(build))
    except OSError:
        return
    for e in entries:
        st = os.lstat(e.path)
        if not stat.S_ISDIR(st.st_mode) or is_link(st) or e.name == 'disc' or e.name.endswith(DELETING):
            continue
        for folder in (e.path, os.path.join(e.path, 'disc')):
            if not glob.glob(os.path.join(glob.escape(folder), '*_track01.iso')):
                continue
            try:
                if not is_link(os.lstat(folder)):
                    yield folder
            except OSError:
                pass


def disc_verdict(folder, cutoff, commands):
    """(size of the data track, None if it may go or why it stays)."""
    newest = size = 0
    hold = None
    for path, st in walk(folder):
        name = os.path.basename(path)
        if name == 'KEEP':
            hold = 'marked KEEP'
        elif hold != 'marked KEEP' and (is_link(st) or stat.S_ISDIR(st.st_mode) or os.path.splitext(name)[1].lower() not in DISC_FILES):
            hold = f'holds {name}'
        if not stat.S_ISDIR(st.st_mode):
            newest = max(newest, st.st_mtime)
            if name.endswith('.iso'):
                size += st.st_size
    if not hold and not glob.glob(os.path.join(glob.escape(folder), '*.cue')):
        hold = 'has no cue'
    if not hold and newest > cutoff:
        hold = 'recent'
    if not hold and named_by(commands, folder):
        hold = 'in use'
    return size, hold


def sweep(dry, quiet):
    commands = running_commands()
    if commands is None:
        print('housekeep: running programs could not be read, so nothing was cleared')
        return
    trees = worktrees()
    main = trees[0][0]
    cutoff = time.time() - MAX_AGE_DAYS * 86400
    log_path = os.path.join(main, 'build', 'housekeep.log')
    removed = []

    def gone(path, size, why):
        removed.append(size)
        if dry:
            print(f'  would remove {size / MB:6.0f} MB  {path}')
            return
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        with open(log_path, 'a') as f:
            f.write(f'{time.strftime("%Y-%m-%d %H:%M")}  {size / MB:6.0f} MB  {why}  {path}\n')

    stock = {}
    for tree, _, _ in trees:
        total = count = kept = 0
        build = os.path.join(tree, 'build')
        for leftover in glob.glob(os.path.join(glob.escape(build), '*' + DELETING)) + glob.glob(os.path.join(glob.escape(build), '*', '*' + DELETING)):
            if not dry:
                remove_tree(leftover)
        for folder in list(disc_folders(tree)):
            size, hold = disc_verdict(folder, cutoff, commands)
            if not hold and (dry or remove_unless_open(folder)):
                gone(folder, size, 'old disc')
                continue
            total += size
            count += 1
            kept += hold == 'marked KEEP'
        stock[tree] = (total, count, kept)
        try:
            walkable = not is_link(os.lstat(build))
        except OSError:
            walkable = False
        for path, st in (walk(build) if walkable else ()):
            if (path.endswith('.txt') and st.st_size > BIG_TRACE_MB * MB and st.st_mtime < cutoff
                    and stat.S_ISREG(st.st_mode) and not is_link(st) and not named_by(commands, path)):
                try:
                    if not dry:
                        os.remove(path)
                    gone(path, st.st_size, 'old trace dump')
                except OSError:
                    pass

    for path in glob.glob(os.path.join(glob.escape(main), 'build', 'night', '*.mkv')):
        st = os.lstat(path)
        if st.st_mtime < cutoff and os.path.exists(os.path.splitext(path)[0] + '.json') and not is_link(st) and not named_by(commands, path):
            try:
                if not dry:
                    os.remove(path)
                gone(path, st.st_size, 'old recording')
            except OSError:
                pass

    if removed or not quiet:
        if dry:
            print(f'housekeep: would free {sum(removed) / GB:.1f} GB in {len(removed)} items older than {MAX_AGE_DAYS} days')
        else:
            print(f'housekeep: freed {sum(removed) / GB:.1f} GB in {len(removed)} items older than {MAX_AGE_DAYS} days; '
                  f'a list is in build/housekeep.log; put a file called KEEP in a disc folder to hold it')
    if quiet:
        return
    print(f'\n{"data tracks left":>18}  discs  marked KEEP  tree')
    for tree, (total, count, kept) in sorted(stock.items(), key=lambda kv: -kv[1][0]):
        print(f'{total / GB:15.1f} GB  {count:5}  {kept:11}  {os.path.basename(tree)}')
    print(f'\nfree on this drive: {shutil.disk_usage(main).free / GB:.0f} GB')
    idle = []
    for tree, head, branch in trees[1:]:
        newest = max((st.st_mtime for _, st in walk(tree) if not stat.S_ISDIR(st.st_mode)), default=0)
        if newest < time.time() - IDLE_DAYS * 86400:
            idle.append(os.path.basename(tree))
    if idle:
        print(f'worktrees with no file written in {IDLE_DAYS} days (housekeep.py drop NAME): {" ".join(idle)}')


def tree_fingerprint(path):
    count = size = 0
    for _, st in walk(path):
        if not stat.S_ISDIR(st.st_mode):
            count += 1
            size += st.st_size
    return count, size


def drop(name):
    """Takes a worktree away, keeping what's only there: uncommitted changes become a patch and untracked files a copy,
    both in build/worktree_leftovers. Refuses if it would lose commits, is in use, or has been written to within a day.
    A branch stays; only the folder goes. Links are removed one by one first, and what they point at is checked afterwards."""
    trees = worktrees()
    main = trees[0][0]
    match = [t for t in trees[1:] if os.path.basename(t[0]).lower() == name.lower()]
    if not match:
        sys.exit(f'{name} is not a worktree (the main checkout can never be dropped)')
    tree, head, branch = match[0]
    if not branch and git('merge-base', '--is-ancestor', head, 'HEAD', cwd=main).returncode:
        sys.exit(f'{name}: {head[:7]} is on no branch and not in main; dropping it would lose the commit. Make a branch first')
    commands = running_commands()
    if commands is None:
        sys.exit('running programs could not be read')
    if re.search(r'(?<![a-z0-9_\-])' + re.escape(os.path.basename(tree).lower()) + r'(?![a-z0-9_\-])', commands):
        sys.exit(f'{name}: a running program names it')
    items = list(walk(tree))
    newest = max((st.st_mtime for _, st in items if not stat.S_ISDIR(st.st_mode)), default=0)
    if newest > time.time() - 86400:
        sys.exit(f'{name}: written to within a day ({time.strftime("%m-%d %H:%M", time.localtime(newest))})')

    untracked = git('ls-files', '--others', '--exclude-standard', '-z', cwd=tree).stdout.split('\0')
    untracked = [u for u in untracked if u and not is_link(os.lstat(os.path.join(tree, u)))]
    too_big = [u for u in untracked if os.path.getsize(os.path.join(tree, u)) > LEFTOVER_MAX_MB * MB]
    if too_big:
        sys.exit(f'{name}: untracked files too big to keep aside: {too_big[:5]}')

    leftovers = os.path.join(main, 'build', 'worktree_leftovers')
    os.makedirs(leftovers, exist_ok=True)
    diff = git('diff', 'HEAD', cwd=tree)
    if diff.returncode:
        sys.exit(f'{name}: git diff failed: {diff.stderr.strip()}')
    if diff.stdout.strip():
        with open(os.path.join(leftovers, name + '.patch'), 'w', newline='\n') as f:
            f.write(diff.stdout)
    for u in untracked:
        target = os.path.join(leftovers, name, u)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(os.path.join(tree, u), target)

    links = [path for path, st in items if is_link(st)]
    targets = {os.path.realpath(p) for p in links}
    before = {t: tree_fingerprint(t) for t in targets if os.path.isdir(t)}
    for path in links:
        os.unlink(path)
    left = [path for path, st in walk(tree) if is_link(st)]
    if left:
        sys.exit(f'{name}: links still there, stopping: {left[:5]}')
    if not remove_unless_open(tree):
        sys.exit(f'{name}: something has a file open in it; its links are gone, the rest stays')
    for t, was in before.items():
        if tree_fingerprint(t) != was:
            sys.exit(f'STOP: {t} changed while {name} was dropped: {was} before, {tree_fingerprint(t)} now')
    git('worktree', 'prune', cwd=main)
    line = f'{time.strftime("%Y-%m-%d %H:%M")}  dropped worktree {name} at {head[:7]}{" on " + branch if branch else ""}'
    with open(os.path.join(main, 'build', 'housekeep.log'), 'a') as f:
        f.write(line + '\n')
    print(f'{name}: gone, {len(links)} links removed first, what they point at unchanged. '
          f'To have it back: git worktree add ../{os.path.basename(tree)} {branch.split("/")[-1] if branch else head[:7]}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--if-due', action='store_true')
    ap.add_argument('command', nargs='?', choices=['drop'])
    ap.add_argument('name', nargs='?')
    args = ap.parse_args()
    if args.command == 'drop':
        if not args.name:
            ap.error('drop needs a worktree name')
        drop(args.name)
        return
    if args.if_due:
        stamp = os.path.join(worktrees()[0][0], 'build', 'housekeep.stamp')
        try:
            if time.time() - os.path.getmtime(stamp) < DUE_SECONDS:
                return
        except OSError:
            pass
        os.makedirs(os.path.dirname(stamp), exist_ok=True)
        with open(stamp, 'w'):
            pass
    sweep(args.dry_run, args.if_due)


if __name__ == '__main__':
    main()
