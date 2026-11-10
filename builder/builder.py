"""The disc builder players download: a window that takes their own Tomb Raider discs, the Saturn one and,
if they have it, the PlayStation one, and builds our disc from them (discbuild.py does the work). Given
--saturn, it builds without a window: discbuild.py's arguments. Discs and folders are chosen with the system's
own dialogs: Windows' through Tk, the Linux desktop's through dialogs.py.
"""
import multiprocessing
import os
import queue
import subprocess
import sys
import threading
import traceback

if sys.stdout is None:      # a windowed program, and its worker processes, have nowhere to print
    sys.stdout = open(os.devnull, "w")
if sys.stderr is None:
    sys.stderr = open(os.devnull, "w")
if not getattr(sys, "frozen", False):   # discbuild.py and art.py beside it, under python -I too
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

COLOURS = dict(bg="#15120f", panel="#211c17", hover="#2b241d", line="#3a3027", text="#efe6d6", muted="#a89c88",
               gold="#d9a441", gold_hi="#e8b75a", ink="#1a140e", ok="#94c36f", bad="#e58468")
# each kind's first face Tk finds (pick_fonts): on Windows the first. The Linux build's Tk draws with X's core
# fonts and finds none, so it takes its own.
FACES = dict(text=("Segoe UI", "Ubuntu", "Noto Sans", "DejaVu Sans", "Liberation Sans"),
             title=("Georgia", "DejaVu Serif", "Noto Serif", "Liberation Serif"),
             mono=("Consolas", "DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono"))
FONT, TITLE_FONT, MONO = (FACES[k][0] for k in ("text", "title", "mono"))
WIDTH = 760
# the few symbols the window shows: the Linux build draws with X's core fonts, which lack them
DOTS, YES, NO = ("…", "✓", "✗") if sys.platform == "win32" else ("...", "OK", "No")
PATH_CHARS = 80 if sys.platform == "win32" else 68      # what fits the disc's folder line (X's fonts are wider)
# Linux file names are case-sensitive, and rips come either way
RIPS = [("Disc rip (.cue, .zip)", ("*.cue", "*.zip") if sys.platform == "win32" else ("*.cue", "*.CUE", "*.zip", "*.ZIP"))]


def pick_fonts():
    global FONT, TITLE_FONT, MONO
    from tkinter import font
    have = set(font.families())
    FONT, TITLE_FONT, MONO = (next((f for f in FACES[k] if f in have), FACES[k][0])
                              for k in ("text", "title", "mono"))


def open_folder(path):
    if sys.platform == "win32":
        os.startfile(path)
        return
    import dialogs
    subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", path], env=dialogs.system_env(),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def terminal_command():
    """How the player starts this builder from a terminal in its folder."""
    if getattr(sys, "frozen", False):
        return "./" + os.path.basename(sys.executable)
    return "python3 " + os.path.abspath(sys.argv[0])


def short(path, n=PATH_CHARS):
    """A path to fit a line: its end, where it's long."""
    return path if len(path) <= n else DOTS + path[-(n - len(DOTS)):]


def default_out(release):
    base = os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else os.getcwd()
    return os.path.join(base, f"Tomb Raider Saturn - {release}")


