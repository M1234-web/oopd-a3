
import os
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

BASE = "results/partD"
os.makedirs(BASE, exist_ok=True)


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(BASE, name), dpi=160)
    plt.close(fig)


# --------------------------------------------------
# D1: Strong scaling
# --------------------------------------------------

def d1():
    f = os.path.join(BASE, "d1_strong_scaling.csv")

    if not os.path.exists(f):
        print("Run D1 first:", f)
        return

    raw = pd.read_csv(f)

    g = raw.groupby("threads")["seconds"].agg(
        median="median",
        min="min",
        max="max"
    ).reset_index()

    if not (g.threads == 1).any():
        print("D1 needs a 1-thread baseline.")
        return

    t1 = float(g.loc[g.threads == 1, "median"].iloc[0])

    g["speedup"] = t1 / g["median"]
    g["efficiency"] = g["speedup"] / g["threads"]

    g["karp_flatt"] = np.nan
    mask = g.threads > 1

    p = g.loc[mask, "threads"].to_numpy(dtype=float)
    s = g.loc[mask, "speedup"].to_numpy(dtype=float)

    g.loc[mask, "karp_flatt"] = (
        (1.0 / s - 1.0 / p) / (1.0 - 1.0 / p)
    )

    g.to_csv(
        os.path.join(BASE, "d1_analysis.csv"),
        index=False
    )

    fig, ax = plt.subplots()

    ax.plot(g.threads, g.speedup, "o-", label="Measured")
    ax.plot(g.threads, g.threads, "--", label="Ideal")

    valid = g[g.threads > 1]

    if len(valid):
        fserial = float(valid.karp_flatt.median())
        x = g.threads.to_numpy(dtype=float)

        ax.plot(
            x,
            1.0 / (fserial + (1.0 - fserial) / x),
            "s--",
            label=f"Amdahl, f={fserial:.4f}"
        )

    ax.set(
        xlabel="Threads",
        ylabel="Speedup",
        title="D1 Strong scaling: speedup"
    )
    ax.legend()
    ax.grid(True, alpha=0.3)
    save(fig, "d1_speedup.png")

    lower = g.efficiency - (t1 / g["max"])
    upper = (t1 / g["min"]) - g.efficiency

    lower = lower.clip(lower=0)
    upper = upper.clip(lower=0)

    fig, ax = plt.subplots()

    ax.errorbar(
        g.threads,
        g.efficiency,
        yerr=[lower, upper],
        fmt="o-",
        capsize=4
    )

    ax.set(
        xlabel="Threads",
        ylabel="Efficiency",
        title="D1 Strong scaling: efficiency"
    )
    ax.grid(True, alpha=0.3)
    save(fig, "d1_efficiency.png")

    fig, ax = plt.subplots()

    ax.plot(
        g.threads[g.threads > 1],
        g.karp_flatt[g.threads > 1],
        "o-"
    )

    ax.set(
        xlabel="Threads",
        ylabel="Karp-Flatt serial fraction",
        title="D1 Karp-Flatt estimate"
    )
    ax.grid(True, alpha=0.3)
    save(fig, "d1_karp_flatt.png")


# --------------------------------------------------
# D2: Weak scaling
# --------------------------------------------------

def d2():
    f = os.path.join(BASE, "d2_weak_scaling.csv")

    if not os.path.exists(f):
        print("Run D2 first:", f)
        return

    raw = pd.read_csv(f)

    g = raw.groupby(["threads", "N"])["seconds"].median().reset_index()

    baseline = g[g.threads == 1]

    if baseline.empty:
        print("D2 needs a 1-thread baseline.")
        return

    t1 = float(baseline.seconds.iloc[0])
    g["efficiency"] = t1 / g.seconds

    g.to_csv(
        os.path.join(BASE, "d2_analysis.csv"),
        index=False
    )

    fig, ax = plt.subplots()

    ax.plot(g.threads, g.efficiency, "o-")

    ax.set(
        xlabel="Threads",
        ylabel="Weak-scaling efficiency",
        title="D2 Weak scaling"
    )
    ax.grid(True, alpha=0.3)
    save(fig, "d2_efficiency.png")


