#!/usr/bin/env python3
"""Plot GA2 convergence CSVs produced by _benchmark_ga2_convergence.exe.

For each size class it renders a two-panel figure:
  * left: mean gap% vs generation for every config (log y-scale), aggregated
    over instances and seeds;
  * right: final mean gap% per config, sorted (horizontal bars).

Usage:
  python tools/plot_convergence.py --csv results/ga2_convergence_<ts>.csv
  python tools/plot_convergence.py --csv a.csv --csv b.csv --classes=200-500
  python tools/plot_convergence.py --csv a.csv --out results/figs
"""

import argparse
import csv
import math
import os
import re
import sys
from collections import defaultdict
from datetime import datetime

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("matplotlib is required: pip install matplotlib")


def load(paths, class_filter=None):
    # rows[(class, config)][sample] -> list of (generation, gap)
    rows = defaultdict(lambda: defaultdict(list))
    for path in paths:
        with open(path, newline="") as fh:
            reader = csv.DictReader(fh)
            for r in reader:
                cls = r["size_class"]
                if class_filter and cls not in class_filter:
                    continue
                try:
                    gap = float(r["gap_percent"])
                except ValueError:
                    continue  # instance without a reference optimum
                if not math.isfinite(gap):
                    continue
                key = (cls, r["config"])
                rows[key][int(r["sample"])].append(
                    (float(r["generation"]), gap))
    return rows


def smooth(curve, window=3):
    """Simple moving average over (gen, gap) points, sorted by generation."""
    pts = sorted(curve)
    gens = [p[0] for p in pts]
    gaps = [p[1] for p in pts]
    if len(gaps) <= window:
        return gens, gaps
    out_g, out_v = [], []
    for i in range(len(gaps)):
        lo, hi = max(0, i - window // 2), min(len(gaps), i + window // 2 + 1)
        out_g.append(gens[i])
        out_v.append(sum(gaps[lo:hi]) / (hi - lo))
    return out_g, out_v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", action="append", required=True)
    ap.add_argument("--classes", default="", help="comma-separated subset")
    ap.add_argument("--out", default="results", help="output directory")
    ap.add_argument("--dpi", type=int, default=130)
    args = ap.parse_args()

    class_filter = {c for c in args.classes.split(",") if c}
    data = load(args.csv, class_filter or None)
    if not data:
        sys.exit("no usable rows found (check --csv / --classes)")

    os.makedirs(args.out, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    written = []

    by_class = defaultdict(dict)
    for (cls, config), samples in data.items():
        by_class[cls][config] = samples

    for cls in sorted(by_class):
        configs = by_class[cls]
        fig, (ax1, ax2) = plt.subplots(
            1, 2, figsize=(15, 6), gridspec_kw={"width_ratios": [3, 1]})

        finals = []
        for config in sorted(configs):
            samples = configs[config]
            # aggregate: mean gap per sample index
            curve = []
            for s in sorted(samples):
                pts = samples[s]
                gens = [g for g, _ in pts]
                gaps = [g for _, g in pts]
                curve.append((sum(gens) / len(gens),
                              sum(gaps) / len(gaps)))
            gens, gaps = smooth(curve)
            label = config if len(config) <= 28 else config[:25] + "..."
            ax1.plot(gens, gaps, label=label, linewidth=1.4)
            finals.append((gaps[-1], config))

        ax1.set_yscale("log")
        ax1.set_xlabel("generation")
        ax1.set_ylabel("mean gap to optimum (%)")
        ax1.set_title(f"{cls}: convergence by config "
                      f"(mean over instances/seeds)")
        ax1.legend(fontsize=7, ncol=2, loc="upper right")
        ax1.grid(alpha=0.3)

        finals.sort(reverse=True)  # worst at bottom
        ax2.barh([c for _, c in finals], [g for g, _ in finals], color="#4878cf")
        ax2.set_xlabel("final mean gap%")
        ax2.set_title("final gap ranking")
        ax2.tick_params(axis="y", labelsize=7)
        ax2.grid(alpha=0.3, axis="x")

        fig.tight_layout()
        out_path = os.path.join(args.out, f"ga2_convergence_{cls}_{stamp}.png")
        fig.savefig(out_path, dpi=args.dpi)
        plt.close(fig)
        written.append(out_path)
        print(f"wrote {out_path}")

    if not written:
        sys.exit("nothing plotted")


if __name__ == "__main__":
    main()
