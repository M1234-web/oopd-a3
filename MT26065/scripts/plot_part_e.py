import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

BASE = "results/partE"
RAW_FILE = os.path.join(BASE, "raw_timeseries.csv")

os.makedirs(BASE, exist_ok=True)

df = pd.read_csv(RAW_FILE)

COUNT_COLUMNS = ["S", "I", "R", "V", "D"]

for col in COUNT_COLUMNS:
    df[col] = pd.to_numeric(df[col])

df["day"] = pd.to_numeric(df["day"])
df["seed"] = pd.to_numeric(df["seed"])

POPULATION = 500 * 500
INITIAL_INFECTED = 10


def save(fig, filename):
    fig.tight_layout()
    fig.savefig(os.path.join(BASE, filename), dpi=160)
    plt.close(fig)


def get_setting_data(experiment, setting):
    return df[
        (df["experiment"] == experiment) &
        (df["setting"] == setting)
    ].copy()


def complete_trajectories(data):
    """
    Extend each seed's trajectory after extinction by carrying
    forward its final compartment counts.

    This allows daily averages to include all seeds.
    """
    trajectories = []

    for seed, group in data.groupby("seed"):
        group = group.sort_values("day").set_index("day")

        last_day = int(group.index.max())

        full_days = range(last_day + 1)
        group = group.reindex(full_days).ffill()

        group["seed"] = seed
        trajectories.append(group.reset_index(names="day"))

    return pd.concat(trajectories, ignore_index=True)


def mean_curve(experiment, setting):
    data = get_setting_data(experiment, setting)
    data = complete_trajectories(data)

    for col in COUNT_COLUMNS:
        data[col] = data[col] / POPULATION

    summary = data.groupby("day")[COUNT_COLUMNS].agg(
        ["mean", "std"]
    )

    return summary


def plot_curve(experiment, setting, title, filename):
    summary = mean_curve(experiment, setting)

    fig, ax = plt.subplots(figsize=(9, 5))

    for state in ["S", "I", "R", "D"]:
        mean = summary[(state, "mean")]
        std = summary[(state, "std")].fillna(0)

        ax.plot(mean.index, mean.values, label=state)
        ax.fill_between(
            mean.index,
            np.maximum(0, mean.values - std.values),
            np.minimum(1, mean.values + std.values),
            alpha=0.15
        )

    ax.set_xlabel("Day")
    ax.set_ylabel("Population fraction")
    ax.set_title(title)
    ax.legend()
    ax.grid(alpha=0.3)

    save(fig, filename)


def run_metrics(experiment, setting):
    data = get_setting_data(experiment, setting)

    rows = []

    for seed, group in data.groupby("seed"):
        group = group.sort_values("day")

        peak_index = group["I"].idxmax()
        peak_row = group.loc[peak_index]
        final_row = group.iloc[-1]

        peak_fraction = peak_row["I"] / POPULATION

        # Total ever infected = recovered + currently infected + deaths.
        # Vaccinated individuals are not counted as infected.
        attack_rate = (
            final_row["R"] +
            final_row["I"] +
            final_row["D"]
        ) / POPULATION

        rows.append({
            "seed": seed,
            "peak_fraction": peak_fraction,
            "peak_day": int(peak_row["day"]),
            "attack_rate": attack_rate,
            "duration": int(final_row["day"]),
            "extinct": int(final_row["I"] == 0)
        })

    return pd.DataFrame(rows)


def summarize_experiment(experiment):
    settings = sorted(
        df[df["experiment"] == experiment]["setting"].unique()
    )

    rows = []

    for setting in settings:
        metrics = run_metrics(experiment, setting)

        rows.append({
            "experiment": experiment,
            "setting": setting,
            "mean_peak_fraction": metrics["peak_fraction"].mean(),
            "std_peak_fraction": metrics["peak_fraction"].std(),
            "mean_peak_day": metrics["peak_day"].mean(),
            "std_peak_day": metrics["peak_day"].std(),
            "mean_attack_rate": metrics["attack_rate"].mean(),
            "std_attack_rate": metrics["attack_rate"].std(),
            "mean_duration": metrics["duration"].mean(),
            "std_duration": metrics["duration"].std(),
            "extinction_fraction": metrics["extinct"].mean()
        })

    return pd.DataFrame(rows)