# --------------------------------------------------
# D3: Size of work units
# --------------------------------------------------

def d3():
    f = os.path.join(BASE, "d3_work_units.csv")

    if not os.path.exists(f):
        print("Run D3 first:", f)
        return

    raw = pd.read_csv(f)

    experiments = [
        ("chunk_sweep", "d3_chunk_sizes.png", "chunks"),
        ("division", "d3_divisions.png", "division"),
        ("small_grid_sequential", "d3_small_grid_sequential.png", "N"),
        ("small_grid_thread-per-day", "d3_small_grid_thread_per_day.png", "N"),
        ("small_grid_pool", "d3_small_grid_pool.png", "N")
    ]

    for exp, name, xcol in experiments:
        sub = raw[raw.experiment == exp]

        if sub.empty:
            continue

        g = sub.groupby(xcol).seconds.median().reset_index()

        fig, ax = plt.subplots()

        if xcol == "division":
            ax.bar(g[xcol], g.seconds)
        else:
            ax.plot(g[xcol], g.seconds, "o-")

        ax.set(
            xlabel=xcol,
            ylabel="Median seconds",
            title=exp.replace("_", " ").title()
        )
        ax.grid(True, alpha=0.3)
        save(fig, name)


# --------------------------------------------------
# D4: Parallelism within a run vs across runs
# --------------------------------------------------

def d4():
    f = os.path.join(BASE, "d4_run_parallelism.csv")

    if not os.path.exists(f):
        print("Run D4 first:", f)
        return

    raw = pd.read_csv(f)

    g = raw.groupby(
        ["runs_at_once", "threads_per_run"]
    ).agg(
        seconds=("total_seconds", "median"),
        throughput=("runs_per_second", "median")
    ).reset_index()

    g.to_csv(
        os.path.join(BASE, "d4_analysis.csv"),
        index=False
    )

    fig, ax = plt.subplots()

    ax.plot(g.runs_at_once, g.seconds, "o-")

    ax.set(
        xlabel="Independent runs concurrently",
        ylabel="Total seconds",
        title="D4 Total completion time"
    )
    ax.grid(True, alpha=0.3)
    save(fig, "d4_time.png")

    fig, ax = plt.subplots()

    ax.plot(g.runs_at_once, g.throughput, "o-")

    ax.set(
        xlabel="Independent runs concurrently",
        ylabel="Runs per second",
        title="D4 Throughput"
    )
    ax.grid(True, alpha=0.3)
    save(fig, "d4_throughput.png")


# --------------------------------------------------
# D5: Hiding I/O time
# --------------------------------------------------

