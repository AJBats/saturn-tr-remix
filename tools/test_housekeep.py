"""Tests tools/housekeep.py on a throwaway repository: python -I tools/test_housekeep.py
Every rule, above all that it never reaches through a link or takes what it should keep."""
import os
import shutil
import subprocess
import sys
import tempfile
import time

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'housekeep.py')
OLD = time.time() - 5 * 86400
failures = []


def check(what, ok):
    print(('ok    ' if ok else 'FAIL  ') + what)
    if not ok:
        failures.append(what)


def write(path, text='x', age=OLD):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(text)
    if age:
        os.utime(path, (age, age))


def disc(folder, age=OLD, extra=()):
    """A disc folder as tools/snapshot.sh leaves it, its audio hard-linked from AUDIO."""
    write(os.path.join(folder, 'tomb_track01.iso'), 'data', age)
    write(os.path.join(folder, 'tomb.cue'), 'cue', age)
    os.makedirs(folder, exist_ok=True)
    os.link(AUDIO, os.path.join(folder, 'tomb_track02.bin'))
    for name in extra:
        write(os.path.join(folder, name), 'x', age)


def link_dir(link, target):
    os.makedirs(os.path.dirname(link), exist_ok=True)
    if os.name == 'nt':
        subprocess.run(['cmd', '/c', 'mklink', '/J', link, target], check=True, capture_output=True)
    else:
        os.symlink(target, link)


def age_tree(tree):
    for path, _, names in os.walk(tree):
        for n in names:
            os.utime(os.path.join(path, n), (OLD, OLD))


def run(*args):
    return subprocess.run([sys.executable, '-I', os.path.join(repo, 'tools', 'housekeep.py')] + list(args), capture_output=True, text=True, cwd=repo)


