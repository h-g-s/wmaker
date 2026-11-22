import ast
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def plist(text):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[(),]|[^\s(),]+', text)
    position = 0

    def value():
        nonlocal position
        token = tokens[position]
        position += 1
        if token == "(":
            result = []
            while tokens[position] != ")":
                if tokens[position] == ",":
                    position += 1
                else:
                    result.append(value())
            position += 1
            return result
        return ast.literal_eval(token) if token.startswith('"') else token

    result = value()
    assert position == len(tokens)
    return result


def entries(menu):
    result = {}
    for item in menu[1:]:
        if len(item) > 1 and isinstance(item[1], list):
            result.update(entries(item))
        else:
            result[item[0]] = item
    return result


def desktop(path, name, extra="", command="/bin/true"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"[Desktop Entry]\nType=Application\nName={name}\nExec={command}\n{extra}")


def theme(base, name, inherits=""):
    path = base / "icons" / name
    path.mkdir(parents=True, exist_ok=True)
    (path / "index.theme").write_text(
        f"[Icon Theme]\nName={name}\nDirectories=16x16/apps,32x32/apps\nInherits={inherits}\n"
        "[16x16/apps]\nSize=16\nType=Fixed\nContext=Applications\n"
        "[32x32/apps]\nSize=32\nType=Fixed\nContext=Applications\n"
    )
    return path


def icon(base, name, size=16):
    path = base / f"{size}x{size}" / "apps" / f"{name}.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"icon lookup fixture")
    return str(path)


