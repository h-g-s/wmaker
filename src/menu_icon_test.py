import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time


def wait_for(check):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.05)
    raise AssertionError("Timed out waiting for isolated menu test")


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def xpm(path, width, height, color):
    rows = ",\n".join('"' + "." * width + '"' for _ in range(height))
    path.write_text(
        '/* XPM */\nstatic char *image[] = {\n'
        f'"{width} {height} 1 1",\n". c {color}",\n{rows}\n}};\n'
    )


def main():
    tools = ("Xvfb", "xdotool", "import", "xprop")
    if any(shutil.which(tool) is None for tool in tools):
        print("SKIP: menu icon rendering needs Xvfb, xdotool, import and xprop")
        return 77
    wmaker = str(Path(sys.argv[1]).resolve())
    server = manager = None
    try:
        with tempfile.TemporaryDirectory(prefix="wmaker-menu-icons-") as directory:
            root = Path(directory)
            defaults = root / "Defaults"
            defaults.mkdir()
            xpm(root / "square.xpm", 64, 64, "#ff0000")
            xpm(root / "tall.xpm", 1, 256, "#00ff00")
            xpm(root / "wide.xpm", 256, 1, "#0000ff")
            (root / "broken.xpm").write_text("not an image\n")
            (defaults / "WindowMaker").write_text(
                '{ WindowListAppIcons = NO; MenuTextFont = "Sans:pixelsize=12";'
                f' IconPath = ("{root}"); WorkspaceBack = (solid, "#333333"); }}'
            )
            command = f"touch {root}/menu-launched"
            with_icon = (
                '(Icons, (Run, ICON, square.xpm, SHORTCUT, "Control+F8", SHEXEC,'
                f' "{command}"),'
                '(Tall, ICON, "~/tall.xpm", REFRESH),'
                f'(Wide, ICON, "{root}/wide.xpm", REFRESH),'
                '(Missing, ICON, absent.xpm, REFRESH),'
                '(Broken, ICON, broken.xpm, REFRESH),'
                '(Malformed, ICON, ()), (Legacy, REFRESH))'
            )
            menu_file = defaults / "WMRootMenu"
            menu_file.write_text(with_icon)
            with (root / "xvfb.log").open("w") as server_log, (root / "wmaker.log").open("w+") as manager_log:
                server = subprocess.Popen(
                    ["Xvfb", "-displayfd", "1", "-screen", "0", "800x600x24", "-nolisten", "tcp"],
                    stdout=subprocess.PIPE, stderr=server_log, text=True,
                )
                assert select.select([server.stdout], [], [], 10)[0], "Xvfb did not start"
                display = server.stdout.readline().strip()
                assert display.isdigit(), "Xvfb failed to allocate a display"
                env = dict(os.environ, DISPLAY=f":{display}", HOME=directory, WMAKER_USER_ROOT=directory)

                def run(*args):
                    return subprocess.check_output(args, env=env, stderr=subprocess.STDOUT, timeout=10)

                run("xdotool", "getdisplaygeometry")
                manager = subprocess.Popen(
                    [wmaker, "--for-real", "--no-autolaunch", "--dont-restore",
                     "--no-dock", "--no-clip", "--no-drawer"],
                    env=env, stdout=manager_log, stderr=manager_log,
                )
                wait_for(lambda: b"window id" in run("xprop", "-root", "_NET_SUPPORTING_WM_CHECK"))
                assert manager.poll() is None

                def pixels():
                    raw = run("import", "-window", "root", "-depth", "8", "rgb:-")
                    assert len(raw) == 800 * 600 * 3
                    result = {}
                    for color in (b"\xff\x00\x00", b"\x00\xff\x00", b"\x00\x00\xff"):
                        positions = [i // 3 for i in range(0, len(raw), 3) if raw[i:i + 3] == color]
                        if positions:
                            xs = [p % 800 for p in positions]
                            ys = [p // 800 for p in positions]
                            result[color] = (min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)
                    return result

                run("xdotool", "mousemove", "100", "100", "click", "3")
                wait_for(lambda: len(pixels()) == 3)
                sizes = pixels()
                x, y, size, height = sizes[b"\xff\x00\x00"]
                assert 12 <= size < 32 and size == height, sizes
                assert sizes[b"\x00\xff\x00"][2:] == (1, size), sizes
                assert sizes[b"\x00\x00\xff"][2:] == (size, 1), sizes
                run("xdotool", "mousemove", str(x + size // 2), str(y + size // 2), "click", "1")
                wait_for(lambda: (root / "menu-launched").exists())

                menu_file.write_text(with_icon.replace("ICON, square.xpm, ", ""))
                run("xdotool", "mousemove", "110", "110")
                time.sleep(0.2)
                run("xdotool", "click", "3")
                wait_for(lambda: len(pixels()) == 2)
                assert b"\xff\x00\x00" not in pixels()
                assert manager.poll() is None
                stop(manager)
                manager_log.seek(0)
                log = manager_log.read()
                assert 'could not find menu icon "absent.xpm"' in log, log
                assert 'could not load menu icon "broken.xpm"' in log, log
                assert "format error in root menu configuration" in log, log
                stop(server)
    finally:
        stop(manager)
        stop(server)
    print("PASS: desktop menu icons, scaling, command execution, removal and invalid-image warnings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
