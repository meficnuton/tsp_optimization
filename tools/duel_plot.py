#!/usr/bin/env python3
# Duel convergence charts: mean gap vs generation per config, one PNG per class.
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

raw = pd.read_pickle('results/duel_convergence_raw.pkl')
STYLES = {
    'baseline_ox_swap':      ('baseline (OX+swap, random)', '#7f7f7f', '-'),
    'combo_ox_inv10':        ('combo OX+inversion (random)', '#1f77b4', '-'),
    'combo_erx_inv10':       ('combo ERX+inversion (random)', '#9467bd', '-'),
    'init_greedy':           ('init greedy', '#2ca02c', '-'),
    'init_rgreedy':          ('init rgreedy', '#98df8a', '--'),
    'init_greedy_twoopt25':  ('init greedy + 2opt(1/25)', '#d62728', '-'),
    'twoopt10_random':       ('2opt(1/10) random', '#ff7f0e', '-'),
}
for cls, sub in raw.groupby('size_class'):
    fig, ax = plt.subplots(figsize=(7.4, 4.4))
    for cfg, g in sub.groupby('config'):
        if cfg not in STYLES:
            continue
        label, color, ls = STYLES[cfg]
        mean = g.groupby('generation')['gap_percent'].mean().sort_index()
        ax.plot(mean.index, mean.values, ls, color=color, lw=2.0, label=label)
    ax.set_xlabel('Thế hệ (generation)')
    ax.set_ylabel('Gap so với tối ưu (%) — trung bình')
    ax.set_title(f'Duel toán tử — lớp {cls}', fontsize=12)
    ax.set_yscale('log')
    ax.grid(alpha=0.3, which='both')
    ax.legend(fontsize=8)
    fig.tight_layout()
    out = f'latex/images/duel_{cls}.png'
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print('chart ->', out)
