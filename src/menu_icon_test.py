import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time


def wait_for(check, diagnostic=None):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.05)
    detail = diagnostic() if diagnostic else ""
    raise AssertionError(f"Timed out waiting for isolated menu test: {detail}")


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
            data = root / "data"
            applications = data / "applications"
            applications.mkdir(parents=True)
            desktop = (
                "[Desktop Entry]\nType=Application\nName=Network Test\n"
                f"Exec={shutil.which('touch')} {root}/application-launched\n"
                f"Icon={root}/square.xpm\nCategories=Network;\n"
            )
            (applications / "net'work%test.desktop").write_text(desktop)
            generator = Path(wmaker).parent.parent / "util" / "wmmenugen"
            assert generator.exists(), "Build wmmenugen before running the menu icon test"
            generator_env = dict(os.environ, HOME=directory, XDG_DATA_HOME=str(data),
                                 XDG_DATA_DIRS=str(root / "empty"), XDG_CONFIG_HOME=str(root / "config"),
                                 XDG_CURRENT_DESKTOP="WindowMaker")
            probe = subprocess.run([str(generator), "--applications"], env=generator_env,
                                   capture_output=True, text=True, timeout=10)
            supports_applications = probe.returncode == 0
            assert supports_applications or "built without GIO" in probe.stderr, probe.stderr
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
                '(Malformed, ICON, ()), (Legacy, REFRESH)'
                + (', ("Auto Apps", APPLICATIONS_MENU, Network)' if supports_applications else '') + ')'
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
                env = dict(generator_env, DISPLAY=f":{display}", WMAKER_USER_ROOT=directory,
                           PATH=f"{generator.parent}:{os.environ.get('PATH', '/usr/bin:/bin')}")

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
                wait_for(lambda: len(pixels()) == 3,
                         lambda: f"{pixels()}\n{(root / 'wmaker.log').read_text()}")
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
                if supports_applications:
                    def open_applications():
                        run("xdotool", "mousemove", "600", "500", "click", "1")
                        run("xdotool", "mousemove", "110", "110", "click", "3")
                        colors = pixels()
                        green = colors[b"\x00\xff\x00"]
                        blue = colors[b"\x00\x00\xff"]
                        row_height = blue[1] - green[1] - (size - 1) // 2
                        run("xdotool", "mousemove", str(green[0] + 40),
                            str(green[1] + size // 2 + 5 * row_height), "click", "1")

                    open_applications()
                    wait_for(lambda: b"\xff\x00\x00" in pixels(),
                             lambda: f"{pixels()}\n{(root / 'wmaker.log').read_text()}")
                    red = pixels()[b"\xff\x00\x00"]
                    run("xdotool", "mousemove", str(red[0] + size // 2),
                        str(red[1] + size // 2), "click", "1")
                    wait_for(lambda: (root / "application-launched").exists(),
                             lambda: (root / "wmaker.log").read_text())
                    (applications / "second.desktop").write_text(desktop.replace("Network Test", "Second Network"))
                    open_applications()
                    wait_for(lambda: pixels().get(b"\xff\x00\x00", (0, 0, 0, 0))[3] > size)
                    (applications / "net'work%test.desktop").unlink()
                    (applications / "second.desktop").unlink()
                    open_applications()
                    wait_for(lambda: b"\xff\x00\x00" not in pixels())
                    time.sleep(0.2)
                    assert manager.poll() is None, (root / "wmaker.log").read_text()
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
    print("PASS: desktop menu icons, scaling, execution, Applications generation/refresh and invalid-image warnings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
