"""Run reproducible loss, window-size, and reordering sweeps.

Each row launches a real receiver, persistent emulator, and sender. Failed
runs are recorded with status and error rather than being presented as data.
"""

import argparse
import csv
import hashlib
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path


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
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def parse_metrics(text):
    metrics = {}
    for line in text.splitlines():
        if line.startswith("METRIC "):
            for field in line[7:].split():
                key, value = field.split("=", 1)
                metrics[key] = value
    return metrics


def run_transfer(root, input_file, protocol, loss, reorder, window, seed,
                 timeout):
    sender_port = free_port()
    receiver_port = free_port()
    output_file = root / f".experiment-{os.getpid()}-{sender_port}.bin"
    log_dir = Path(tempfile.mkdtemp(prefix="reliable-udp-experiment-"))
    logs = []
    processes = []
    try:
        receiver_log = (log_dir / "receiver.log").open("w+", encoding="utf-8")
        emulator_log = (log_dir / "emulator.log").open("w+", encoding="utf-8")
        sender_log = (log_dir / "sender.log").open("w+", encoding="utf-8")
        logs.extend((receiver_log, emulator_log, sender_log))
        receiver = subprocess.Popen(
            [
                str(root / "file_transfer.exe"), "receive",
                "--protocol", protocol, "--input", "unused",
                "--output", str(output_file), "--port", str(receiver_port),
                "--window", str(window),
            ],
            cwd=root, stdout=receiver_log, stderr=subprocess.STDOUT,
        )
        emulator = subprocess.Popen(
            [
                str(root / "emulator" / "udp_channel.exe"),
                str(sender_port), str(receiver_port),
                str(loss), "0", "0", str(reorder), "0", "0",
                str(seed), "8000",
            ],
            cwd=root, stdout=emulator_log, stderr=subprocess.STDOUT,
        )
        processes.extend((receiver, emulator))
        time.sleep(0.5)
        if receiver.poll() is not None or emulator.poll() is not None:
            return {"status": "failed", "error": "startup failure"}
        sender = subprocess.Popen(
            [
                str(root / "file_transfer.exe"), "send",
                "--protocol", protocol, "--input", str(input_file),
                "--output", "unused", "--host", "127.0.0.1",
                "--port", str(sender_port), "--window", str(window),
            ],
            cwd=root, stdout=sender_log, stderr=subprocess.STDOUT,
        )
        processes.append(sender)
        try:
            sender_code = sender.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return {"status": "failed", "error": "sender timeout"}
        sender_log.seek(0)
        metrics = parse_metrics(sender_log.read())
        if sender_code != 0:
            return {"status": "failed", "error": f"sender exit {sender_code}",
                    **metrics}
        try:
            receiver_code = receiver.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return {"status": "failed", "error": "receiver timeout",
                    **metrics}
        if receiver_code != 0 or not output_file.exists():
            return {"status": "failed", "error": "receiver failure",
                    **metrics}
        if digest(input_file) != digest(output_file):
            return {"status": "failed", "error": "SHA-256 mismatch",
                    **metrics}
        return {"status": "passed", "error": "", **metrics}
    finally:
        for process in reversed(processes):
            stop_process(process)
        for stream in logs:
            stream.close()
        if output_file.exists():
            output_file.unlink()
        for path in log_dir.iterdir():
            path.unlink()
        log_dir.rmdir()


def values(text, cast=float):
    return tuple(cast(item) for item in text.split(",") if item)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path,
                        default=Path("results/experiments.csv"))
    parser.add_argument("--sweep", choices=("loss", "window", "reorder", "all"),
                        default="all")
    parser.add_argument("--protocols", default=",".join(PROTOCOLS))
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--loss-rates",
                        default="0,0.05,0.10,0.15,0.20,0.25,0.30")
    parser.add_argument("--window-sizes", default="1,4,8,16")
    parser.add_argument("--reorder-rates", default="0,0.10,0.25,0.50")
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--timeout", type=int, default=90)
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    protocols = tuple(arguments.protocols.split(","))
    loss_rates = values(arguments.loss_rates)
    window_sizes = values(arguments.window_sizes, int)
    reorder_rates = values(arguments.reorder_rates)
    if arguments.sweep == "loss":
        configurations = [(rate, 0.0, 4) for rate in loss_rates]
    elif arguments.sweep == "window":
        configurations = [(0.0, 0.0, size) for size in window_sizes]
    elif arguments.sweep == "reorder":
        configurations = [(0.0, rate, 4) for rate in reorder_rates]
    else:
        configurations = (
            [(rate, 0.0, 4) for rate in loss_rates] +
            [(0.0, 0.0, size) for size in window_sizes] +
            [(0.0, rate, 4) for rate in reorder_rates]
        )
    fields = [
        "run_id", "status", "integrity", "error", "sweep", "protocol",
        "loss_rate", "window_size",
        "reorder_rate", "seed", "bytes", "elapsed_seconds",
        "goodput_bytes_per_sec", "retransmissions", "source_sha256",
        "received_sha256",
    ]
    with arguments.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for protocol in protocols:
            for loss, reorder, window in configurations:
                for repetition in range(arguments.repetitions):
                    seed = arguments.seed + repetition
                    row = run_transfer(
                        root, arguments.input, protocol, loss, reorder,
                        window, seed, arguments.timeout
                    )
                    row.update({
                        "run_id": (
                            f"{arguments.sweep}-{protocol}-"
                            f"loss{loss:.2f}-window{window}-"
                            f"reorder{reorder:.2f}-rep{repetition}"
                        ),
                        "sweep": arguments.sweep,
                        "protocol": protocol,
                        "loss_rate": loss,
                        "window_size": window,
                        "reorder_rate": reorder,
                        "seed": seed,
                        "integrity": (
                            "passed" if row.get("status") == "passed"
                            else "not_verified"
                        ),
                    })
                    writer.writerow({field: row.get(field, "")
                                    for field in fields})
                    stream.flush()
                    print(
                        f"{protocol:18} loss={loss:.2f} "
                        f"window={window:2d} reorder={reorder:.2f} "
                        f"{row['status'].upper()} {row.get('error', '')}"
                    )


if __name__ == "__main__":
    main()