class Slot:
    """One disc: its picture, names, the Choose button and what its check found."""

    def __init__(self, app, parent, title, subtitle, label, ring, check, optional):
        self.app, self.check, self.optional = app, check, optional
        self.path = None
        self.ok = False
        self.result = None
        import art
        self.images = [app.photo(art.disc(56, label, ring, lit)) for lit in (False, True)]
        c = COLOURS
        self.frame = tk.Frame(parent, bg=c["panel"], highlightthickness=1, highlightbackground=c["line"])
        self.pic = tk.Label(self.frame, image=self.images[0], bg=c["panel"])
        self.pic.grid(row=0, column=0, rowspan=3, padx=(14, 12), pady=12)
        tk.Label(self.frame, text=title, bg=c["panel"], fg=c["text"], font=(FONT, 12, "bold"),
                 anchor="w").grid(row=0, column=1, sticky="sw", pady=(12, 0))
        tk.Label(self.frame, text=subtitle, bg=c["panel"], fg=c["muted"], font=(FONT, 9),
                 anchor="w").grid(row=1, column=1, sticky="w")
        self.status = tk.Label(self.frame, text="Not chosen" + (" (optional)" if optional else ""),
                               bg=c["panel"], fg=c["muted"], font=(FONT, 9), anchor="w", justify="left",
                               wraplength=480)
        self.status.grid(row=2, column=1, sticky="nw", pady=(2, 12))
        buttons = tk.Frame(self.frame, bg=c["panel"])
        buttons.grid(row=0, column=2, rowspan=3, padx=14)
        self.choose = app.button(buttons, "Choose" + DOTS, self.pick)
        self.choose.pack(side="left")
        if optional:
            self.clear = app.button(buttons, "Leave out", self.leave_out)
            self.clear.pack(side="left", padx=(8, 0))
        self.frame.columnconfigure(1, weight=1)

    def pick(self):
        # the folder of the rip chosen last, here or in the other slot
        start = next((os.path.dirname(s.path) for s in (self, self.app.saturn, self.app.ps1) if s.path), None)
        self.app.ask(self.picked, "Choose the rip's .cue file, or a .zip of the rip", start=start)

    def picked(self, path):
        self.path, self.ok, self.result = path, False, None
        self.show("Checking " + os.path.basename(path) + DOTS, "muted")
        self.pic.configure(image=self.images[0])
        self.app.refresh()
        threading.Thread(target=self._check, args=(path,), daemon=True).start()

    def _check(self, path):
        try:
            self.app.events.put((self._checked, path, self.check(path), None))
        except discbuild.Failed as e:
            self.app.events.put((self._checked, path, None, str(e)))
        except Exception:       # noqa: BLE001
            self.app.events.put((self._checked, path, None, "It can't be read:\n" + traceback.format_exc(limit=1)))

    def _checked(self, path, result, why):
        if path != self.path:
            return      # another rip chosen since
        self.ok, self.result = result is not None, None     # the build reads it again: no copy held
        name = os.path.basename(path)
        if self.ok:
            self.show(f"{YES}  {name}: every file as it should be", "ok")
        else:
            self.show(f"{NO}  {name}: {why}", "bad")
        self.pic.configure(image=self.images[1 if self.ok else 0])
        self.app.refresh()

    def leave_out(self):
        self.path, self.ok, self.result = None, False, None
        self.show("Not chosen (optional)", "muted")
        self.pic.configure(image=self.images[0])
        self.app.refresh()

    def show(self, text, colour):
        self.status.configure(text=text, fg=COLOURS[colour])

    def enable(self, on):
        for b in (self.choose, getattr(self, "clear", None)):
            if b:
                b.configure(state="normal" if on else "disabled")


