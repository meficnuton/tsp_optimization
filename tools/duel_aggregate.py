#!/usr/bin/env python3
# Aggregate the operator-duel convergence run (2026-10-08):
#  * merge per-config convergence CSVs (launched from results/duel_conv_*.log)
#  * metrics per (config, class): final gap mean/std over 25 inst x 3 seeds,
#    gen95 (first checkpoint with mean gap <= 5%), win-vs-greedy per instance
# greedy%: 200-500 from results/ga2_200-500_stdout.txt; 500-1000 from
# results/ga2_500-1000_stdout.txt with pa561.tsp override (23.85, from
# results/duel_stats_500-1000_baseline_ox_swap.log on the fixed solutions file).
import glob, json, statistics, sys, time
import pandas as pd

t0 = time.time()
GREEDY = {}

def load_greedy(path, overrides=None):
    df = pd.read_csv(path, sep=r'\s{2,}', engine='python', header=None,
                     names=['inst', 'dim', 'greedy', 'best', 'baseline', 'w', 'cfg'])
    for _, r in df.iterrows():
        inst = str(r['inst'])
        try:
            g = float(r['greedy'])
        except (ValueError, TypeError):
            g = float('nan')
        if overrides and inst in overrides:
            g = overrides[inst]
        GREEDY[(path, inst)] = g

load_greedy('results/ga2_200-500_stdout.txt')
load_greedy('results/ga2_500-1000_stdout.txt', overrides={'pa561.tsp': 23.85})

# ---- which convergence CSV belongs to which config/class --------------------
CONFIGS = ['baseline_ox_swap', 'combo_ox_inv10', 'combo_erx_inv10',
           'init_greedy', 'init_rgreedy', 'init_greedy_twoopt25', 'twoopt10_random']
CLASSES = ['200-500', '500-1000']
csv_map = {}
for path in glob.glob('results/duel_conv_*.log'):
    cls = '200-500' if 'conv_200-500_' in path else '500-1000'
    lbl = path.split('conv_' + cls + '_')[1].rsplit('.log', 1)[0]
    for line in open(path):
        if 'convergence CSV: results/' in line:
            csv_map[(lbl, cls)] = line.split('convergence CSV: ')[1].strip()
print(f'CSV map ({len(csv_map)}):')
for k in sorted(csv_map):
    print('  ', k, csv_map[k])

# ---- merge -----------------------------------------------------------------
frames = []
for (lbl, cls), csv_path in csv_map.items():
    df = pd.read_csv(csv_path)
    frames.append(df)
raw = pd.concat(frames, ignore_index=True)
del frames
print(f'raw rows: {len(raw):,}  ({time.time()-t0:.0f}s)')

# sanity: every config/class fully covered
cov = raw.groupby(['config', 'size_class'])['instance'].nunique().unstack()
print(cov)

# ---- final gap per (config, class, instance, seed) -------------------------
final = (raw.sort_values('sample')
            .groupby(['config', 'size_class', 'instance', 'seed'])
            .tail(1))
stats = (final.groupby(['config', 'size_class'])
              .agg(final_gap_mean=('gap_percent', 'mean'),
                   final_gap_std=('gap_percent', lambda s: statistics.stdev(s)),
                   runs=('gap_percent', 'count')))
stats = stats.round(2)
print(stats)

# ---- gen95 per (config, class, instance, seed): first generation with gap<=5
raw95 = raw[raw['gap_percent'] <= 5.0]
gen95 = (raw95.sort_values('generation')
              .groupby(['config', 'size_class', 'instance', 'seed'])
              .head(1))
g95 = (gen95.groupby(['config', 'size_class'])
             .agg(gen95_mean=('generation', 'mean'),
                  gen95_n=('generation', 'count')))
g95['gen95_frac'] = (g95['gen95_n'] /
                     raw.groupby(['config', 'size_class'])['instance']
                        .nunique().mul(3))
print(g95.round(1))

# ---- win vs greedy per instance --------------------------------------------
rows = []
for (lbl, cls) in csv_map:
    sub = final[(final['config'] == lbl) & (final['size_class'] == cls)]
    for inst, g in sub.groupby('instance'):
        mean_gap = g['gap_percent'].mean()
        greedy = GREEDY.get(('results/ga2_200-500_stdout.txt' if cls == '200-500'
                             else 'results/ga2_500-1000_stdout.txt', inst),
                            float('nan'))
        rows.append({'config': lbl, 'size_class': cls, 'instance': inst,
                     'final_gap': mean_gap, 'greedy_gap': greedy,
                     'win': mean_gap < greedy})
win = pd.DataFrame(rows)
win['size_class'] = win['size_class'].astype(str)
win['size_class'] = pd.Categorical(win['size_class'], CLASSES)
wr = (win.groupby(['config', 'size_class'], observed=True)
         .agg(wins=('win', 'sum'), instances=('instance', 'nunique')))
wr['win_rate'] = (wr['wins'] / wr['instances'] * 100).round(0)
print(wr)

# per-instance win/loss detail
detail = win.pivot_table(index='instance', columns=['size_class', 'config'],
                         values='final_gap', observed=True).round(2)
detail.to_csv('results/duel_per_instance_final_gap.csv')

# ---- aggregate table --------------------------------------------------------
agg = stats.join(g95, how='outer').join(wr, how='outer')
agg = agg[['final_gap_mean', 'final_gap_std', 'gen95_mean', 'gen95_frac',
           'wins', 'instances', 'win_rate']]
agg = agg.round({'final_gap_mean': 2, 'final_gap_std': 2, 'gen95_mean': 0,
                 'gen95_frac': 2})
agg.index = pd.MultiIndex.from_tuples(agg.index, names=['config', 'size_class'])
agg = agg.reindex([(c, cl) for c in CONFIGS for cl in CLASSES if (c, cl) in
                   agg.index])
agg.to_csv('results/duel_aggregate_20261008.csv')
print('\n=== DUEL AGGREGATE ===')
print(agg.to_string())

raw.to_pickle('results/duel_convergence_raw.pkl')
win.to_csv('results/duel_win_vs_greedy_20261008.csv', index=False)
print(f'\ndone ({time.time()-t0:.0f}s)')
