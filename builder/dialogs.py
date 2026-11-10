"""The desktop's own dialogs for choosing a rip or a folder, on Linux: the XDG desktop portal over D-Bus (GNOME, KDE
Plasma, SteamOS's desktop and most others), else zenity or kdialog. With none of them, choose() raises NoDialog and
the window points the player to the terminal build. On Windows the builder uses Tk's dialogs, which are Windows' own.
"""
import itertools
import os
import shutil
import subprocess
import urllib.parse


class NoDialog(Exception):
    """The desktop has no dialog the builder can show."""


class _Unavailable(Exception):
    """This way of showing a dialog isn't there: try the next."""


_tokens = itertools.count()


def system_env():
    """The environment for the system's own programs. PyInstaller points the library path at the program's own
    libraries, and a system program loading those may not start."""
    env = dict(os.environ)
    orig = env.pop("LD_LIBRARY_PATH_ORIG", None)
    if orig is None:
        env.pop("LD_LIBRARY_PATH", None)
    else:
        env["LD_LIBRARY_PATH"] = orig
    return env


def choose(title, folder=False, filters=(), start=None, parent=""):
    """The path the player chose, or None if they cancelled. filters: (name, globs) pairs, for a file; start: the
    folder to open in; parent: the window's handle for the portal ("x11:<XID in hex>")."""
    for way in (lambda: _portal(title, folder, filters, start, parent),
                lambda: _zenity(title, folder, filters, start),
                lambda: _kdialog(title, folder, filters, start)):
        try:
            return way()
        except _Unavailable:
            pass
    raise NoDialog


def _portal(title, folder, filters, start, parent):
    """org.freedesktop.portal.FileChooser.OpenFile, and the Response signal it ends with."""
    try:
        from jeepney import DBusAddress, HeaderFields, MatchRule, MessageType, message_bus, new_method_call
        from jeepney.io.blocking import Proxy, open_dbus_connection
        address = os.environ.get("DBUS_SESSION_BUS_ADDRESS") or \
            "unix:path=" + os.path.join(os.environ["XDG_RUNTIME_DIR"], "bus")
        conn = open_dbus_connection(address)
    except Exception as e:      # noqa: BLE001 - no jeepney, no session bus: no portal
        raise _Unavailable from e
    with conn:
        desktop = dict(object_path="/org/freedesktop/portal/desktop", bus_name="org.freedesktop.portal.Desktop")
        chooser = DBusAddress(interface="org.freedesktop.portal.FileChooser", **desktop)
        props = DBusAddress(interface="org.freedesktop.DBus.Properties", **desktop)
        version = conn.send_and_get_reply(new_method_call(props, "Get", "ss", (chooser.interface, "version")),
                                          timeout=30)
        if version.header.message_type == MessageType.error:
            raise _Unavailable(version.body)      # no portal, or none that chooses files
        if folder and version.body[0][1] < 3:
            raise _Unavailable("this portal can't choose folders")      # FileChooser 3 added them

        # Every Response this connection is sent, caught from before the call, so the answer can't be missed;
        # the one for this request is the one on the path the call returns.
        rule = MatchRule(type="signal", interface="org.freedesktop.portal.Request", member="Response")
        Proxy(message_bus, conn).AddMatch(rule)
        with conn.filter(rule, bufsize=16) as answers:
            options = {"handle_token": ("s", f"trs{os.getpid()}_{next(_tokens)}"), "modal": ("b", True)}
            if folder:
                options["directory"] = ("b", True)
            if filters:
                options["filters"] = ("a(sa(us))", [(name, [(0, g) for g in globs])
                                                    for name, globs in list(filters) + [("All files", ("*",))]])
            if start:
                options["current_folder"] = ("ay", os.fsencode(start) + b"\0")
            reply = conn.send_and_get_reply(new_method_call(chooser, "OpenFile", "ssa{sv}",
                                                            (parent, title, options)), timeout=30)
            if reply.header.message_type == MessageType.error:
                raise _Unavailable(reply.body)
            request = reply.body[0]
            while True:
                answer = conn.recv_until_filtered(answers)
                if answer.header.fields.get(HeaderFields.path) == request:
                    break
    code, results = answer.body
    uris = results.get("uris", ("as", []))[1]
    if code != 0 or not uris:
        return None     # 1: cancelled; 2: ended some other way
    return os.fsdecode(urllib.parse.unquote_to_bytes(urllib.parse.urlparse(uris[0]).path))


def _run(args):
    """A dialog program's choice: its path, None if cancelled."""
    if not shutil.which(args[0]):
        raise _Unavailable
    r = subprocess.run(args, env=system_env(), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL)
    if r.returncode == 0:
        return os.fsdecode(r.stdout).rstrip("\n") or None
    if r.returncode == 1:
        return None
    raise _Unavailable(f"{args[0]} exited {r.returncode}")      # no display, or it failed


def _zenity(title, folder, filters, start):
    args = ["zenity", "--file-selection", f"--title={title}"]
    if folder:
        args.append("--directory")
    if filters:
        args += [f"--file-filter={name} | {' '.join(globs)}" for name, globs in filters]
        args.append("--file-filter=All files | *")
    if start:
        args.append("--filename=" + os.path.join(start, ""))
    return _run(args)


def _kdialog(title, folder, filters, start):
    start = start or os.path.expanduser("~")
    if folder:
        return _run(["kdialog", "--title", title, "--getexistingdirectory", start])
    kinds = "\n".join([f"{' '.join(globs)}|{name}" for name, globs in filters] + ["*|All files"])
    return _run(["kdialog", "--title", title, "--getopenfilename", start, kinds])