# ---------------------------------------------------------
# E1. Default S, I, R, D curves
# ---------------------------------------------------------

plot_curve(
    "E1",
    "default",
    "E1: Default epidemic dynamics (mean +/- SD)",
    "e1_default_curves.png"
)


# ---------------------------------------------------------
# E2. Beta sweep
# ---------------------------------------------------------

e2 = summarize_experiment("E2")
e2["beta"] = e2["setting"].str.replace("beta_", "", regex=False).astype(float)
e2 = e2.sort_values("beta")

e2.to_csv(os.path.join(BASE, "e2_beta_summary.csv"), index=False)

fig, ax = plt.subplots(figsize=(8, 5))

ax.errorbar(
    e2["beta"],
    e2["mean_attack_rate"],
    yerr=e2["std_attack_rate"].fillna(0),
    marker="o",
    capsize=4
)

ax.set_xlabel("Transmission probability beta")
ax.set_ylabel("Final attack rate")
ax.set_title("E2: Effect of transmission probability")
ax.grid(alpha=0.3)

save(fig, "e2_beta_sweep.png")


# ---------------------------------------------------------
# E3. Vaccination sweep
# ---------------------------------------------------------

e3 = summarize_experiment("E3")
e3["vaccination"] = (
    e3["setting"].str.replace("v_", "", regex=False).astype(float)
)
e3 = e3.sort_values("vaccination")

e3.to_csv(os.path.join(BASE, "e3_vaccination_summary.csv"), index=False)

fig, ax = plt.subplots(figsize=(8, 5))

ax.errorbar(
    e3["vaccination"],
    e3["mean_attack_rate"],
    yerr=e3["std_attack_rate"].fillna(0),
    marker="o",
    capsize=4
)

ax.set_xlabel("Vaccination fraction")
ax.set_ylabel("Final attack rate")
ax.set_title("E3: Effect of vaccination")
ax.grid(alpha=0.3)

save(fig, "e3_vaccination_sweep.png")


# ---------------------------------------------------------
# E4. Masks and lockdown
# ---------------------------------------------------------

e4 = summarize_experiment("E4")
e4.to_csv(os.path.join(BASE, "e4_intervention_summary.csv"), index=False)

fig, ax = plt.subplots(figsize=(9, 5))

x = np.arange(len(e4))
ax.bar(
    x,
    e4["mean_peak_fraction"],
    yerr=e4["std_peak_fraction"].fillna(0),
    capsize=4
)

ax.set_xticks(x)
ax.set_xticklabels(e4["setting"], rotation=15)
ax.set_ylabel("Peak infected fraction")
ax.set_title("E4: Intervention effect on peak height")
ax.grid(axis="y", alpha=0.3)

save(fig, "e4_peak_height.png")


fig, ax = plt.subplots(figsize=(9, 5))

ax.errorbar(
    e4["setting"],
    e4["mean_peak_day"],
    yerr=e4["std_peak_day"].fillna(0),
    marker="o",
    capsize=4
)

ax.set_ylabel("Day of peak infection")
ax.set_title("E4: Intervention effect on peak timing")
ax.grid(alpha=0.3)

save(fig, "e4_peak_timing.png")


fig, ax = plt.subplots(figsize=(9, 5))

for setting in ["baseline", "masks", "lockdown", "masks_lockdown"]:
    summary = mean_curve("E4", setting)
    ax.plot(
        summary.index,
        summary[("I", "mean")],
        label=setting
    )

ax.set_xlabel("Day")
ax.set_ylabel("Infected population fraction")
ax.set_title("E4: Infected trajectories under interventions")
ax.legend()
ax.grid(alpha=0.3)

