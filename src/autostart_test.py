import os
from pathlib import Path
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def wait_for(check, detail, timeout=10):
    deadline = time.monotonic() + timeout
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


def plstring(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    if any(shutil.which(tool) is None for tool in ("Xvfb", "xprop")) or not os.path.isdir("/proc"):
        print("SKIP: autostart test needs Xvfb, xprop and /proc")
        return 77
    wmaker = str(Path(sys.argv[1]).resolve())
    server = manager = None
    temp = tempfile.TemporaryDirectory(prefix="wmaker-autostart-")
    tray_pids = []
    try:
        root = Path(temp.name)
        defaults = root / "Defaults"
        defaults.mkdir()
        output = root / "output"
        log = root / "wmaker.log"
        out = shlex.quote(str(output))
        tray = root / "wm-autostart-tray-test"
        tray.write_text(f"#!/bin/sh\necho $$ >> {shlex.quote(str(root / 'tray.pids'))}\n"
                        f"echo tray >> {out}\nwhile :; do sleep 0.2; done\n")
        tray.chmod(0o755)
        items = [
            {"Command": f"echo first >> {out}"},
            {"Command": f"echo disabled >> {out}", "Enabled": "NO"},
            {"Command": f"echo low $(nice) >> {out}", "Priority": "Low"},
            {"Command": f"echo delayed >> {out}", "Delay": "1"},
            {"Command": f"/bin/sh {shlex.quote(str(tray))} --hidden", "NoRelaunch": "YES"},
        ]
        entries = ", ".join("{" + " ".join(f"{k} = {plstring(v)};" for k, v in item.items()) + "}"
                            for item in items)
        (defaults / "WindowMaker").write_text("{ AutostartCommands = (" + entries + "); }")

        with (root / "xvfb.log").open("w") as server_log, log.open("w") as manager_log:
            server = subprocess.Popen(
                ["Xvfb", "-displayfd", "1", "-screen", "0", "800x600x24", "-nolisten", "tcp"],
                stdout=subprocess.PIPE, stderr=server_log, text=True,
            )
            assert select.select([server.stdout], [], [], 10)[0], "Xvfb did not start"
            display = server.stdout.readline().strip()
            assert display.isdigit(), "Xvfb failed to allocate a display"
            env = dict(os.environ, HOME=temp.name, WMAKER_USER_ROOT=temp.name, DISPLAY=f":{display}",
                       XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                       XDG_CACHE_HOME=str(root / "cache"))

            def lines():
                return output.read_text().splitlines() if output.exists() else []

            def count(word):
                return sum(1 for line in lines() if line.split()[0] == word)

            def detail():
                return f"output={lines()}\n{log.read_text()}"

            manager = subprocess.Popen(
                [wmaker, "--for-real", "--no-autolaunch", "--dont-restore",
                 "--no-dock", "--no-clip", "--no-drawer"],
                env=env, stdout=manager_log, stderr=manager_log,
            )
            started = time.monotonic()
            wait_for(lambda: count("first") == 1 and count("tray") == 1 and count("low") == 1, detail)
            assert count("delayed") == 0 or time.monotonic() - started >= 1, detail()
            wait_for(lambda: count("delayed") == 1, detail)
            assert time.monotonic() - started >= 1, detail()
            assert count("disabled") == 0, detail()
            base = os.getpriority(os.PRIO_PROCESS, 0)
            low = [line for line in lines() if line.startswith("low ")][0]
            assert int(low.split()[1]) == max(base, 10), detail()
            assert manager.poll() is None, detail()

            manager.send_signal(signal.SIGUSR1)
            wait_for(lambda: count("first") == 2 and count("delayed") == 2, detail)
            time.sleep(0.5)
            assert count("tray") == 1, detail()
            assert manager.poll() is None, detail()

            tray_pids = [int(pid) for pid in (root / "tray.pids").read_text().split()]
            for pid in tray_pids:
                os.kill(pid, signal.SIGTERM)
            wait_for(lambda: not any(Path(f"/proc/{pid}").exists() and
                                     "zombie" not in Path(f"/proc/{pid}/status").read_text().lower()
                                     for pid in tray_pids), detail)
            manager.send_signal(signal.SIGUSR1)
            wait_for(lambda: count("first") == 3 and count("tray") == 2, detail)
            tray_pids = [int(pid) for pid in (root / "tray.pids").read_text().split()]
    finally:
        for pid in tray_pids:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        stop(manager)
        stop(server)
        temp.cleanup()
    print("Autostart integration tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
