# Parallel and Asynchronous Simulation of Pandemic Spread 

## 1. Project overview

This project implements a stochastic epidemic simulation on an `N × N` two-dimensional population grid and studies sequential execution, multithreading, asynchronous work, and performance.

Each cell represents one person. A person can be in one of five states:

- `S` — susceptible
- `I` — infected (also stores the number of days infected)
- `R` — recovered
- `V` — vaccinated
- `D` — dead

The neighbourhood is the eight surrounding cells. The grid does **not** wrap around at its boundaries. Every simulation day is a synchronous update: all decisions for day `t + 1` are made using the grid from day `t`.

The repository is organised by assignment part. Each part is kept on its own Git branch:

| Branch | Assignment content |
|---|---|
| `main` | Part A — sequential baseline |
| `part_b` | Part B — threaded simulation and thread pool |
| `part_c` | Part C — asynchronous snapshots and parameter sweep |
| `part_d` | Part D — performance experiments |
| `part_e` | Part E — epidemiology experiments |


## 2. Model and mathematical rules

### 2.1 Infection probability

If a susceptible cell has `k` infected neighbours, its probability of becoming infected is

\[
P(k) = 1 - (1-\beta_{\mathrm{eff}})^k.
\]

Here, `k` ranges from 0 to 8. When `k = 0`, the infection probability is zero.

The effective transmission probability is

\[
\beta_{\mathrm{eff}} = \beta m,
\]

where `β` is the base transmission probability and `m` is the mask factor.

### 2.2 Disease progression

For an infected person, the infection counter is incremented once per day. When the counter reaches `d`, the person:

- dies with probability `μ`, becoming `D`; or
- recovers with probability `1 - μ`, becoming `R`.

A newly infected person starts with infection counter `c = 0`. Recovered, vaccinated, and dead people do not change state.

### 2.3 Lockdown

The main simulation loop uses the infected fraction from the completed day to decide the transmission probability for the next day.

- Start lockdown when the infected fraction rises above `θ_on`.
- While lockdown is active, multiply the current effective transmission probability by `λ`.
- Lift lockdown when the infected fraction falls below `θ_off`.

The decision based on day `t` is applied from day `t + 1`; it does not change the updates already completed for day `t`.

### 2.4 Initialisation

Initialisation is performed before the daily simulation:

1. Select a fraction `v` of the population at random and mark those cells vaccinated.
2. Select `I0` susceptible cells at random and mark them infected.

The initialisation may be sequential. The deterministic-randomness requirement applies to the daily simulation updates.

### 2.5 Default parameters

| Parameter | Meaning | Default |
|---|---|---:|
| `N` | Grid width and height | 1000 |
| `days` | Maximum number of simulated days | 200 |
| `β` | Base transmission probability | 0.08 |
| `d` | Infectious period | 7 days |
| `μ` | Mortality probability at the end of the infectious period | 0.01 |
| `I0` | Initially infected people | 10 |
| `v` | Vaccinated fraction | 0 |
| `m` | Mask factor | 1 |
| `λ` | Lockdown multiplier | 0.3 |
| `θ_on` | Lockdown activation threshold | 0.02 |
| `θ_off` | Lockdown deactivation threshold | 0.005 |
| `seed` | Random seed | 42 |

For performance experiments, use a fixed number of days so that runs perform comparable amounts of work. For epidemiology experiments, the simulation can stop when no infected people remain.

## 3. Deterministic random-number generation

A shared pseudo-random-number generator would introduce a data race or require synchronisation. A separate generator per thread would make results depend on the number of threads and the way work is partitioned.

Instead, the implementation uses a stateless hash-based random number generator based on **SplitMix64**. A random value is derived from:

```text
(seed, day, cell_id, stream)
```

The stream identifier separates independent decisions:

| Stream | Purpose |
|---:|---|
| `0` | Infection decision |
| `1` | Death decision |

The hash is repeatedly mixed with SplitMix64 so that the result is determined by the seed, day, cell, and stream—not by the worker thread or the order in which cells are processed.