def git(*args, cwd=None):
    return subprocess.run(['git', '-C', cwd or repo, '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'core.autocrlf=false'] + list(args), capture_output=True, text=True)


root = tempfile.mkdtemp(prefix='hk')
try:
    repo = os.path.join(root, 'repo')
    os.makedirs(os.path.join(repo, 'tools'))
    shutil.copy(TOOL, os.path.join(repo, 'tools', 'housekeep.py'))
    write(os.path.join(repo, '.gitignore'), 'build/\nexternal_resources/\n', None)
    write(os.path.join(repo, 'code.txt'), 'one\n', None)
    git('init', '-q', '-b', 'main')
    git('add', '.')
    git('commit', '-qm', 'first')

    res = os.path.join(repo, 'external_resources')
    write(os.path.join(res, 'retail.bin'), 'retail disc')
    AUDIO = os.path.join(res, 'audio.bin')
    write(AUDIO, 'audio')

    b = os.path.join(repo, 'build')
    disc(os.path.join(b, 'disc'))
    disc(os.path.join(b, 'disc_old'), extra=['0MAIN.elf', '0MAIN.map'])
    disc(os.path.join(b, 'disc_young'), age=time.time() - 3600)
    disc(os.path.join(b, 'disc_marked'), extra=['KEEP'])
    disc(os.path.join(b, 'disc_state'), extra=['fight.mc0'])
    disc(os.path.join(b, 'disc_running'))
    disc(os.path.join(b, 'disc_running2'))
    disc(os.path.join(b, 'disc_open'))
    disc(os.path.join(b, 'x', 'disc'))
    write(os.path.join(b, 'x', 'obj', 'a.o'))
    write(os.path.join(b, 'disc_nocue', 'tomb_track01.iso'), 'data')
    disc(os.path.join(b, 'disc_linked'))
    link_dir(os.path.join(b, 'disc_linked', 'res'), res)

    outside = os.path.join(root, 'outside')
    big = os.path.join(outside, 'huge.txt')
    os.makedirs(outside)
    disc(os.path.join(outside, 'disc_out'))
    with open(big, 'wb') as f:
        f.truncate(200 * 1024 * 1024)
    os.utime(big, (OLD, OLD))
    link_dir(os.path.join(b, 'captures'), outside)
    for name, age in (('old', OLD), ('young', time.time() - 3600)):
        path = os.path.join(b, 'night', name + '.trace.txt')
        write(path, '', None)
        with open(path, 'wb') as f:
            f.truncate(101 * 1024 * 1024)
        os.utime(path, (age, age))
    with open(os.path.join(b, 'small.txt'), 'w') as f:
        f.write('notes')
    os.utime(os.path.join(b, 'small.txt'), (OLD, OLD))
    write(os.path.join(b, 'night', 'read.mkv'))
    write(os.path.join(b, 'night', 'read.json'))
    write(os.path.join(b, 'night', 'unread.mkv'))
    write(os.path.join(b, 'data', 'LEVEL1.PKD'))

    # A worktree, its own disc swept like the main checkout's, its link to the resources left alone.
    wt = os.path.join(root, 'hkwt_side')
    git('worktree', 'add', '-q', '-b', 'side', wt)
    link_dir(os.path.join(wt, 'external_resources'), res)
    disc(os.path.join(wt, 'build', 'disc_tree'))
    disc(os.path.join(wt, 'builds', 'video'))

    dry = run('--dry-run')
    check('dry run clears nothing', os.path.exists(os.path.join(b, 'disc_old', 'tomb.cue')) and 'would remove' in dry.stdout)

    held = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)', os.path.join(b, 'disc_running', 'tomb.cue').replace('\\', '/')])
    held2 = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)', 'hw.py', 'test', os.path.relpath(os.path.join(b, 'disc_running2'), root)])
    handle = open(os.path.join(b, 'disc_open', 'tomb_track01.iso'), 'rb')
    out = run()
    if out.returncode:
        print(out.stdout, out.stderr)
    handle.close()
    held.kill()
    held2.kill()
    held.wait()
    held2.wait()

    e = lambda *p: os.path.exists(os.path.join(b, *p))
    check('an old disc goes', not e('disc_old'))
    check("a tree's own build/disc stays", e('disc', 'tomb.cue'))
    check('a recent disc stays', e('disc_young', 'tomb.cue'))
    check('KEEP holds a disc', e('disc_marked', 'tomb.cue'))
    check('a save state holds a disc', e('disc_state', 'fight.mc0') and e('disc_state', 'tomb_track01.iso'))
    check('a disc named by a running program stays', e('disc_running', 'tomb.cue'))
    check('a disc named by a relative path stays', e('disc_running2', 'tomb.cue'))
    if os.name == 'nt':
        check('a disc with a file open stays whole', e('disc_open', 'tomb.cue') and e('disc_open', 'tomb_track01.iso'))
    check("a build's disc goes, its objects stay", not e('x', 'disc') and e('x', 'obj', 'a.o'))
    check('a folder with no cue is not taken for a disc', e('disc_nocue', 'tomb_track01.iso'))
    check('a folder holding a link stays', e('disc_linked', 'tomb.cue'))
    check('resources behind links are intact', open(os.path.join(res, 'retail.bin')).read() == 'retail disc')
    check('the hard-linked audio survives', open(AUDIO).read() == 'audio')
    check('nothing is taken through a link: disc', os.path.exists(os.path.join(outside, 'disc_out', 'tomb.cue')))
    check('nothing is taken through a link: trace', os.path.exists(big))
    check('an old trace dump goes', not e('night', 'old.trace.txt'))
    check('a recent trace dump stays', e('night', 'young.trace.txt'))
    check('a small text file stays', e('small.txt'))
    check('a recording goes once read', not e('night', 'read.mkv') and e('night', 'read.json'))
    check('a recording nothing read stays', e('night', 'unread.mkv'))
    check('other build output stays', e('data', 'LEVEL1.PKD'))
    check("a worktree's disc goes", not os.path.exists(os.path.join(wt, 'build', 'disc_tree')))
    check('builds/ is left alone', os.path.exists(os.path.join(wt, 'builds', 'video', 'tomb.cue')))
    log = open(os.path.join(b, 'housekeep.log')).read()
    check('every removal is logged', 'disc_old' in log and 'old.trace.txt' in log and 'read.mkv' in log)
    check('no half-deleted folder left', not [n for n in os.listdir(b) if n.endswith('.housekeep-del')])

    disc(os.path.join(b, 'disc_due'))
    run('--if-due')
    check('--if-due clears when due', not e('disc_due'))
    disc(os.path.join(b, 'disc_due2'))
    quiet = run('--if-due')
    check('--if-due waits an hour', e('disc_due2', 'tomb.cue') and not quiet.stdout.strip())

    # drop
    check('drop refuses the main checkout', run('drop', 'repo').returncode != 0 and os.path.exists(os.path.join(repo, 'code.txt')))
    os.utime(os.path.join(wt, 'code.txt'), None)
    young = run('drop', 'hkwt_side')
    check('drop refuses a tree written to today', 'written to within a day' in young.stderr and os.path.exists(os.path.join(wt, 'code.txt')))
    write(os.path.join(wt, 'code.txt'), 'one\nchanged\n')
    write(os.path.join(wt, 'run.log'), 'log')
    age_tree(wt)
    busy = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)', wt])
    named = run('drop', 'hkwt_side')
    busy.kill()
    busy.wait()
    check('drop refuses a tree a running program names', 'a running program names it' in named.stderr and os.path.exists(os.path.join(wt, 'code.txt')))
    done = run('drop', 'hkwt_side')
    if done.returncode:
        print(done.stdout, done.stderr)
    check('drop takes the worktree away', not os.path.exists(wt) and 'hkwt_side' not in git('worktree', 'list').stdout)
    check('drop leaves the resources whole', open(os.path.join(res, 'retail.bin')).read() == 'retail disc' and os.path.exists(AUDIO))
    check('drop keeps uncommitted changes as a patch', 'changed' in open(os.path.join(b, 'worktree_leftovers', 'hkwt_side.patch')).read())
    check('drop keeps untracked files', os.path.exists(os.path.join(b, 'worktree_leftovers', 'hkwt_side', 'run.log')))
    check('drop keeps the branch', 'side' in git('branch', '--list', 'side').stdout)

    # A detached commit main doesn't have would be lost, so it is refused.
    wt2 = os.path.join(root, 'hkwt_two')
    git('worktree', 'add', '-q', '--detach', wt2)
    write(os.path.join(wt2, 'new.txt'), 'new\n', None)
    git('add', '.', cwd=wt2)
    git('commit', '-qm', 'only here', cwd=wt2)
    age_tree(wt2)
    check('drop refuses a commit on no branch', 'on no branch' in run('drop', 'hkwt_two').stderr and os.path.exists(os.path.join(wt2, 'new.txt')))

    # A big untracked file can't be kept aside, so it is refused.
    wt3 = os.path.join(root, 'hkwt_three')
    git('worktree', 'add', '-q', '--detach', wt3)
    with open(os.path.join(wt3, 'big.dat'), 'wb') as f:
        f.truncate(11 * 1024 * 1024)
    age_tree(wt3)
    check('drop refuses big untracked files', 'too big' in run('drop', 'hkwt_three').stderr and os.path.exists(os.path.join(wt3, 'big.dat')))
finally:
    if os.name == 'nt':
        for dp, dn, _ in os.walk(root):
            for d in list(dn):
                p = os.path.join(dp, d)
                if os.lstat(p).st_file_attributes & 0x400:
                    os.rmdir(p)
                    dn.remove(d)
    shutil.rmtree(root, ignore_errors=True)

print(f'\n{len(failures)} failed' if failures else '\nall passed')
sys.exit(1 if failures else 0)
