"""Plot reproducible reliable-UDP experiment CSV output.

The experiment runner writes one row per transfer. Required columns are
protocol, loss_rate, window_size, goodput_bytes_per_sec, retransmissions,
and reorder_rate. Optional columns are accepted and ignored.
"""

import argparse
import csv
from collections import defaultdict
from pathlib import Path


def load_rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return [
            row for row in csv.DictReader(stream)
            if row.get("status", "passed") == "passed"
        ]


def number(row, key, default=0.0):
    value = row.get(key, "")
    return float(value) if value else default


def grouped_mean(rows, x_key, y_key):
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["protocol"], number(row, x_key))].append(
            number(row, y_key)
        )
    result = defaultdict(list)
    for (protocol, x_value), values in grouped.items():
        result[protocol].append((x_value, sum(values) / len(values)))
    for values in result.values():
        values.sort()
    return result


def make_plots(rows, output):
    import matplotlib.pyplot as pyplot

    output.mkdir(parents=True, exist_ok=True)
    plots = [
        ("loss_rate", "goodput_bytes_per_sec", "goodput_vs_loss_rate.svg",
         "Loss rate", "Goodput (bytes/sec)"),
        ("window_size", "goodput_bytes_per_sec",
         "goodput_vs_window_size.svg", "Window size", "Goodput (bytes/sec)"),
        ("reorder_rate", "retransmissions",
         "retransmissions_vs_reordering.svg", "Reordering probability",
         "Retransmissions"),
    ]
    for x_key, y_key, filename, xlabel, ylabel in plots:
        figure, axes = pyplot.subplots()
        for protocol, values in grouped_mean(rows, x_key, y_key).items():
            axes.plot([x for x, _ in values], [y for _, y in values],
                      marker="o", label=protocol)
        axes.set_xlabel(xlabel)
        axes.set_ylabel(ylabel)
        axes.grid(True, alpha=0.3)
        axes.legend()
        figure.tight_layout()
        figure.savefig(output / filename)
        pyplot.close(figure)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("results/plots"))
    arguments = parser.parse_args()
    rows = load_rows(arguments.csv)
    if not rows:
        raise SystemExit("experiment CSV contains no rows")
    make_plots(rows, arguments.output)


if __name__ == "__main__":
    main()