The hash value is converted to a floating-point value in `[0, 1)` using the high 53 bits:

\[
r = \frac{\text{hash} \gg 11}{2^{53}}.
\]

A decision is made by comparing this value with the relevant probability. For example, a susceptible cell becomes infected if

\[
r_{\mathrm{infection}} < P(k).
\]

**Why this matters:** the same seed and model parameters produce the same cell-level random decisions regardless of thread count, chunk count, or grid partitioning. This is the basis of the sequential-versus-parallel correctness tests.

## 4. Shared simulation design

### 4.1 Two-grid update

The simulation maintains two grids:

- `current` — read-only input for the current day;
- `next` — output grid for the next day.

Workers read the state of a cell and its neighbours only from `current`. Each worker writes only to its assigned cells in `next`. After every worker has completed, the grids are swapped.

This avoids update-order dependence: a cell never observes a neighbour that has already been updated for the new day.

### 4.2 Why the update is race-free

The two-grid design separates reads from writes. Work is partitioned so that each output cell is owned by exactly one task during a day. No task modifies `current`, and no two tasks write to the same cell in `next`. The main thread waits for the day's work to finish before swapping the grids or starting the next day.

Each task also maintains its own local counts of the states. The main thread combines those counts after the tasks complete. This avoids shared counter updates during the parallel loop.

### 4.3 Daily CSV and checksum

The simulation writes one row per simulated day with the following fields:

```text
day,S,I,R,V,D,beta_eff,checksum
```

The checksum is calculated from the grid state (and the per-person infection counter where applicable). It provides a compact way to detect differences between sequential and parallel executions. The checksum is not a replacement for comparing the full CSV, but it makes daily state comparisons practical.

## 5. Implementation in each part

### Part A — Sequential baseline (`main`)

The sequential version is the reference implementation.

Main responsibilities:

1. Initialise the population using the seed and model parameters.
2. Update the grid one day at a time using the two-grid method.
3. Count the five states after each day.
4. Apply mask and lockdown rules at day boundaries.
5. Write daily totals, effective transmission probability, and checksum to CSV.
6. Support a fixed-day mode for controlled timing experiments.

The command-line interface includes model parameters such as `--N`, `--days`, `--seed`, `--beta`, `--d`, `--mu`, `--I0`, `--v`, `--m`, `--lambda`, `--theta-on`, `--theta-off`, and `--output`. The `--fixed-days` option is used when the experiment must execute a fixed number of days.

**Build (on `main`):**

```bash
cd MT26065
g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    src/main.cpp src/simulation.cpp -o pandemic
```

**Example run (from inside `MT26065/`):**

```bash
mkdir -p results
./pandemic --N 1000 --days 200 --seed 42 \
    --beta 0.08 --d 7 --mu 0.01 --I0 10 \
    --v 0 --m 1 --lambda 0.3 \
    --theta-on 0.02 --theta-off 0.005 \
    --output results/partA.csv --fixed-days
```

The exact accepted spelling of every option is defined by the argument parser in the checked-out branch. Run the executable's usage/help output if an option differs.

### Part B — Threaded simulation (`part_b`)

Part B parallelises the daily grid update while preserving the sequential result.

The implementation supports the following execution modes:

- **Sequential:** one worker performs the update.
- **Thread per day:** create one thread for each chunk for that day, then join all threads before swapping the grids.
- **Thread pool:** create a fixed pool of `T` workers once, submit `C` tasks per day, and wait for all submitted tasks before swapping the grids.

The grid can be divided in three ways:

1. **Strips:** each chunk owns a contiguous range of rows.
2. **Blocks:** each chunk owns a square/tiled region of the grid.
3. **Interleaved rows:** worker/chunk `i` processes rows `i`, `i + T`, `i + 2T`, and so on.

`C` (number of chunks/tasks) is distinct from `T` (number of worker threads). In the thread-pool implementation, `C` can be greater than `T`.

The correctness test runs the same input with multiple thread counts and all supported divisions, then compares each parallel CSV against the sequential CSV. The assignment specifies thread counts `1, 2, 3, 4, 7, 8, 16`.