def d5():
    f = os.path.join(BASE, "d5_io_sweep.csv")

    if not os.path.exists(f):
        print("Run the D5 sweep first:", f)
        return

    raw = pd.read_csv(f)

    # Median and interquartile range across repetitions.
    def summarize(data, group_cols):
        return (
            data.groupby(group_cols)["seconds"]
            .agg(
                median_seconds="median",
                q25=lambda x: x.quantile(0.25),
                q75=lambda x: x.quantile(0.75),
                min_seconds="min",
                max_seconds="max",
                std_seconds="std"
            )
            .reset_index()
        )

    # Plot blocking and asynchronous measurements.
    def plot_grouped(data, x, title, filename):
        fig, ax = plt.subplots()

        for mode in ["blocking", "async"]:
            subset = data[data["mode"] == mode].sort_values(x)

            if subset.empty:
                continue

            median = subset["median_seconds"].to_numpy()
            lower = median - subset["q25"].to_numpy()
            upper = subset["q75"].to_numpy() - median

            ax.errorbar(
                subset[x],
                median,
                yerr=[lower, upper],
                marker="o",
                capsize=4,
                label=mode
            )

        ax.set(
            xlabel=x,
            ylabel="Median total execution time (seconds)",
            title=title
        )
        ax.legend()
        ax.grid(True, alpha=0.3)
        save(fig, filename)

    # 1. Latency sweep
    latency_data = raw[raw["sweep"] == "latency"]

    latency_summary = summarize(
        latency_data, ["latency_ms", "mode"]
    )

    latency_summary.to_csv(
        os.path.join(BASE, "d5_latency_analysis.csv"),
        index=False
    )

    plot_grouped(
        latency_summary,
        "latency_ms",
        "D5: Effect of I/O latency",
        "d5_latency_sweep.png"
    )

    # 2. Snapshot frequency sweep
    frequency_data = raw[raw["sweep"] == "frequency"]

    frequency_summary = summarize(
        frequency_data, ["K", "mode"]
    )

    frequency_summary.to_csv(
        os.path.join(BASE, "d5_frequency_analysis.csv"),
        index=False
    )

    plot_grouped(
        frequency_summary,
        "K",
        "D5: Effect of snapshot frequency",
        "d5_snapshot_frequency.png"
    )

    # 3. Asynchronous queue capacity sweep
    queue_data = raw[raw["sweep"] == "queue"]

    queue_summary = summarize(queue_data, ["Q"])

    queue_summary.to_csv(
        os.path.join(BASE, "d5_queue_analysis.csv"),
        index=False
    )

    fig, ax = plt.subplots()

    median = queue_summary["median_seconds"].to_numpy()
    lower = median - queue_summary["q25"].to_numpy()
    upper = queue_summary["q75"].to_numpy() - median

    ax.errorbar(
        queue_summary["Q"],
        median,
        yerr=[lower, upper],
        marker="o",
        capsize=4
    )

    ax.set(
        xlabel="Maximum pending asynchronous writes (Q)",
        ylabel="Median total execution time (seconds)",
        title="D5: Effect of asynchronous queue capacity"
    )
    ax.grid(True, alpha=0.3)
    save(fig, "d5_queue_capacity.png")

    # 4. Estimate how much blocking I/O overhead is hidden.
    baseline_rows = raw[raw["sweep"] == "baseline"]["seconds"]
    baseline = baseline_rows.median()

    hidden_rows = []

    for latency in sorted(latency_data["latency_ms"].unique()):
        blocking = latency_data[
            (latency_data["latency_ms"] == latency) &
            (latency_data["mode"] == "blocking")
        ]["seconds"].median()

        asynchronous = latency_data[
            (latency_data["latency_ms"] == latency) &
            (latency_data["mode"] == "async")
        ]["seconds"].median()

        blocking_overhead = blocking - baseline

        if blocking_overhead > 0:
            hidden_percent = (
                (blocking - asynchronous) / blocking_overhead
            ) * 100
        else:
            hidden_percent = float("nan")

        hidden_rows.append({
            "latency_ms": latency,
            "baseline_seconds": baseline,
            "blocking_median_seconds": blocking,
            "async_median_seconds": asynchronous,
            "blocking_io_overhead_seconds": blocking_overhead,
            "estimated_hidden_percent": hidden_percent
        })

    hidden_df = pd.DataFrame(hidden_rows)

    hidden_df.to_csv(
        os.path.join(BASE, "d5_async_hidden_time.csv"),
        index=False
    )

    print("\nD5 baseline median:", baseline)
    print("\nEstimated asynchronous I/O hiding:")
    print(hidden_df.to_string(index=False))

    print("\nD5 analysis CSVs and plots generated.")


# --------------------------------------------------
# Main
# --------------------------------------------------

if __name__ == "__main__":
    for fn in (d1, d2, d3, d4, d5):
        fn()

    print("Plots and analysis CSVs saved in", BASE)