save(fig, "e4_intervention_curves.png")


# ---------------------------------------------------------
# E5. Standard SIR/SIRD model using RK4
# ---------------------------------------------------------

def sir_rhs(y, beta, gamma, delta):
    S, I, R, D = y

    dS = -beta * S * I
    dI = beta * S * I - (gamma + delta) * I
    dR = gamma * I
    dD = delta * I

    return np.array([dS, dI, dR, dD])


def solve_sir(
    beta,
    mu,
    infectious_days,
    initial_infected,
    population,
    max_days,
    dt=0.1
):
    S0 = (population - initial_infected) / population
    I0 = initial_infected / population

    # Approximate the grid's fixed infectious duration with
    # continuous recovery and death rates.
    gamma = (1.0 - mu) / infectious_days
    delta = mu / infectious_days

    # Eight neighbouring cells can expose a susceptible cell.
    # This is a mean-field approximation, not an exact equivalence.
    sir_beta = 8.0 * beta

    y = np.array([S0, I0, 0.0, 0.0])

    times = [0.0]
    values = [y.copy()]

    t = 0.0

    while t < max_days and y[1] > 1e-10:
        k1 = sir_rhs(y, sir_beta, gamma, delta)
        k2 = sir_rhs(y + dt * k1 / 2, sir_beta, gamma, delta)
        k3 = sir_rhs(y + dt * k2 / 2, sir_beta, gamma, delta)
        k4 = sir_rhs(y + dt * k3, sir_beta, gamma, delta)

        y = y + (dt / 6) * (k1 + 2*k2 + 2*k3 + k4)
        y = np.maximum(y, 0)

        t += dt

        if abs(t - round(t)) < dt / 2:
            times.append(round(t))
            values.append(y.copy())

    return np.array(times), np.array(values)


grid = complete_trajectories(get_setting_data("E1", "default"))

for col in COUNT_COLUMNS:
    grid[col] = grid[col] / POPULATION

grid_mean = grid.groupby("day")[COUNT_COLUMNS].mean()

sir_times, sir_values = solve_sir(
    beta=0.08,
    mu=0.01,
    infectious_days=7,
    initial_infected=INITIAL_INFECTED,
    population=POPULATION,
    max_days=int(grid_mean.index.max())
)

fig, ax = plt.subplots(figsize=(10, 6))

for state in ["S", "I", "R", "D"]:
    ax.plot(
        grid_mean.index,
        grid_mean[state],
        label=f"Grid {state}"
    )

sir_labels = ["SIR S", "SIR I", "SIR R", "SIR D"]

for i, label in enumerate(sir_labels):
    ax.plot(
        sir_times,
        sir_values[:, i],
        linestyle="--",
        label=label
    )

ax.set_xlabel("Day")
ax.set_ylabel("Population fraction")
ax.set_title("E5: Grid model versus standard SIRD model")
ax.legend(ncol=2)
ax.grid(alpha=0.3)

save(fig, "e5_grid_vs_sir.png")


# ---------------------------------------------------------
# Final report
# ---------------------------------------------------------

all_summaries = pd.concat([
    summarize_experiment("E1"),
    e2.drop(columns=["beta"]),
    e3.drop(columns=["vaccination"]),
    e4
], ignore_index=True)

all_summaries.to_csv(
    os.path.join(BASE, "all_experiment_summaries.csv"),
    index=False
)

print("\nPart E analysis completed.")
print("Results saved in:", BASE)

print("\nE2 beta summary:")
print(e2[[
    "beta",
    "mean_attack_rate",
    "std_attack_rate",
    "mean_duration"
]].to_string(index=False))

print("\nE3 vaccination summary:")
print(e3[[
    "vaccination",
    "mean_attack_rate",
    "std_attack_rate",
    "mean_peak_fraction"
]].to_string(index=False))

print("\nE4 intervention summary:")
print(e4[[
    "setting",
    "mean_peak_fraction",
    "mean_peak_day",
    "mean_attack_rate"
]].to_string(index=False))