**Build (on `part_b`):**

```bash
cd MT26065
g++ -std=c++17 -O2 -pthread \
    src/main.cpp src/simulation.cpp -o pandemic
```

**Example run pattern (from inside `MT26065/`):**

```bash
mkdir -p results
./pandemic --N 1000 --days 200 --seed 42 \
    --threads 4 --chunks 16 \
    --mode pool --division strips \
    --output results/partB_pool_strips.csv --fixed-days
```

Use the mode and division option names accepted by the checked-out `main.cpp`; the implementation's internal mode names are `sequential`, `thread-per-day`, and `pool`, and the division names are `strips`, `blocks`, and `interleaved`.

**Correctness test:** run the Part B test script from inside `MT26065/` (inspect `scripts/` for its actual filename), or run the same parameter set manually for each thread count and division. Every daily CSV must match the sequential reference.

### Part C — Asynchronous programming (`part_c`)

Part C contains two independent exercises.

#### C1. Background snapshot writer

The simulation periodically saves a PPM image of the grid.

- **Blocking snapshots:** the main simulation thread writes the image and waits for the write to finish.
- **Asynchronous snapshots:** copy the grid and submit the file-writing operation as an asynchronous task. The simulation can then continue while the copied grid is written.
- **Bounded pending writes:** allow at most `Q` snapshot tasks to remain pending. When the limit is reached, wait for the oldest pending write before submitting another.
- **Artificial I/O delay:** `--io-latency` adds a delay in milliseconds to each write, allowing the effect of slow storage to be measured.
- **Shutdown:** wait for all remaining writes and report failed writes before the program exits.

The grid copy is necessary because the simulation reuses and modifies its current/next buffers. Without a snapshot copy, the writer could read a grid while the simulation changes it, producing a data race or an inconsistent image.

A PPM image stores one colour per state. The precise colour mapping and snapshot filename convention are defined in the Part C source.

#### C2. Asynchronous parameter sweep

The sweep program runs independent simulations for combinations of parameters (for example, transmission probability `β`, vaccination fraction `v`, and random seed).

Each run returns a summary containing:

- peak infected fraction;
- day on which the peak occurs;
- total infected;
- total deaths;
- number of days simulated.

Completed futures are consumed in completion order. Progress is printed as runs finish, and each summary is appended to a results CSV.

A time limit can stop further work: runs that have not started are cancelled/not launched, while already running tasks are allowed to finish. The program reports runs with missing results.

**Build the sweep driver (on `part_c`):**

```bash
cd MT26065
g++ -std=c++17 -O2 -pthread \
    src/sweep.cpp src/simulation.cpp -o sweep
```

**Run pattern used during development (from inside `MT26065/`):**

```bash
mkdir -p results/partC
./sweep 2 4
```

In this invocation, the two positional arguments are the time limit in seconds and the worker-thread count, respectively. Check the sweep driver's usage text/source for the full default parameter combinations and output CSV name.

**Completion order versus submission order:** completion-order handling reports a result as soon as its run finishes, so progress reflects actual completed work. Submission-order handling may wait for an earlier, slower run even when later runs have already finished.

### Part D — Performance study (`part_d`)

Part D measures the performance of the simulation and its asynchronous features. The experiments should time the simulation/work being studied, exclude setup and unrelated output where the assignment requires it, and use repeated measurements. Report medians and spread rather than relying on one timing.

The benchmark driver is `src/benchmarks.cpp`. It is built separately from the normal simulation executable.

**Build (on `part_d`):**

```bash
cd MT26065
g++ -std=c++17 -O2 -pthread \
    src/benchmarks.cpp src/simulation.cpp -o part_d
```

**Run (from inside `MT26065/`):**

```bash
mkdir -p results/partD
./part_d
```

The benchmark driver controls the experiments and writes its raw measurements under `results/partD/`. The exact output filenames and whether individual experiments can be selected by command-line arguments are defined in `src/benchmarks.cpp`.

#### D1. Strong scaling

