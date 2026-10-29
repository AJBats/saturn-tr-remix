"""A private Mednafen for the crunch lab: its own IPC directory and home (a
copy of build/mednafen_home's settings and firmware), so it never races the
MCP server's instance or other sessions'.

    with Mednafen(cue) as m:
        m.send('run_to_frame 1800', 'done run_to_frame', 120)
"""
import os
import shutil
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, 'tools', 'mednafen', 'src', 'mednafen.exe')
SHARED_HOME = os.path.join(ROOT, 'build', 'mednafen_home')
LAB = os.path.join(ROOT, 'build', 'crunch')


def fwd(path):
    return os.path.abspath(path).replace('\\', '/')


class Mednafen:
    def __init__(self, cue, name='med'):
        self.cue = cue
        self.ipc = os.path.join(LAB, name + '_ipc')
        self.home = os.path.join(LAB, name + '_home')
        self.ack_file = os.path.join(self.ipc, 'mednafen_ack.txt')
        self.action_file = os.path.join(self.ipc, 'mednafen_action.txt')
        self.proc = None
        self.seq = 0

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()

    def start(self, timeout=60):
        os.makedirs(self.ipc, exist_ok=True)
        os.makedirs(os.path.join(self.home, 'firmware'), exist_ok=True)
        shutil.copy(os.path.join(SHARED_HOME, 'mednafen.cfg'), self.home)
        for f in os.listdir(os.path.join(SHARED_HOME, 'firmware')):
            dst = os.path.join(self.home, 'firmware', f)
            if not os.path.exists(dst):
                shutil.copy(os.path.join(SHARED_HOME, 'firmware', f), dst)
        for f in (self.ack_file, self.action_file, os.path.join(self.home, 'mednafen.lck')):
            if os.path.exists(f):
                os.remove(f)
        env = dict(os.environ, MEDNAFEN_HOME=self.home)
        self.stderr = open(os.path.join(LAB, os.path.basename(self.ipc) + '_stderr.txt'), 'w')
        self.proc = subprocess.Popen([EXE, '--sound', '0', '-cd.image_memcache', '1', '--automation', self.ipc,
                                      self.cue], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=self.stderr, env=env)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f'Mednafen exited with code {self.proc.returncode}')
            if 'ready' in self.ack():
                return
            time.sleep(0.2)
        raise RuntimeError('Mednafen did not come up')

    def stop(self):
        """Quits the instance this object started (and only that one)."""
        if self.proc and self.proc.poll() is None:
            try:
                self.send('quit', 'quit', 10)
            except RuntimeError:
                pass
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                pass
        self.proc = None

    def ack(self):
        try:
            with open(self.ack_file) as f:
                return f.read().strip()
        except OSError:
            return ''

    def send(self, cmd, expect, timeout=30):
        """Sends one command and returns the first reply holding expect."""
        self.seq += 1
        before = self.ack()
        tmp = self.action_file + '.tmp'
        with open(tmp, 'w', newline='\n') as f:
            f.write(f'# {self.seq}{"." * (self.seq % 16)}\n{cmd}\n')
        for attempt in range(50):
            try:
                os.replace(tmp, self.action_file)
                break
            except PermissionError:     # Mednafen is reading it
                time.sleep(0.02 * (attempt + 1))
        else:
            os.replace(tmp, self.action_file)
        deadline = time.time() + timeout
        while time.time() < deadline:
            reply = self.ack()
            if reply and reply != before:
                if expect in reply:
                    return reply
                if reply.startswith('error'):
                    raise RuntimeError(f'{cmd}: {reply}')
            if self.proc.poll() is not None:
                raise RuntimeError(f'{cmd}: Mednafen exited')
            time.sleep(0.01)
        raise RuntimeError(f'{cmd}: no reply with "{expect}" in {timeout} s (last: {self.ack()})')
