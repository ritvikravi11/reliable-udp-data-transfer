"""Create aggregate tables and a hypothesis analysis from experiment CSVs."""

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return [
            row for row in csv.DictReader(stream)
            if row.get("status") == "passed" and
            row.get("integrity") == "passed"
        ]


def mean(values):
    return sum(values) / len(values)


def stdev(values):
    if len(values) < 2:
        return 0.0
    average = mean(values)
    return math.sqrt(sum((value - average) ** 2 for value in values)
                     / (len(values) - 1))


def summarize(rows, x_key, y_key):
    groups = defaultdict(list)
    for row in rows:
        groups[(row["protocol"], row[x_key])].append(float(row[y_key]))
    result = []
    for (protocol, x_value), values in sorted(groups.items()):
        result.append({
            "protocol": protocol,
            x_key: x_value,
            "trials": len(values),
            f"{y_key}_mean": f"{mean(values):.6f}",
            f"{y_key}_stdev": f"{stdev(values):.6f}",
        })
    return result


def write_csv(path, rows, fields):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--loss", type=Path, required=True)
    parser.add_argument("--window", type=Path, required=True)
    parser.add_argument("--reorder", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    loss = read_rows(arguments.loss)
    window = read_rows(arguments.window)
    reorder = read_rows(arguments.reorder)
    write_csv(
        arguments.output / "loss_summary.csv",
        summarize(loss, "loss_rate", "goodput_bytes_per_sec"),
        ["protocol", "loss_rate", "trials",
         "goodput_bytes_per_sec_mean", "goodput_bytes_per_sec_stdev"],
    )
    write_csv(
        arguments.output / "window_summary.csv",
        summarize(window, "window_size", "goodput_bytes_per_sec"),
        ["protocol", "window_size", "trials",
         "goodput_bytes_per_sec_mean", "goodput_bytes_per_sec_stdev"],
    )
    write_csv(
        arguments.output / "reorder_summary.csv",
        summarize(reorder, "reorder_rate", "retransmissions"),
        ["protocol", "reorder_rate", "trials",
         "retransmissions_mean", "retransmissions_stdev"],
    )
    with (arguments.output / "RESULTS_ANALYSIS.md").open(
            "w", encoding="utf-8") as stream:
        stream.write("# Final results analysis\n\n")
        stream.write("Only rows with `status=passed` and "
                     "`integrity=passed` are included. Means and sample "
                     "standard deviations are computed separately for each "
                     "protocol and sweep setting.\n\n")
        stream.write("## Hypothesis\n\n")
        stream.write("The proposal predicts that Selective Repeat will have "
                     "higher goodput than Go-Back-N as loss increases, with "
                     "the gap widening above roughly 10%. Consult "
                     "`loss_summary.csv` for the measured comparison. This "
                     "analysis does not declare the hypothesis supported "
                     "unless the observed means show that ordering.\n\n")
        stream.write("## Reproducibility\n\n")
        stream.write("The raw CSVs contain one row per protocol/configuration/"
                     "repetition, including run ID, seed, status, integrity, "
                     "elapsed time, goodput, and retransmissions. Re-run "
                     "`scripts/run_experiments.py` with the configuration in "
                     "`EXPERIMENT_CONFIGURATION.txt`.\n\n")
        stream.write("## Limitations\n\n")
        stream.write("- Failed or integrity-invalid runs are not aggregated.\n")
        stream.write("- GBN and SR currently use different retransmission "
                     "strategies by design; their RTO implementation details "
                     "must be considered when interpreting goodput.\n")
        stream.write("- Results are valid only for the recorded machine, "
                     "seed set, file, and emulator configuration.\n")


if __name__ == "__main__":
    main()