- Grid: `N = 4000`.
- Duration: 200 fixed days.
- No snapshots.
- Vary `T` from 1 through powers of two, up to twice the number of logical cores.
- Calculate speedup, efficiency, and the Karp–Flatt serial fraction.

The baseline is the one-thread time `T₁`; the parallel time with `p` threads is `Tₚ`.

\[
S(p) = \frac{T_1}{T_p}
\]

\[
E(p) = \frac{S(p)}{p}
\]

The Karp–Flatt estimate is

\[
e(p) =
\frac{\frac{1}{S(p)}-\frac{1}{p}}
     {1-\frac{1}{p}}.
\]

Amdahl's law predicts

\[
S(p) = \frac{1}{f + \frac{1-f}{p}},
\]

where `f` is the serial fraction. Compare the measured Karp–Flatt value with the Amdahl prediction. Beyond the physical-core count, extra threads may provide less benefit because of SMT resource sharing, scheduling overhead, and memory-bandwidth saturation.

#### D2. Weak scaling

Keep the work per thread approximately constant (for example, 1,000 × 1,000 cells per thread) while increasing the grid size with the number of threads.

The ideal weak-scaling efficiency is 1. In practice, efficiency can fall because of memory bandwidth, cache capacity, memory allocation/initialisation, and other fixed or communication/scheduling costs.

Gustafson's scaled-speedup model is

\[
S_G(p) = p - f(p-1).
\]

#### D3. Size and shape of work units

1. Keep `T` equal to the number of physical cores and vary `C` from `T` to `64T`. Compare execution times to find a useful chunk count.
2. Compare strips, blocks, and interleaved rows. Consider locality and the amount of scheduling overhead.
3. Compare thread-per-day and thread-pool execution for small grids (`N = 50` to `1000`). Identify the smallest grid where parallel execution beats sequential execution. A thread pool can reduce repeated thread-creation overhead, while very small grids may not contain enough work to amortise parallel overhead.

#### D4. Parallelism within a run versus across runs

Use `P` physical cores to complete 32 independent parameter-sweep runs. Compare configurations satisfying `R × T = P`, such as:

| Concurrent runs `R` | Threads per run `T` |
|---:|---:|
| 1 | 8 |
| 2 | 4 |
| 4 | 2 |
| 8 | 1 |

Measure total completion time and runs per second. The best configuration depends on per-run threading overhead, variation in run duration, and memory consumption.

#### D5. Hiding I/O time

Compare:

1. no snapshots;
2. blocking snapshots;
3. asynchronous snapshots.

Vary `--io-latency`, snapshot interval `K`, and pending-write limit `Q`. Compare total elapsed time and memory use. Asynchronous writing can hide some I/O time when writing overlaps useful simulation work. If the writer is consistently slower than snapshot production, the pending queue fills and the simulation must wait; at that point, writing becomes a bottleneck.

### Part E — Epidemiology (`part_e`)

Part E uses the simulator to study epidemic behaviour rather than primarily measuring execution speed.

The assignment asks for `N = 500` to `1000`, at least 10 seeds per setting, and averages plus spread.

#### E1. Default epidemic curves

Plot susceptible (`S`), infected (`I`), recovered (`R`), and dead (`D`) counts/fractions over time using the default model parameters.

#### E2. Transmission probability

Vary `β` and identify the range below which outbreaks tend to die out quickly. Use multiple seeds because the model is stochastic.

#### E3. Vaccination

Vary `v` and estimate the vaccination fraction at which sustained outbreaks become unlikely. Report the estimate as an empirical result for this local-neighbour grid model, not as a universal herd-immunity threshold.

#### E4. Masks and lockdown

Compare epidemic curves with and without masks and lockdown. Discuss changes in peak height and the day of the peak.

#### E5. Comparison with standard SIR

Compare the grid simulation with the standard SIR differential equations using similar parameters. The SIR model assumes homogeneous mixing, whereas this simulation permits infection only through the eight neighbouring cells. Spatial clustering and local depletion of susceptible neighbours can therefore change outbreak timing and shape.

The exact Part E driver filename, command-line interface, output filenames, and plotting script should be taken from the files on the `part_e` branch.

