# TSP Solver & Benchmark Framework

C++17 project implementing two heuristic solvers for the Traveling Salesman
Problem — a nearest-neighbour **greedy** solver and a **genetic algorithm**
(GA) — plus a benchmark framework that measures both against known-optimal
TSPLIB tour lengths, organized by problem size.

The current focus is a GA-vs-Greedy configuration benchmark: GA configurations
are typed by hand in a CSV file, every configuration carries its own compute
budget, and results are aggregated per instance with win rates against the
greedy baseline.

---

## Repository layout

```
tsp/
├── README.md                     # this file
├── tree.md                       # condensed project tree
├── uml-class-diagram.html        # UML class diagram of the solver framework
│
├── src/                          # all sources (build & run from repo root)
│   ├── TSPProblem.h/.cpp         # problem definition (name, dimension, distance matrix, tourCost)
│   ├── MatrixLoader.h/.cpp       # TSPLIB file parser → distance matrix
│   ├── TSPSolver.h               # solver interface + SolverConfig base
│   ├── SolverResult.h        # SolverResult struct (tour, cost, time, gap%)
│   ├── GreedySolver.h/.cpp       # nearest-neighbour greedy solver
│   ├── GeneticAlgorithm.h/.cpp   # genetic algorithm solver (OX/PMX, elitism, tournament)
│   │
│   ├── benchmark/                # benchmark framework
│   │   └── GaBenchmark.h/.cpp    # GA-vs-Greedy config benchmark building blocks
│   │
│   └── _benchmark_ga_configs.cpp # MAIN harness: GA configs from CSV vs greedy
│   
│
├── configs/                      # GA benchmark config CSVs (one row per config)
├── bin/                          # compiled harness executables
├── results/                      # timestamped benchmark outputs (CSV + summary .txt)
└── data/                         # TSPLIB instances grouped by city count
    └── solutions                 # known-optimal costs, `name : value` per line
```

Size classes in `data/` (auto-discovered by the main harness):

| Class        | Instances | City range     |
|--------------|-----------|----------------|
| `0-100`      | 22        | 14 – 99        |
| `100-200`    | 25        | 100 – 198     |
| `200-500`    | 19        | 202 – 493     |
| `500-1000`   | 12        | 535 – 1000    |
| `1000-10000` | 27        | 1173 – 7397   |
| `10000+`     | 7         | 11849 – 85900 |

`data/solutions` holds the reference optima (`name : value` per line, trailing
comments like `(CEIL_2D)` are tolerated). Instances missing from the file get
a NaN gap in reports.

---

## Building

Toolchain: `g++` with `-std=c++17` (uses `std::filesystem`). **Build and run
from the repo root** — harnesses resolve `data/...`, `configs/...`, and
`results/...` relative to the working directory.

```bash
# Main harness (from the tsp/ repo root)
g++ -O2 -std=c++17 \
    src/_benchmark_ga_configs.cpp src/benchmark/GaBenchmark.cpp \
    src/GeneticAlgorithm.cpp src/GreedySolver.cpp src/TSPProblem.cpp src/MatrixLoader.cpp \
    -o bin/_benchmark_ga_configs.exe
```