def test_cache(generator, root, original_env):
    root = root / "cache-tests"
    user, system, home, config = (root / name for name in ("user", "system", "home", "config"))
    binary = root / "bin"
    binary.mkdir(parents=True)
    home.mkdir()
    env = dict(original_env, HOME=str(home), XDG_DATA_HOME=str(user), XDG_DATA_DIRS=str(system),
               XDG_CONFIG_HOME=str(config), XDG_CACHE_HOME=str(root / "cache"), LANGUAGE="",
               PATH=f"{binary}:{original_env['PATH']}")
    selected = theme(user, "Cached")
    gtk = config / "gtk-4.0" / "settings.ini"
    gtk.parent.mkdir(parents=True)
    gtk.write_text("[Settings]\ngtk-icon-theme-name=Cached\n")
    application = user / "applications" / "cached.desktop"
    extra = "Categories=Network;\nIcon=late-icon\n"
    desktop(application, "Original App", extra)

    def generate(*args, warning=None):
        result = subprocess.run([generator, "--applications", "--icon-size", "16", *args],
                                env=env, capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, result.stderr
        if warning:
            assert warning in result.stderr, result.stderr
        else:
            assert not result.stderr, result.stderr
        return result.stdout

    probe = subprocess.run([generator, "--applications", "--icon-size", "16"], env=env,
                           capture_output=True, text=True, timeout=15)
    assert probe.returncode == 0, probe.stderr
    if "menu caching is disabled" in probe.stderr:
        print("SKIP: persistent menu caching needs high-resolution filesystem timestamps")
        return
    assert not probe.stderr, probe.stderr
    output = probe.stdout
    cache, = (root / "cache" / "WindowMaker" / "menus").glob("applications-*.cache")

    def assert_hit():
        before = cache.stat()
        data = cache.read_bytes()
        result = generate()
        after = cache.stat()
        assert (before.st_ino, before.st_mtime_ns, data) == (after.st_ino, after.st_mtime_ns, cache.read_bytes())
        return result

    assert assert_hit() == output
    assert generate("--no-cache") == output
    cache.write_text("broken cache\n")
    assert generate("--no-cache") == output
    assert cache.read_text() == "broken cache\n"
    assert generate(warning="menu cache") == output
    assert assert_hit() == output

    before = application.stat()
    desktop(application, "Modified App", extra)
    os.utime(application, ns=(before.st_atime_ns, before.st_mtime_ns))
    assert application.stat().st_size == before.st_size
    assert entries(plist(generate())).keys() == {"Modified App"}
    assert_hit()

    nested = system / "applications" / "nested" / "tool.desktop"
    desktop(nested, "Nested App")
    assert "Nested App" in entries(plist(generate()))
    assert_hit()
    desktop(nested, "Changed Nested App")
    assert "Changed Nested App" in entries(plist(generate()))
    nested.unlink()
    assert "Changed Nested App" not in entries(plist(generate()))

    themed_icon = Path(icon(selected, "late-icon"))
    assert entries(plist(generate()))["Modified App"][1:3] == ["ICON", str(themed_icon)]
    assert_hit()
    themed_icon.unlink()
    assert entries(plist(generate()))["Modified App"][1] == "EXEC"
    target = root / "target.png"
    target.write_bytes(b"symlink target")
    themed_icon.symlink_to(target)
    assert entries(plist(generate()))["Modified App"][2] == str(themed_icon)
    assert_hit()
    target.unlink()
    assert entries(plist(generate()))["Modified App"][1] == "EXEC"

    absolute = root / "new-absolute.png"
    desktop(user / "applications" / "absolute.desktop", "Absolute", f"Icon={absolute}\n")
    assert entries(plist(generate()))["Absolute"][1] == "EXEC"
    assert_hit()
    absolute.write_bytes(b"absolute icon")
    assert entries(plist(generate()))["Absolute"][1:3] == ["ICON", str(absolute)]

    parent = theme(system, "Parent")
    parent_icon = icon(parent, "late-icon")
    assert entries(plist(generate()))["Modified App"][1] == "EXEC"
    theme(user, "Cached", "Parent")
    assert entries(plist(generate()))["Modified App"][1:3] == ["ICON", parent_icon]
    assert_hit()

    # A newly created higher-priority theme root must supersede the inherited icon.
    preferred = theme(user, "Parent")
    preferred_icon = icon(preferred, "late-icon")
    assert entries(plist(generate()))["Modified App"][2] == preferred_icon
    assert_hit()

    desktop(user / "applications" / "tryexec.desktop", "TryExec App", "TryExec=wm-cache-tryexec\n")
    desktop(user / "applications" / "exec.desktop", "Exec App", command="wm-cache-exec")
    assert not {"TryExec App", "Exec App"} & entries(plist(generate())).keys()
    assert_hit()
    executable = binary / "wm-cache-tryexec"
    executable.write_text("#!/bin/sh\nexit 0\n")
    executable.chmod(0o755)
    assert "TryExec App" in entries(plist(generate()))
    assert_hit()
    executable.chmod(0o644)
    assert "TryExec App" not in entries(plist(generate()))
    executable.chmod(0o755)
    assert "TryExec App" in entries(plist(generate()))
    executable.unlink()
    assert "TryExec App" not in entries(plist(generate()))
    executable = binary / "wm-cache-exec"
    executable.write_text("#!/bin/sh\nexit 0\n")
    executable.chmod(0o755)
    assert "Exec App" in entries(plist(generate()))
    assert_hit()
    env["PATH"] = original_env["PATH"]
    assert "Exec App" not in entries(plist(generate()))
    env["PATH"] = f"{binary}:{original_env['PATH']}"
    assert "Exec App" in entries(plist(generate()))
    executable.unlink()
    assert "Exec App" not in entries(plist(generate()))

    # Concurrent cold generators must leave one complete cache, then reuse it.
    cache.unlink()
    with ThreadPoolExecutor(max_workers=4) as workers:
        outputs = list(workers.map(lambda _: generate(), range(4)))
    assert len(set(outputs)) == 1
    assert assert_hit() == outputs[0]
    cache.write_text(cache.read_text().replace("Modified App", "Corrupted App"))
    assert "Modified App" in entries(plist(generate(warning="corrupted menu cache")))
    assert_hit()

    alternate = theme(user, "Alternate")
    alternate_icon = icon(alternate, "late-icon")
    gtk.write_text("[Settings]\ngtk-icon-theme-name=Alternate\n")
    assert entries(plist(generate()))["Modified App"][2] == alternate_icon
    assert all(item[1] == "EXEC" for item in entries(plist(generate("--no-icons"))).values())
    desktop(application, "Modified App", extra + "Name[fr]=Application locale\n")
    env["LANGUAGE"] = "fr"
    assert "Application locale" in entries(plist(generate()))
    env["LANGUAGE"] = ""
    assert "Modified App" in entries(plist(generate()))
    desktop(user / "applications" / "desktop.desktop", "Other Desktop", "OnlyShowIn=GNOME;\n")
    assert "Other Desktop" not in entries(plist(generate()))
    env["XDG_CURRENT_DESKTOP"] = "GNOME"
    assert "Other Desktop" in entries(plist(generate()))

    blocked = root / "unusable-cache"
    blocked.write_text("not a directory")
    env["XDG_CACHE_HOME"] = str(blocked)
    assert "Modified App" in entries(plist(generate(warning="menu cache directory")))


def main():
    generator = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="wmmenugen-desktop-") as directory:
        root = Path(directory)
        user, system, second = root / "user", root / "system", root / "second"
        home, config = root / "home", root / "config"
        home.mkdir()
        env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(user),
                   XDG_DATA_DIRS=f"{system}:{second}", XDG_CONFIG_HOME=str(config),
                   XDG_CACHE_HOME=str(root / "cache"), XDG_CURRENT_DESKTOP="WindowMaker",
                   LC_ALL="C.UTF-8", LANGUAGE="fr")

        def run(*args, status=0):
            result = subprocess.run([generator, *args], env=env, capture_output=True, text=True, timeout=15)
            assert result.returncode == status, (args, result.returncode, result.stderr)
            return result.stdout

        probe = subprocess.run([generator, "--applications"], env=env, capture_output=True, text=True)
        if "built without GIO" in probe.stderr:
            print("SKIP: Applications generation needs GIO support")
            return 77
        assert probe.returncode == 0, probe.stderr
        assert entries(plist(probe.stdout)) == {}
        desktop(system / "applications" / "browser.desktop", "System Browser",
                "Categories=Network;WebBrowser;\nIcon=browser\n")
        desktop(user / "applications" / "browser.desktop", "User Browser",
                "Name[fr]=Navigateur\nCategories=Network;WebBrowser;\nIcon=browser\n")
        desktop(system / "applications" / "hidden.desktop", "Masked")
        (user / "applications" / "hidden.desktop").write_text("[Desktop Entry]\nHidden=true\n")
        desktop(system / "applications" / "private.desktop", "NoDisplay", "NoDisplay=true\n")
        desktop(system / "applications" / "gnome.desktop", "GNOME Only", "OnlyShowIn=GNOME;\n")
        desktop(system / "applications" / "excluded.desktop", "Excluded", "NotShowIn=WindowMaker;\n")
        desktop(system / "applications" / "missing.desktop", "Unavailable", "TryExec=wm-test-absent-program\n")
        desktop(system / "applications" / "terminal.desktop", "Terminal",
                "Categories=System;TerminalEmulator;\nTerminal=true\nIcon=terminal\n")
        desktop(system / "applications" / "nested" / "tool.desktop", "Nested Tool",
                "Categories=Development;\nOnlyShowIn=WindowMaker;\n")
        desktop(second / "applications" / "browser.desktop", "Lower Priority")
        selected = theme(user, "Selected", "Parent")
        parent = theme(system, "Parent", "Selected")
        theme(system, "hicolor")
        hicolor = theme(second, "hicolor")
        user_icon = icon(selected, "browser")
        icon(selected, "browser", 32)
        terminal_icon = icon(hicolor, "terminal")
        inherited = icon(parent, "inherited")
        desktop(system / "applications" / "inherited.desktop", "Inherited", "Icon=inherited\n")
        desktop(system / "applications" / "absent.desktop", "No Icon", "Icon=absent\n")
        absolute_icon = root / "absolute.png"
        absolute_icon.write_bytes(b"absolute icon")
        desktop(system / "applications" / "absolute.desktop", "Absolute", f"Icon={absolute_icon}\n")
        gtk4 = config / "gtk-4.0" / "settings.ini"
        gtk4.parent.mkdir(parents=True)
        gtk4.write_text("[Settings]\ngtk-icon-theme-name=Selected\n")
        menu = plist(run("--applications", "--icon-size", "16"))
        visible = entries(menu)
        assert set(visible) == {"Navigateur", "Terminal", "Nested Tool", "Inherited", "No Icon", "Absolute"}, visible
        assert visible["Navigateur"][1:3] == ["ICON", user_icon]
        assert visible["Terminal"][1:3] == ["ICON", terminal_icon]
        assert visible["Inherited"][1:3] == ["ICON", inherited]
        assert visible["Absolute"][1:3] == ["ICON", str(absolute_icon)]
        assert visible["No Icon"][1] == "EXEC"
        assert "wmmenugen --launch 'nested-tool.desktop'" in visible["Nested Tool"][-1]
        assert entries(plist(run("--applications", "--category", "Network"))).keys() == {"Navigateur"}
        terminals = plist(run("--applications", "--category", "TerminalEmulator"))
        assert len(terminals) == 2 and terminals[1][0] == "Terminal"
        assert entries(plist(run("--category", "WebBrowser", "--applications"))).keys() == {"Navigateur"}
        assert all(item[1] == "EXEC" for item in entries(plist(run("--applications", "--no-icons"))).values())
        large = entries(plist(run("--applications", "--icon-size", "32")))["Navigateur"][2]
        assert "/32x32/" in large
        gtk4.unlink()
        gtk3 = config / "gtk-3.0" / "settings.ini"
        gtk3.parent.mkdir()
        gtk3.write_text("[Settings]\ngtk-icon-theme-name=Selected\n")
        assert entries(plist(run("--applications", "--icon-size", "16")))["Navigateur"][2] == user_icon
        gtk3.unlink()
        (home / ".gtkrc-2.0").write_text('gtk-icon-theme-name = "Selected"\n')
        assert entries(plist(run("--applications", "--icon-size", "16")))["Navigateur"][2] == user_icon
        (user / "applications" / "hidden.desktop").unlink()
        assert "Masked" in entries(plist(run("--applications")))
        desktop(user / "applications" / "browser.desktop", "Changed", "Categories=Network;\n")
        assert entries(plist(run("--applications", "--category", "Network"))).keys() == {"Changed"}
        run("--applications", "--category", "Network;bad", status=2)
        run("--applications", "--icon-theme", "../invalid", status=2)
        run("--launch", "does-not-exist.desktop", status=1)
        run("--launch", "browser.desktop", "--category", "Network", status=2)

        output = root / "launched.json"
        recorder = root / "record.py"
        recorder.write_text(
            "import json, os, sys\n"
            f"open({str(output)!r}, 'w').write(json.dumps([sys.argv[1:], os.getcwd()]))\n"
        )
        working = root / "working directory"
        working.mkdir()
        launcher = user / "applications" / "launch.desktop"
        desktop(launcher, "Launch App",
                f"Path={working}\nIcon=browser\n",
                f'{sys.executable} "{recorder}" "two words" %c %k %i %% %f %U')
        run("--launch", "launch.desktop")
        deadline = time.monotonic() + 5
        while not output.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        arguments, cwd = json.loads(output.read_text())
        assert arguments == ["two words", "Launch App", str(launcher), "--icon", "browser", "%"], arguments
        assert cwd == str(working), cwd
        test_cache(generator, root, env)
    print("PASS: XDG discovery, icons, launching, cache reuse, invalidation, corruption and concurrent generation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
