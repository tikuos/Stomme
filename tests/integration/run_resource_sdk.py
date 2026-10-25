#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Qualify a relocated SDK client against an isolated desktop resource service.
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time


def run(args):
    """Build without source includes and exercise one running desktop."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="sv2-") as directory:
        work = Path(directory)
        sdk = work / "sdk"
        shutil.copytree(args.sdk.resolve(), sdk)
        source = work / "client.c"
        shutil.copy2(root / "examples/resource_client.c", source)
        client = work / "client"
        command = [os.environ.get("CC", "cc"), "-D_DEFAULT_SOURCE", "-std=gnu11",
                   "-Wall", "-Wextra", "-Werror", "-I" + str(sdk / "include"),
                   str(source), str(sdk / "lib/libtiku_application.a"),
                   str(sdk / "lib/libtiku_support.a"), "-lm", "-pthread", "-o", str(client)]
        build = subprocess.run(command, text=True, capture_output=True, timeout=120)
        (evidence / "client-build.log").write_text(build.stdout + build.stderr)
        assert build.returncode == 0, build.stderr
        home = work / "home"
        (home / "Desktop").mkdir(parents=True)
        runtime = work / "control"
        env = os.environ.copy()
        env.update(HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"),
                   XDG_DATA_HOME=str(home / ".local/share"), XDG_CACHE_HOME=str(home / ".cache"),
                   STOMME_RESOURCE_DIRECTORY=str(runtime), STOMME_RESOURCE_TEST_PROVIDER="1",
                   STOMME_RESOURCE_MOUNT="/sv2-" + str(os.getpid()))
        env.pop("DISPLAY", None)
        metadata = json.loads((sdk / "sdk.json").read_text())
        startup_cleanup = None
        if metadata.get("host") == "x11":
            failed = subprocess.run([str(args.desktop.resolve())], env=dict(env, DISPLAY=""),
                                    cwd=work, capture_output=True, text=True, timeout=20)
            (evidence / "startup-failure.log").write_text(failed.stdout + failed.stderr)
            assert failed.returncode != 0 and "no display" in failed.stderr, "startup failure was not exercised"
            assert not (runtime / "control.sock").exists(), "startup failure retained the owned resource endpoint"
            startup_cleanup = True
        log = open(evidence / "desktop.log", "w")
        process = subprocess.Popen([str(args.desktop.resolve()), "-conduct", str(work / "driver.sock")],
                                   env=env, cwd=work, stdout=log, stderr=subprocess.STDOUT)
        slow = None
        try:
            deadline = time.monotonic() + 20
            while not (runtime / "control.sock").exists():
                assert process.poll() is None, "desktop exited before endpoint startup"
                assert time.monotonic() < deadline, "endpoint startup deadline"
                time.sleep(0.02)
            slow = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            slow.connect(str(runtime / "control.sock"))
            slow.sendall(struct.pack(">I", 8192) + b"x")
            malformed = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            malformed.connect(str(runtime / "control.sock"))
            malformed.sendall(struct.pack(">I", 65537))
            malformed.close()
            workflow = subprocess.run([str(client), str(runtime)], capture_output=True, text=True, timeout=25)
            (evidence / "client.log").write_text(workflow.stdout + workflow.stderr)
            assert workflow.returncode == 0, workflow.stderr
            stopped = subprocess.run([str(client), str(runtime), "--stop"], capture_output=True, text=True, timeout=15)
            (evidence / "stop.log").write_text(stopped.stdout + stopped.stderr)
            assert stopped.returncode == 0, stopped.stderr
            assert process.wait(timeout=20) == 0, "desktop shutdown failed"
            assert not (runtime / "control.sock").exists(), "owned endpoint retained after shutdown"
            metadata = json.loads((sdk / "sdk.json").read_text())
            (evidence / "result.json").write_text(json.dumps({
                "status": "passed", "desktop": str(args.desktop.resolve()),
                "sdk_identity": metadata["sdk_identity"], "source_includes": False,
                "independent_client": True, "desktop_startup_shutdown": True, "startup_failure_cleanup": startup_cleanup,
                "partial_peer_did_not_block": True, "test_provider": True,
                "real_settings_coverage": 0, "hardware_ports_opened": False,
            }, indent=2) + "\n")
        finally:
            if slow is not None:
                slow.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            log.close()
    print("Resource SDK: independent relocated client and desktop runtime passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--desktop", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    run(parser.parse_args())