> **Static linking is not required.** A plain dynamic build runs fine as long
> as the building toolchain's `bin` directory is on `PATH` (it normally is).
> The silent exit code 127 failure occurs only when a *different* MinGW's
> `libstdc++-6.dll` (e.g. git-bash's bundled copy) shadows the compiler's own
> on `PATH`. Fix the `PATH` order, or pass `-static -static-libgcc
> -static-libstdc++` as a workaround.

Compile-check everything without producing artifacts:

```bash
g++ -O2 -std=c++17 -fsyntax-only -Isrc src/*.cpp src/benchmark/*.cpp
```

Include convention: headers include each other relative to their own file
(`benchmark/Benchmark.h` uses `../TSPProblem.h`; harness `main`s sit at
`src/` level and include `benchmark/GaBenchmark.h`). Keep core sources in
`src/` and harness mains next to `benchmark/` — moving files requires
updating these relative includes.

---

## The solvers

Both solvers implement the `TSPSolver` interface:

```cpp
class TSPSolver {
public:
    virtual SolverResult solve(const TSPProblem& problem,
                               const SolverConfig& config) = 0;
    virtual std::string name() const = 0;
};
```

Each solver has its own config struct deriving from `SolverConfig`, and
returns a `SolverResult` (`algorithm`, `tour`, `cost`, `time_ms`,
`reference_cost`, `gap_percent`).

### GreedySolver — nearest neighbour

From a configurable start vertex (default 0), repeatedly visits the closest
unvisited city. Deterministic (ties broken by lowest index), O(n²) tour
construction. Config: `GreedyConfig { start_vertex }`.

### GeneticAlgorithm

A classic generational GA over permutations:

| Parameter (`GeneticAlgorithmConfig`) | Default | Notes |
|---|---|---|
| `population_size` | 100 | must be ≥ 1 |
| `generations` | 200 | compute budget knob |
| `mutation_rate` | 0.10 | swap mutation, **at most once per child** — so high values (e.g. 0.40) are viable |
| `crossover_rate` | 0.90 | probability a child is crossed over (else clones parent 1) |
| `elitism_count` | 1 | best individuals copied unchanged each generation |
| `tournament_size` | 3 | tournament selection |
| `seed` | 0 | 0 = non-deterministic; non-zero = reproducible |
| `crossover_type` | OX | OX (ordered) or PMX (partially mapped), both permutation-preserving |

Initial population: one identity permutation plus `population_size - 1`
random shuffles. Each generation: sort by cost, copy the elite, then fill with
tournament-selected parents → crossover → mutate. The best individual of the
final generation is returned.

---

## Benchmark framework

### Core runner — `src/benchmark/Benchmark.{h,cpp}`

The simple runner used by the legacy harnesses:

```cpp
std::vector<BenchmarkResult> runBenchmark(
    const std::vector<BenchmarkCase>& cases,      // problem + reference cost
    const std::vector<BenchmarkSolver>& solvers, // solver + config + label
    const BenchmarkOptions& options);             // runs, print_tour

void writeBenchmarkStdout(results, out);
void writeBenchmarkCsv(results, out, header, tours);
```

Pitfall: `runBenchmark` reuses one config object per solver, so for a
stochastic solver a fixed seed yields N identical runs — the config-sweep
harness calls the solvers directly instead.

### GA config benchmark — `src/benchmark/GaBenchmark.{h,cpp}`

The reusable building blocks behind the main harness:

- **`ReferenceCosts`** — loads known optima from a `name : value` solutions
  file; unknown instance → NaN gap. Optima are never hardcoded.
- **`GaVariant` / `GaConfigFile`** — a named GA config; population and
  generations are part of the config (the budget is a row in the CSV, not
  code). Duplicate labels are rejected.
- **`RunRecord`** — one (config, instance, seed) run.
- **`BenchmarkStats`** — Welford streaming best/mean/std of gap% plus mean
  wall time and win counts.
- **`BenchmarkAggregator`** — collects records, produces per-class/config
  rankings and the per-instance Greedy-vs-GA comparison.
- **`ReportWriter`** — timestamped `results/<name>_YYYYMMDD_HHMMSS.csv` and
  matching `_summary_....txt` (never a fixed filename a rerun overwrites).

---

## Running the main benchmark

```
./bin/_benchmark_ga_configs.exe [options]

--runs=N        seeds per (config, instance)          [default: 5]
--classes=A,B   subset of auto-discovered data/ dirs  [default: all]
--configs=PATH   GA config CSV file                   [default: configs/ga_configs.csv]
--filter=SUBSTR  only configs whose label contains SUBSTR
--tours          append a `tour` column to the CSV
```

Methodology (implemented in `_benchmark_ga_configs.cpp`):

- **Size classes are auto-discovered** — every `data/` subdirectory holding
  at least one `.tsp` file is a class. Nothing about the test set is
  hardcoded.
- **Greedy runs ONCE per instance** (deterministic); the stochastic GA runs
  `--runs` times per (config, instance).
- **Seeds are derived deterministically** from (config label, instance name,
  run index) — runs are distinct *and* reproducible.
- Configs whose label starts with `baseline` fill the `baselineGA%` column.

Example runs (from the repo root):

```bash
# Quick smoke test on the small class
./bin/_benchmark_ga_configs --runs=3 --classes=0-100 --configs=configs/ga_config_default.csv

# The tuned champion config for 0-100
./bin/_benchmark_ga_configs --runs=5 --classes=0-100 --configs=configs/ga_config_0-100_final.csv

# Full sweep, all classes (expensive on 1000-10000 and 10000+)
./bin/_benchmark_ga_configs --runs=5 --configs=configs/ga_config_default.csv
```

### Config CSV format (`configs/*.csv`)

```
label,population,generations[,mutation_rate,crossover_rate,elitism,tournament,crossover]
```

- `#` comments, blank lines, and a `label,...` header row are skipped.
- `population` and `generations` are **required** (≥ 1) — the compute budget
  is part of the config, so a CSV fully defines a benchmark (no recompile).
- Omitted/empty trailing fields fall back to defaults: mut 0.10, xov 0.90,
  elitism 2, tournament 3, crossover OX.
- `crossover` is `OX` or `PMX` (case-insensitive).
- Labels must be unique; a label starting with `baseline` feeds the
  `baselineGA%` column.
- To compare configs fairly, give them the **same population × generations**
  and vary one knob at a time.

### Outputs

Per-run CSV (`results/ga_benchmark_<timestamp>.csv`):

```
size_class,instance,dimension,config,seed,cost,reference_cost,gap_percent,time_ms[,tour]
```

Summary (printed to stdout, also written to
`results/ga_benchmark_summary_<timestamp>.txt`):

- **Config ranking per class** by mean gap%: mean/best/std gap, mean wall
  time, per-instance win count.
- **Per-instance Greedy-vs-best-GA table**: `greedy%` vs `bestGA%` vs
  `baselineGA%`, the winner, and which GA config won each instance.
- **Overall GA win rate** over greedy.

Progress messages go to stderr; results go to stdout, so shell redirection
keeps logs clean.

---

## Current results (0-100 class)

The champion config found by a two-round sweep
(`configs/ga_config_0-100_final.csv`):

```
big_200x900_m40_x99_e5,200,900,0.40,0.99,5,3,OX
```

i.e. population 200, 900 generations, mutation 0.40, crossover rate 0.99,
elitism 5, tournament 3, OX. At 5 seeds it **beats Greedy(start=0) on all
22 instances** of `data/0-100` on the mean-gap criterion (GA mean gap 6.71%
vs greedy 24.83%; see
`results/ga_benchmark_summary_20260930_101053.txt`). Knobs that mattered:
high mutation (the GA mutates at most once per child), crossover rate 0.99,
elitism 5, and a 200×900 budget.

Note a plain GA (no 2-opt local search) is far from optimum at n≈1000+, so
on the large classes configs are best compared against each other rather
than against the optimum.

---

## Conventions & pitfalls

- **Run from the repo root.** Harnesses read `data/`, `configs/`, and write
  `results/` relative to the cwd.
- **Reference optima live in `data/solutions`**, never in code.
- **Size classes are auto-discovered** — add a `data/<name>/` directory with
  `.tsp` files and it joins the benchmark automatically.
- **Never hardcode a size-class list** or a config sweep in a harness; the
  config CSV is the benchmark definition.
- Timestamped output files only — a rerun must never overwrite results.
- Greedy is deterministic → one run per instance; GA is stochastic → seed it
  deterministically from (config, instance, run) for reproducibility.
