import ctypes
import ctypes.util
import os
from pathlib import Path
import select
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


def wait_for(check, detail):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.05)
    raise AssertionError(f"Timed out: {detail()}")


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    if any(shutil.which(tool) is None for tool in ("Xvfb", "xdotool", "xprop", "xterm")):
        print("SKIP: command shortcuts need Xvfb, xdotool, xprop and xterm")
        return 77
    wmaker = str(Path(sys.argv[1]).resolve())
    server = manager = client = None
    temp = tempfile.TemporaryDirectory(prefix="wmaker-command-shortcuts-")
    try:
        directory = temp.name
        root = Path(directory)
        defaults = root / "Defaults"
        defaults.mkdir()
        config = defaults / "WindowMaker"
        output = root / "output"
        log = root / "wmaker.log"
        timestamp = int(time.time()) - 1
        output_path = shlex.quote(str(output))

        def write_config(bindings):
            nonlocal timestamp
            timestamp += 1
            def plstring(value):
                return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'

            pairs = ", ".join(f"({plstring(key)}, {plstring(command)})" for key, command in bindings)
            config.write_text('{ CommandShortcuts = (' + pairs + '); }')
            os.utime(config, (timestamp, timestamp))

        write_config([("Control+F8", f"echo first >> {output_path}"),
                      ("Control+X C", f"echo chain >> {output_path}")])
        with (root / "xvfb.log").open("w") as server_log, log.open("w") as manager_log:
            server = subprocess.Popen(
                ["Xvfb", "-displayfd", "1", "-screen", "0", "800x600x24", "-nolisten", "tcp"],
                stdout=subprocess.PIPE, stderr=server_log, text=True,
            )
            assert select.select([server.stdout], [], [], 10)[0], "Xvfb did not start"
            display = server.stdout.readline().strip()
            assert display.isdigit(), "Xvfb failed to allocate a display"
            env = dict(os.environ, HOME=directory, WMAKER_USER_ROOT=directory, DISPLAY=f":{display}",
                       XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                       XDG_CACHE_HOME=str(root / "cache"))

            def run(*args):
                return subprocess.check_output(args, env=env, stderr=subprocess.STDOUT, timeout=10)

            def lines():
                return output.read_text().splitlines() if output.exists() else []

            def detail():
                return f"output={lines()}\n{log.read_text()}"

            xlib = ctypes.CDLL(ctypes.util.find_library("X11"))
            xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
            xlib.XOpenDisplay.restype = ctypes.c_void_p
            xlib.XStringToKeysym.argtypes = [ctypes.c_char_p]
            xlib.XStringToKeysym.restype = ctypes.c_ulong
            xlib.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
            xlib.XKeysymToKeycode.restype = ctypes.c_ubyte
            xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
            xdisplay = xlib.XOpenDisplay(env["DISPLAY"].encode())
            assert xdisplay
            f8 = str(xlib.XKeysymToKeycode(xdisplay, xlib.XStringToKeysym(b"F8")))
            f9 = str(xlib.XKeysymToKeycode(xdisplay, xlib.XStringToKeysym(b"F9")))
            xlib.XCloseDisplay(xdisplay)

            def control_key(key):
                run("xdotool", "keydown", "ctrl", "key", key, "keyup", "ctrl")

            manager = subprocess.Popen(
                [wmaker, "--for-real", "--no-autolaunch", "--dont-restore",
                 "--no-dock", "--no-clip", "--no-drawer"],
                env=env, stdout=manager_log, stderr=manager_log,
            )
            wait_for(lambda: b"window id" in run("xprop", "-root", "_NET_SUPPORTING_WM_CHECK"), detail)
            time.sleep(0.5)
            control_key(f8)
            wait_for(lambda: lines() == ["first"], detail)
            client = subprocess.Popen(["xterm", "-title", "shortcut-test", "-e", "/bin/sleep", "60"], env=env,
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            wait_for(lambda: subprocess.run(["xdotool", "search", "--name", "shortcut-test"],
                                            env=env, stdout=subprocess.DEVNULL).returncode == 0, detail)
            window = run("xdotool", "search", "--name", "shortcut-test").splitlines()[0].decode()
            wait_for(lambda: hex(int(window)).encode() in run("xprop", "-root", "_NET_CLIENT_LIST").lower(), detail)
            run("xdotool", "windowactivate", "--sync", window)
            control_key(f8)
            wait_for(lambda: lines() == ["first", "first"], detail)
            run("xdotool", "key", "ctrl+x", "c")
            wait_for(lambda: lines() == ["first", "first", "chain"], detail)

            run("xdotool", "key", "ctrl+x")
            write_config([("Control+F9", f"echo second%literal >> {output_path}")])
            run("xdotool", "mousemove", "120", "120")

            def try_new_shortcut():
                control_key(f9)
                time.sleep(0.15)
                return "second%literal" in lines()

            wait_for(try_new_shortcut, detail)
            assert lines() == ["first", "first", "chain", "second%literal"], detail()
            control_key(f8)
            time.sleep(0.3)
            assert lines() == ["first", "first", "chain", "second%literal"], detail()
            assert manager.poll() is None, detail()
    finally:
        stop(client)
        stop(manager)
        stop(server)
        temp.cleanup()
    print("Command shortcut integration tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