class App:
    def __init__(self):
        self.root = tk.Tk()
        pick_fonts()
        self.known = discbuild.manifest()
        self.release = self.known["release"]
        self.events = queue.Queue()
        self.photos = []
        self.building = False
        self.asking = False         # a Linux desktop dialog is up
        self.out = default_out(self.release)
        c = COLOURS
        root = self.root
        root.title(f"Tomb Raider Saturn - {self.release} - disc builder")
        root.configure(bg=c["bg"])
        root.resizable(False, False)
        import art
        root.iconphoto(True, self.photo(art.icon(64)), self.photo(art.icon(32)))

        ttk.Style().theme_use("clam")
        ttk.Style().configure("Gold.Horizontal.TProgressbar", troughcolor=c["panel"], background=c["gold"],
                              bordercolor=c["line"], lightcolor=c["gold_hi"], darkcolor=c["gold"], thickness=10)

        head = tk.Canvas(root, width=WIDTH, height=170, bg=c["bg"], highlightthickness=0)
        head.pack()
        head.create_image(0, 0, image=self.photo(art.banner(WIDTH, 170)), anchor="nw")
        head.create_text(30, 58, text="TOMB RAIDER", fill=c["text"], font=(TITLE_FONT, 34, "bold"), anchor="w")
        head.create_text(33, 96, text="for the Sega Saturn", fill=c["gold_hi"],
                         font=(TITLE_FONT, 14, "italic"), anchor="w")
        head.create_text(33, 132, text=self.release.upper(), fill=c["muted"], font=(FONT, 9, "bold"), anchor="w")

        body = tk.Frame(root, bg=c["bg"])
        body.pack(fill="both", padx=24, pady=(6, 20))
        tk.Label(body, text="This builds the disc from your own copies of the game. No game data comes with it.",
                 bg=c["bg"], fg=c["muted"], font=(FONT, 10), anchor="w").pack(fill="x", pady=(0, 12))

        self.saturn = Slot(self, body, "Saturn disc", f"{self.known['saturn']['name']}: required",
                           (48, 44, 40), c["gold"], discbuild.check_saturn, False)
        self.saturn.frame.pack(fill="x")
        self.ps1 = Slot(self, body, "PlayStation disc",
                        f"{self.known['ps1']['name']}: optional, for its sound effects",
                        (26, 26, 30), "#9aa0a6", discbuild.check_ps1, True)
        self.ps1.frame.pack(fill="x", pady=(10, 0))

        where = tk.Frame(body, bg=c["bg"])
        where.pack(fill="x", pady=(14, 0))
        tk.Label(where, text="The disc goes to", bg=c["bg"], fg=c["muted"], font=(FONT, 9)).pack(side="left")
        self.where = tk.Label(where, text=short(self.out), bg=c["bg"], fg=c["text"], font=(FONT, 9), anchor="w",
                              width=1)
        self.where.pack(side="left", padx=(6, 0), fill="x", expand=True)
        self.change = self.button(where, "Change" + DOTS, self.pick_out)
        self.change.pack(side="right")

        self.go = tk.Button(body, text="BUILD THE DISC", command=self.start, font=(FONT, 12, "bold"),
                            bg=c["gold"], fg=c["ink"], activebackground=c["gold_hi"], activeforeground=c["ink"],
                            disabledforeground=c["muted"], relief="flat", bd=0, padx=20, pady=10, cursor="hand2")
        self.go.pack(fill="x", pady=(16, 10))
        self.bar = ttk.Progressbar(body, style="Gold.Horizontal.TProgressbar", maximum=1.0)
        self.bar.pack(fill="x")
        line = tk.Frame(body, bg=c["bg"])
        line.pack(fill="x", pady=(8, 0))
        self.state = tk.Label(line, text="Choose your Saturn disc to begin.", bg=c["bg"], fg=c["muted"],
                              font=(FONT, 9), anchor="w", justify="left", wraplength=560)
        self.state.pack(side="left", fill="x", expand=True)
        self.open = self.button(line, "Open the folder", self.open_out)
        self.log = tk.Text(body, height=6, bg=c["panel"], fg=c["muted"], font=(MONO, 8), relief="flat",
                           highlightthickness=1, highlightbackground=c["line"], wrap="none", state="disabled")
        self.log.pack(fill="x", pady=(10, 0))
        self.refresh()
        root.after(100, self.poll)

    def photo(self, image):
        import art
        p = tk.PhotoImage(data=art.tk_data(image))
        self.photos.append(p)
        return p

    def button(self, parent, text, command):
        c = COLOURS
        return tk.Button(parent, text=text, command=command, font=(FONT, 9), bg=c["line"], fg=c["text"],
                         activebackground=c["hover"], activeforeground=c["text"], disabledforeground=c["muted"],
                         relief="flat", bd=0, padx=12, pady=5, cursor="hand2")

    def refresh(self):
        idle = not (self.building or self.asking)
        ready = self.saturn.ok and (self.ps1.path is None or self.ps1.ok) and idle
        c = COLOURS
        self.go.configure(state="normal" if ready else "disabled", bg=c["gold"] if ready else c["line"])
        for w in (self.saturn, self.ps1):
            w.enable(idle)
        self.change.configure(state="normal" if idle else "disabled")
        if self.building:
            return
        if not self.saturn.ok:
            text = "Choose your Saturn disc to begin."
        elif self.ps1.path and not self.ps1.ok:
            text = "Choose another PlayStation rip, or leave it out."
        else:
            text = "Ready: " + ("with the PlayStation's sound effects." if self.ps1.ok
                                else "with the Saturn's own sound effects.")
        self.state.configure(text=text, fg=COLOURS["muted"])

    def ask(self, then, title, folder=False, start=None):
        """A rip or a folder from the system's dialog: then(path) once one is chosen. On Linux the dialog runs on
        a thread (the portal's answer is a D-Bus signal, zenity's its exit), so the window keeps drawing."""
        if sys.platform == "win32":
            path = (filedialog.askdirectory(parent=self.root, title=title) if folder else
                    filedialog.askopenfilename(parent=self.root, title=title,
                                               filetypes=RIPS + [("All files", "*")]))
            if path:
                then(path)
            return
        import dialogs
        self.asking = True
        self.refresh()
        parent = "x11:" + format(int(self.root.wm_frame(), 16), "x")

        def work():
            try:
                self.events.put((self._asked, then, dialogs.choose(title, folder, () if folder else RIPS, start,
                                                                   parent), None))
            except dialogs.NoDialog:
                self.events.put((self._asked, then, None, "none"))
            except Exception:       # noqa: BLE001
                self.events.put((self._asked, then, None, traceback.format_exc().strip().splitlines()[-1]))
        threading.Thread(target=work, daemon=True).start()

    def _asked(self, then, path, why):
        self.asking = False
        self.refresh()
        if why == "none":
            self.state.configure(text="Your desktop has no file dialog for the builder to use: build from a "
                                      "terminal instead, as below.", fg=COLOURS["bad"])
            self.write_log("No file dialog: the builder uses your desktop's own (its portal, zenity or kdialog).")
            self.write_log("Build from a terminal in the builder's folder (--ps1 is optional; README.txt has more):")
            self.write_log(f'  {terminal_command()} --saturn "SATURN.cue" --ps1 "PS1.cue" --out "FOLDER"')
        elif why:
            self.state.configure(text=f"The file dialog failed: {why}", fg=COLOURS["bad"])
        elif path:
            then(path)

    def pick_out(self):
        self.ask(self.picked_out, "Where should the disc go?", folder=True, start=os.path.dirname(self.out))

    def picked_out(self, folder):
        self.out = os.path.join(folder, os.path.basename(default_out(self.release)))
        self.where.configure(text=short(self.out))

    def open_out(self):
        try:
            open_folder(self.out)
        except OSError:
            self.state.configure(text=f"Your disc is in {self.out}", fg=COLOURS["ok"])

    def write_log(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def start(self):
        self.building = True
        self.open.pack_forget()
        self.bar.configure(value=0)
        self.refresh()
        self.state.configure(text="Building" + DOTS + " (a few minutes)", fg=COLOURS["text"])
        threading.Thread(target=self._build, args=(self.saturn.path, self.ps1.path if self.ps1.ok else None,
                                                   self.out), daemon=True).start()

    def _build(self, saturn, ps1, out):
        log = None
        try:
            os.makedirs(out, exist_ok=True)
            log = open(os.path.join(out, "builder.log"), "w", encoding="utf-8")
            log.write(f"{self.release} ({self.known['commit']})\nSaturn: {saturn}\nPlayStation: {ps1}\n")

            def detail(text):
                log.write(text + "\n")
                log.flush()

            def say(text):
                detail(text)
                self.events.put((self.write_log, text))

            cue = discbuild.build(saturn, ps1, out, say, lambda d, t: self.events.put((self.progress, d / t)), detail)
            self.events.put((self.finished, cue, None))
        except discbuild.Failed as e:
            self.events.put((self.finished, None, str(e)))
        except Exception:       # noqa: BLE001
            text = traceback.format_exc()
            if log:
                log.write(text)
            self.events.put((self.finished, None, "An error stopped it (builder.log has the details):\n"
                             + text.strip().splitlines()[-1]))
        finally:
            if log:
                log.close()

    def progress(self, fraction):
        self.bar.configure(value=fraction)

    def finished(self, cue, why):
        self.building = False
        self.refresh()
        if cue:
            self.state.configure(text=f"Done. Your disc is {cue}", fg=COLOURS["ok"])
            self.open.pack(side="right")
        else:
            self.state.configure(text=f"Stopped: {why}", fg=COLOURS["bad"])

    def poll(self):
        try:
            while True:
                event = self.events.get_nowait()
                event[0](*event[1:])
        except queue.Empty:
            pass
        self.root.after(100, self.poll)


def main():
    multiprocessing.freeze_support()
    global discbuild, tk, ttk, filedialog
    import discbuild
    if {"--saturn", "--help", "-h"} & set(sys.argv[1:]):
        return discbuild.main()
    import tkinter as tk
    from tkinter import filedialog, ttk
    App().root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
