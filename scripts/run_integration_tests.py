"""Run bounded end-to-end file-transfer tests through the UDP emulator."""

import argparse
import hashlib
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path


SCENARIOS = {
    "clean": (0.0, 0.0, 0.0, 0.0, 0, 0),
    "loss": (0.02, 0.0, 0.0, 0.0, 0, 0),
    "duplication": (0.0, 0.10, 0.0, 0.0, 0, 0),
    "corruption": (0.0, 0.0, 0.02, 0.0, 0, 0),
    "delay_jitter": (0.0, 0.0, 0.0, 0.0, 20, 20),
    "reordering": (0.0, 0.0, 0.0, 0.50, 0, 0),
    "combined": (0.02, 0.05, 0.01, 0.20, 10, 10),
}
PROTOCOLS = ("stop_wait", "go_back_n", "selective_repeat")


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def stop_process(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def run_case(root, input_file, protocol, scenario, settings, seed, timeout,
             keep_logs=False):
    loss, duplicate, corrupt, reorder, delay, jitter = settings
    sender_port = free_port()
    receiver_port = free_port()
    output_file = root / f".integration-{protocol}-{scenario}.bin"
    log_dir = Path(tempfile.mkdtemp(prefix="reliable-udp-"))
    receiver_log = (log_dir / "receiver.log").open("w+", encoding="utf-8")
    emulator_log = (log_dir / "emulator.log").open("w+", encoding="utf-8")
    sender_log = (log_dir / "sender.log").open("w+", encoding="utf-8")
    receiver = emulator = sender = None
    started = time.monotonic()
    try:
        receiver = subprocess.Popen(
            [
                str(root / "file_transfer.exe"), "receive",
                "--protocol", protocol, "--input", "unused",
                "--output", str(output_file), "--port", str(receiver_port),
                "--window", str(settings.get("window", 4)
                                if isinstance(settings, dict) else 4),
            ],
            cwd=root, stdout=receiver_log, stderr=subprocess.STDOUT,
        )
        emulator = subprocess.Popen(
            [
                str(root / "emulator" / "udp_channel.exe"),
                str(sender_port), str(receiver_port),
                str(loss), str(duplicate), str(corrupt), str(reorder),
                str(delay), str(jitter), str(seed), "8000",
            ],
            cwd=root, stdout=emulator_log, stderr=subprocess.STDOUT,
        )
        time.sleep(0.5)
        if receiver.poll() is not None or emulator.poll() is not None:
            raise RuntimeError("receiver or emulator exited during startup")
        sender = subprocess.Popen(
            [
                str(root / "file_transfer.exe"), "send",
                "--protocol", protocol, "--input", str(input_file),
                "--output", "unused", "--host", "127.0.0.1",
                "--port", str(sender_port), "--window", "4",
            ],
            cwd=root, stdout=sender_log, stderr=subprocess.STDOUT,
        )
        try:
            sender_code = sender.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return False, "sender timeout"
        if sender_code != 0:
            return False, f"sender exit {sender_code}"
        try:
            receiver_code = receiver.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return False, "receiver timeout"
        if receiver_code != 0:
            return False, f"receiver exit {receiver_code}"
        if not output_file.exists():
            return False, "output file missing"
        if digest(input_file) != digest(output_file):
            return False, "SHA-256 mismatch"
        return True, f"{time.monotonic() - started:.2f}s"
    finally:
        for process in (sender, receiver, emulator):
            if process is not None:
                stop_process(process)
        for stream in (receiver_log, emulator_log, sender_log):
            stream.close()
        if output_file.exists():
            output_file.unlink()
        if not keep_logs:
            for path in log_dir.iterdir():
                path.unlink()
            log_dir.rmdir()
        else:
            print(f"logs: {log_dir}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--input", type=Path)
    parser.add_argument("--protocol", choices=PROTOCOLS)
    parser.add_argument("--scenario", choices=tuple(SCENARIOS))
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--keep-logs", action="store_true")
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    input_file = arguments.input or (root / ".integration-input.bin")
    if not input_file.exists():
        input_file.write_bytes(os.urandom(16 * 1024))
        remove_input = True
    else:
        remove_input = False
    protocols = (arguments.protocol,) if arguments.protocol else PROTOCOLS
    scenarios = (arguments.scenario,) if arguments.scenario else tuple(SCENARIOS)
    failures = 0
    try:
        for protocol in protocols:
            for scenario in scenarios:
                ok, detail = run_case(
                    root, input_file, protocol, scenario,
                    SCENARIOS[scenario], arguments.seed, arguments.timeout
                    , arguments.keep_logs
                )
                print(f"{protocol:18} {scenario:14} "
                      f"{'PASS' if ok else 'FAIL'} {detail}")
                failures += not ok
    finally:
        if remove_input and input_file.exists():
            input_file.unlink()
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
