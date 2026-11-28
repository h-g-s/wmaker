import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time


def wait_for(predicate):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError("timed out waiting for dock launcher")


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    if any(shutil.which(tool) is None for tool in ("Xvfb", "xdotool", "xmessage", "xclip", "xprop", "bash")):
        print("SKIP: dock launcher test needs Xvfb, xdotool, xmessage, xclip, xprop and bash")
        return 77
    wmaker = Path(sys.argv[1]).resolve()
    generator = wmaker.parent.parent / "util" / "wmmenugen"
    server = manager = client = selection = None
    with tempfile.TemporaryDirectory(prefix="wmaker-desktop-dock-") as directory:
        root = Path(directory)
        defaults = root / "Defaults"
        defaults.mkdir()
        (defaults / "WindowMaker").write_text("{ IconSize = 64; NoAnimations = YES; }")
        applications = root / "data" / "applications"
        applications.mkdir(parents=True)
        app_id = "test'desk%app"
        output = root / "launched.json"
        recorder = root / "recorder.py"
        recorder.write_text(f"import json, sys\nopen({str(output)!r}, 'w').write(json.dumps(sys.argv[1:]))\n")
        bindir = root / "bin"
        bindir.mkdir()
        wrapper = bindir / "wmmenugen"
        wrapper.write_text("#!/usr/bin/env python3\nimport json, os, sys\n"
                           f"open({str(root / 'launcher-args.json')!r}, 'w').write(json.dumps(sys.argv[1:]))\n"
                           f"os.execv({str(generator)!r}, [{str(generator)!r}] + sys.argv[1:])\n")
        wrapper.chmod(0o755)
        (applications / f"{app_id}.desktop").write_text(
            "[Desktop Entry]\nType=Application\nName=Dock Test\n"
            f'Exec={sys.executable} "{recorder}" %F\n'
        )
        base_env = dict(os.environ, HOME=directory, WMAKER_USER_ROOT=directory,
                        XDG_DATA_HOME=str(root / "data"), XDG_DATA_DIRS=str(root / "empty"),
                        XDG_CACHE_HOME=str(root / "cache"),
                        PATH=f"{bindir}:{generator.parent}:{os.environ.get('PATH', '/usr/bin:/bin')}")
        probe = subprocess.run([str(generator), "--applications"], env=base_env, capture_output=True, text=True)
        if probe.returncode and "built without GIO" in probe.stderr:
            print("SKIP: desktop-entry support is disabled")
            return 77
        assert probe.returncode == 0, probe.stderr
        try:
            server = subprocess.Popen(
                ["Xvfb", "-displayfd", "1", "-screen", "0", "800x600x24", "-nolisten", "tcp"],
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
            assert select.select([server.stdout], [], [], 10)[0], "Xvfb did not start"
            display = server.stdout.readline().strip()
            assert display.isdigit(), "Xvfb did not allocate a display"
            env = dict(base_env, DISPLAY=f":{display}")

            def run(*args):
                return subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      text=True, timeout=10)

            def app_icon_visible():
                ids = run("xdotool", "search", "--onlyvisible", "--name", "").stdout.splitlines()
                return any("X=0\nY=536\nWIDTH=64\nHEIGHT=64\n" in
                           run("xdotool", "getwindowgeometry", "--shell", window).stdout for window in ids)

            def dialog_geometry():
                ids = run("xdotool", "search", "--onlyvisible", "--name", "").stdout.splitlines()
                for window in ids:
                    geometry = dict(line.split("=", 1) for line in
                                    run("xdotool", "getwindowgeometry", "--shell", window).stdout.splitlines())
                    width, height = int(geometry.get("WIDTH", 0)), int(geometry.get("HEIGHT", 0))
                    if 250 <= width < 600 and 100 <= height < 400:
                        return int(geometry["X"]), int(geometry["Y"]), width, height
                return None

            with (root / "wmaker.log").open("w+") as log:
                manager = subprocess.Popen(
                    [str(wmaker), "--for-real", "--no-autolaunch", "--dont-restore", "--no-clip", "--no-drawer"],
                    env=env, stdout=log, stderr=log)
                wait_for(lambda: "window id" in run("xprop", "-root", "_NET_SUPPORTING_WM_CHECK").stdout)
                client = subprocess.Popen(
                    ["bash", "-c", 'exec -a "$2" "$1" -name "$2" hello', "client",
                     shutil.which("xmessage"), app_id], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                wait_for(lambda: run("xdotool", "search", "--onlyvisible", "--name", app_id).returncode == 0)
                wait_for(app_icon_visible)
                drag = run("xdotool", "mousemove", "32", "568", "mousedown", "1", "sleep", "0.3",
                           "mousemove", "768", "96", "sleep", "0.5", "mouseup", "1")
                assert drag.returncode == 0, drag.stderr
                time.sleep(0.5)
                assert run("xdotool", "search", "--onlyvisible", "--name", "Dock Icon").returncode != 0
                stop(client)
                client = None
                time.sleep(0.5)
                selected = root / 'a "quoted" \\ path.txt'
                selection = subprocess.Popen(["xclip", "-quiet", "-selection", "primary", "-target", "STRING"],
                                             env=env, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                             stderr=subprocess.DEVNULL)
                selection.stdin.write(str(selected).encode())
                selection.stdin.close()
                time.sleep(0.2)
                assert run("xclip", "-o", "-selection", "primary").stdout == str(selected)
                click = run("xdotool", "mousemove", "768", "96", "click", "2")
                assert click.returncode == 0, click.stderr
                wait_for(output.exists)
                arguments = json.loads(output.read_text())
                log.flush()
                assert arguments == [str(selected)], (arguments, str(selected), (root / "launcher-args.json").read_text(),
                                                       (root / "wmaker.log").read_text())
                output.unlink()
                time.sleep(0.5)
                click = run("xdotool", "click", "--repeat", "2", "--delay", "150", "1")
                assert click.returncode == 0, click.stderr
                wait_for(output.exists)
                assert json.loads(output.read_text()) == []

                client = subprocess.Popen([shutil.which("xmessage"), "-name", "native-test", "hello"],
                                          env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                wait_for(lambda: run("xdotool", "search", "--onlyvisible", "--name", "native-test").returncode == 0)
                wait_for(app_icon_visible)
                drag = run("xdotool", "mousemove", "32", "568", "mousedown", "1", "sleep", "0.3",
                           "mousemove", "768", "160", "sleep", "0.5", "mouseup", "1")
                assert drag.returncode == 0, drag.stderr
                time.sleep(0.3)
                stop(client)
                wait_for(lambda: not app_icon_visible())
                client = subprocess.Popen(
                    ["bash", "-c", 'exec -a missing-test "$1" -name missing-test hello', "client",
                     shutil.which("xmessage")], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                wait_for(lambda: run("xdotool", "search", "--onlyvisible", "--name", "missing-test").returncode == 0)
                wait_for(app_icon_visible)
                drag = run("xdotool", "mousemove", "32", "568", "mousedown", "1", "sleep", "0.3",
                           "mousemove", "768", "224", "sleep", "0.5", "mouseup", "1")
                assert drag.returncode == 0, drag.stderr
                wait_for(dialog_geometry)
                x, y, width, height = dialog_geometry()
                run("xdotool", "mousemove", str(x + width - 110), str(y + height - 30), "click", "1")
                wait_for(lambda: not dialog_geometry())
                stop(client)
                client = None
                stop(manager)
                manager = None
                state = (defaults / "WMState").read_text()
                assert "wmmenugen --launch" in state and "desk" in state
                assert "-- %S" in state and "-- %d" in state
                assert "xmessage -name native-test hello" in state
                assert "missing-test.Xmessage" not in state
        finally:
            stop(selection)
            stop(client)
            stop(manager)
            stop(server)
    print("PASS: dock desktop matching, launching and selection")
    return 0


if __name__ == "__main__":
    sys.exit(main())