## 6. Build and run reference

All commands below are run from the `MT26065/` directory (`cd MT26065` from the repository root) after checking out the corresponding branch. The project uses C++17 and POSIX threads; `-pthread` is required for threaded builds.

| Branch | Executable | Build command |
|---|---|---|
| `main` | `pandemic` | `g++ -std=c++17 -O2 -Wall -Wextra -pedantic src/main.cpp src/simulation.cpp -o pandemic` |
| `part_b` | `pandemic` | `g++ -std=c++17 -O2 -pthread src/main.cpp src/simulation.cpp -o pandemic` |
| `part_c` sweep | `sweep` | `g++ -std=c++17 -O2 -pthread src/sweep.cpp src/simulation.cpp -o sweep` |
| `part_d` benchmarks | `part_d` | `g++ -std=c++17 -O2 -pthread src/benchmarks.cpp src/simulation.cpp -o part_d` |
| `part_e` | `part_e` | Using the Part E driver source shown by `find src -maxdepth 1 -type f -name '*.cpp'`; compile that driver together with `src/simulation.cpp` using the same flags. |

For Python plotting scripts, need to run by activating the virtual Python environment that has the required packages (typically `numpy`, `pandas`, and `matplotlib`):

```bash
# from inside MT26065/
python3 scripts/<plotting-script>.py
```

## 7. Results and generated files

Generated data and figures are under `MT26065/results/`, separated by assignment part. 

## 8. Reproducibility and performance methodology

For reproducible results:

1. Use the same seed and model parameters when comparing sequential and parallel implementations.
2. Use fixed-day runs for timing experiments.
3. Repeat each timing measurement at least five times.
4. Report the median and spread of the measurements.
5. Use the same machine and avoid other heavy workloads during measurements.
6. Record the CPU model, physical and logical core counts, cache sizes, RAM, operating system, and compiler version.
7. Separate simulation time from setup and output time, except for experiments that explicitly measure I/O (D5).

Environment commands on Linux used in the project:

```bash
lscpu
free -h
uname -a
g++ --version
```

## 9. Important implementation constraints

- Daily updates are synchronous; workers read only the old grid and write only their assigned cells in the new grid.
- No worker updates a neighbour's cell.
- Per-task state counts are local and are combined by the main thread.
- The main thread waits for all work for a day before swapping grids.
- Hash-based random values are keyed by seed, day, cell, and decision stream, not by thread ID.
- Thread count and partitioning must not change the simulation output.
- Part C's asynchronous snapshot writer must own a stable copy of the grid it writes.
- Pending asynchronous work must be joined/waited for before program exit.


## LLM Prompts Used

1. How can I implement a synchronous grid-based simulation in C++ without modifying the original grid during an update?
2. What is the safest way to calculate the eight neighbours of a cell, especially for corner and boundary cells?
3. Can you explain how SplitMix64 works and how I can use it to generate deterministic random numbers from a seed, day, cell ID, and stream?
4. Why does using a separate random-number generator for each thread make the output dependent on the number of threads?
5. Can you review my infection and recovery logic and identify any edge cases involving the infection counter?
6. Why should the main thread wait for all worker threads before swapping the current and next grids?
7. How can I compare the daily CSV outputs of my sequential and parallel implementations automatically and identify the first day on which they differ?
8. How can I limit the number of outstanding asynchronous snapshot-writing tasks without using locks or condition variables?
9. How can I process asynchronous simulation results in the order in which they complete rather than the order in which they were submitted?
10. How should a parameter sweep handle a time limit when some tasks have already started while others are still waiting to execute?
11. What is the difference between strong scaling and weak scaling, and how should the grid size change in each experiment?
12. How can I estimate an empirical herd-immunity threshold by varying the vaccinated fraction over multiple random seeds?
13. What is the best way to calculate the average and spread of peak infection, peak day, total infections, and mortality across multiple stochastic simulation runs?
14. Can you identify why this program compiles successfully but producing an incomplete CSV output?
15. Write the compilation commands for multiple C++ source files using g++ and pthread support without introducing a Makefile?
