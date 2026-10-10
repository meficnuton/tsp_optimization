#!/usr/bin/env python3
"""Aggregate a GA2 operator-sweep convergence CSV into tables + line charts.

Usage (from tsp/ repo root):
    python tools/plot_operator_sweep.py --csv results/ga2_convergence_<ts>.csv \
        --outdir latex/images --table results/operator_sweep_100-200_table.csv

Outputs (per operator family, configs grouped by label prefix):
    op_sweep_selection.png / op_sweep_crossover.png / op_sweep_mutation.png
    op_sweep_final_ranking.png
    a CSV table: final mean gap %, best mean gap %, gen@95%-improvement.
"""
import argparse
import csv
import os
import re
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

FAMILIES = [
    ("sel_", "op_sweep_selection", "Toán tử CHỌN LỌC (selection)",
     r"^sel_", "baseline_tournament_ox_swap"),
    ("cross_", "op_sweep_crossover", "Toán tử LAI GHÉP (crossover)",
     r"^cross_", "baseline_tournament_ox_swap"),
    ("mut_", "op_sweep_mutation", "Toán tử ĐỘT BIẾN (mutation)",
     r"^mut_", "baseline_tournament_ox_swap"),
]
LABEL_MAP = {
    "baseline_tournament_ox_swap": "baseline (tournament+OX+swap)",
    "sel_roundrobin": "round-robin", "sel_roulette": "roulette",
    "sel_rank": "rank", "cross_pmx": "PMX", "cross_cx": "CX", "cross_erx": "ERX",
    "mut_inversion": "inversion (0.10)", "mut_insertion": "insertion (0.10)",
    "mut_scramble": "scramble (0.10)", "mut_random": "random (0.10)",
    "mut_inversion30": "inversion (0.30)", "mut_insertion30": "insertion (0.30)",
    "mut_scramble30": "scramble (0.30)",
}


def nice(label):
    return LABEL_MAP.get(label, label)


def load(path):
    # gaps[config][generation] -> list of gap%
    gaps = defaultdict(lambda: defaultdict(list))
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            cfg = row["config"]
            gen = int(row["generation"])
            gaps[cfg][gen].append(float(row["gap_percent"]))
    return gaps


def mean_curve(gaps, cfg):
    gens = sorted(gaps[cfg])
    return gens, [sum(v) / len(v) for v in (gaps[cfg][g] for g in gens)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True)
    ap.add_argument("--outdir", default="latex/images")
    ap.add_argument("--table", default="results/operator_sweep_table.csv")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    gaps = load(args.csv)
    configs = sorted(gaps)
    table_rows = []

    for cfg in configs:
        gens, curve = mean_curve(gaps, cfg)
        g0, gf = curve[0], curve[-1]
        # gen at which 95% of the total improvement (g0 -> gf) is reached
        target = gf + 0.05 * (g0 - gf)
        gen95 = next((g for g, v in zip(gens, curve) if v <= target), gens[-1])
        best_mean = min(curve)
        table_rows.append({
            "config": cfg, "initial_gap": round(g0, 2),
            "final_gap": round(gf, 2), "best_gap": round(best_mean, 2),
            "gen95_improvement": gen95,
            "improvement_pct": round(g0 - gf, 2),
        })

    os.makedirs(os.path.dirname(args.table) or ".", exist_ok=True)
    with open(args.table, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(table_rows[0]))
        w.writeheader()
        w.writerows(table_rows)
    print("table ->", args.table)
    for r in sorted(table_rows, key=lambda r: r["final_gap"]):
        print(f"{r['config']:<32} final {r['final_gap']:>7.2f}%  best {r['best_gap']:>7.2f}%  gen95 {r['gen95_improvement']:>4}")

    for prefix, fname, title, pat, base in FAMILIES:
        fig, ax = plt.subplots(figsize=(7.2, 4.2))
        members = [c for c in configs if re.search(pat, c)]
        if base not in members:
            members = [base] + members
        for cfg in members:
            gens, curve = mean_curve(gaps, cfg)
            lw = 2.6 if cfg == base else 1.6
            style = "-" if cfg == base else ("--" if cfg.endswith("30") else "-")
            ax.plot(gens, curve, style, lw=lw, label=nice(cfg))
        ax.set_xlabel("Thế hệ (generation)")
        ax.set_ylabel("Gap so với tối ưu (%)  — trung bình 25 instance × 3 seed")
        ax.set_title(title, fontsize=12)
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8)
        fig.tight_layout()
        out = os.path.join(args.outdir, fname + ".png")
        fig.savefig(out, dpi=160)
        plt.close(fig)
        print("chart ->", out)

    # final-gap ranking bar chart
    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    ranked = sorted(configs, key=lambda c: mean_curve(gaps, c)[1][-1])
    finals = [mean_curve(gaps, c)[1][-1] for c in ranked]
    colors = ["#c0392b" if c == "baseline_tournament_ox_swap" else "#2c7fb8"
              for c in ranked]
    ax.barh([nice(c) for c in ranked], finals, color=colors)
    ax.invert_yaxis()
    ax.set_xlabel("Gap cuối (%) — trung bình")
    ax.set_title("Xếp hạng theo gap cuối cùng", fontsize=12)
    ax.grid(alpha=0.3, axis="x")
    for i, v in enumerate(finals):
        ax.text(v, i, f" {v:.1f}", va="center", fontsize=8)
    fig.tight_layout()
    out = os.path.join(args.outdir, "op_sweep_final_ranking.png")
    fig.savefig(out, dpi=160)
    print("chart ->", out)


if __name__ == "__main__":
    main()
