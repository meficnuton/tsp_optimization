# Project tree

TSP solver project: greedy + genetic-algorithm solvers, a benchmark framework,
and TSPLIB instance data organized by problem size. See `README.md` for full
documentation (build, usage, config CSV format, methodology).

```
tsp/
├── README.md                     # main documentation
├── tree.md                       # this file
├── uml-class-diagram.html        # UML class diagram of the solver framework
│
├── src/                           # all sources (build from repo root, see below)
│   ├── TSPProblem.h / .cpp        # problem definition, loaded from distance matrix
│   ├── MatrixLoader.h / .cpp      # TSPLIB file → distance matrix
│   ├── TSPSolver.h                # solver interface + SolverConfig base
│   ├── SolverResult.h         # SolverResult struct
│   ├── GreedySolver.h / .cpp      # nearest-neighbour greedy solver
│   ├── GeneticAlgorithm.h / .cpp  # genetic algorithm solver (OX/PMX)
│   ├── _MatrixLoader_attached.h / .cpp  # MatrixLoader variant kept for reference
│   │
│   ├── benchmark/                 # benchmark framework
│   │   └── GaBenchmark.h / .cpp   #   GA-vs-Greedy config benchmark building blocks
│   │
│   └── _benchmark_ga_configs.cpp  # MAIN harness: GA configs from CSV vs greedy
│   
│
├── configs/                       # GA benchmark config CSVs (one row per config)
│   ├── ga_config_default.csv      #   one-factor sweep around the 150x600 baseline
│   ├── ga_config_0-100_round1.csv #   round-1 exploration for the <100 class
│   ├── ga_config_0-100_round2.csv #   round-2 combination/bigger budgets
│   └── ga_config_0-100_final.csv  #   champion config (beats greedy 22/22)
│
├── bin/                           # previously compiled harness executables
├── results/                      # timestamped benchmark outputs
│   ├── ga_benchmark_*.csv        #   per-run results (one row per run)
│   └── ga_benchmark_summary_*.txt #  human-readable summaries
│
└── data/                          # TSPLIB instances by city count (auto-discovered)
    ├── solutions                 # known-optimal costs, `name : value` per line
    ├── 0-100/                    # 22 instances, 14–99 cities
    ├── 100-200/                  # 25 instances, 100–198 cities
    ├── 200-500/                  # 19 instances, 202–493 cities
    ├── 500-1000/                 # 12 instances, 535–1000 cities
    ├── 1000-10000/               # 27 instances, 1173–7397 cities
    └── 10000+/                   # 7 instances, 11849–85900 cities
```

## Build & run

```bash
# Main harness (GA configs from CSV vs greedy)
g++ -O2 -std=c++17 \
    src/_benchmark_ga_configs.cpp src/benchmark/GaBenchmark.cpp \
    src/GeneticAlgorithm.cpp src/GreedySolver.cpp src/TSPProblem.cpp src/MatrixLoader.cpp \
    -o bin/_benchmark_ga_configs.exe

./bin/_benchmark_ga_configs --runs=5 --classes=0-100 --configs=configs/ga_config_0-100_final.csv
```

Static linking is NOT required: a plain dynamic build runs fine as long as the
building toolchain's `bin` dir is on `PATH` (it normally is). The silent
exit-127 failure only happens when another MinGW's `libstdc++-6.dll` (e.g.
git-bash's) shadows the compiler's own on `PATH` — fix `PATH` order, or add
`-static -static-libgcc -static-libstdc++` as a workaround.

## Layout notes

- Headers use quote-includes relative to their own file (`benchmark/Benchmark.h`
  includes `../TSPProblem.h`, harnesses include `benchmark/Benchmark.h`), so:
  - core sources stay directly in `src/` (a `src/tsp/` subfolder would break
    `benchmark/`'s `../` includes);
  - harness `main`s stay at `src/` level, next to `benchmark/`;
  - moving any `.h`/`.cpp` elsewhere requires updating includes.
- `bin/` and `results/` hold generated artifacts; `data/` is input only.
- Reference optima come from `data/solutions`, never from code; size classes
  are auto-discovered from `data/` subdirectories containing `.tsp` files.
