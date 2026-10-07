"""PyBaMM simulation front-end: clean multi-channel cell recordings.

Each :class:`~battery_dataset.config.RunSpec` is solved as a single-particle
model with a lumped thermal submodel, so every recording carries terminal
voltage, applied current and cell temperature. Solutions are resampled onto a
fixed-length uniform index grid, giving a rectangular dataset tensor regardless
of how the adaptive DAE solver allocated its own time steps.
"""

from __future__ import annotations

import numpy as np
import pybamm

from .config import CHANNELS, PROFILE_INITIAL_SOC, DatasetConfig, RunSpec

#: Voltage margin kept away from the parameter-set cut-offs so an experiment
#: step always terminates on a physical limit rather than solver failure.
LOWER_MARGIN_V = 0.10
UPPER_MARGIN_V = 0.05

#: CC-CV hold termination current.
HOLD_CUTOFF_A = 50e-3


def build_experiment(profile: str, c_rate: float, lower_v: float, upper_v: float) -> pybamm.Experiment:
    """Return the PyBaMM experiment for ``profile`` within a cell's voltage window."""
    c = f"{c_rate:g}"
    discharge = f"Discharge at {c}C until {lower_v + LOWER_MARGIN_V:.2f} V"
    charge = f"Charge at {c}C until {upper_v - UPPER_MARGIN_V:.2f} V"
    hold = f"Hold at {upper_v - UPPER_MARGIN_V:.2f} V until {HOLD_CUTOFF_A * 1e3:.0f} mA"

    steps = {
        "discharge": ["Rest for 5 minutes", discharge, "Rest for 10 minutes"],
        "cccv": [charge, hold, "Rest for 10 minutes"],
    }[profile]
    return pybamm.Experiment(steps)


def simulate_run(spec: RunSpec, samples_per_run: int, model_name: str = "SPM") -> np.ndarray:
    """Solve one run and return a ``(samples_per_run, n_channels)`` float64 matrix."""
    parameter_values = pybamm.ParameterValues(spec.parameter_set)
    lower_v = float(parameter_values["Lower voltage cut-off [V]"])
    upper_v = float(parameter_values["Upper voltage cut-off [V]"])

    # An explicit initial state removes the dependence on each parameter set's
    # own default state of charge: a discharge must start full, a CC-CV charge
    # must start empty, otherwise the first experiment step is infeasible.
    parameter_values.set_initial_state(PROFILE_INITIAL_SOC[spec.profile])

    ambient_k = 273.15 + spec.ambient_c
    parameter_values.update(
        {"Ambient temperature [K]": ambient_k, "Initial temperature [K]": ambient_k}
    )

    model = getattr(pybamm.lithium_ion, model_name)({"thermal": "lumped"})
    experiment = build_experiment(spec.profile, spec.c_rate, lower_v, upper_v)
    solution = pybamm.Simulation(model, parameter_values=parameter_values, experiment=experiment).solve()

    if getattr(solution, "t", None) is None or len(solution.t) == 0:
        raise RuntimeError("solver returned an empty solution")

    time_s = np.asarray(solution["Time [s]"].entries, dtype=np.float64)
    signals = np.column_stack(
        [
            time_s,
            np.asarray(solution["Current [A]"].entries, dtype=np.float64),
            np.asarray(solution["Terminal voltage [V]"].entries, dtype=np.float64),
            np.asarray(solution["X-averaged cell temperature [K]"].entries, dtype=np.float64),
        ]
    )
    return _resample_uniform(signals, samples_per_run)


def _resample_uniform(signals: np.ndarray, n_samples: int) -> np.ndarray:
    """Linear-interpolate an adaptive solution onto a uniform index grid.

    The solver emits few, unevenly spaced points; the service consumes fixed
    length blocks, so every run is stretched onto ``n_samples`` rows spanning
    its own duration. Time therefore stays absolute seconds but is no longer
    uniformly spaced between runs.
    """
    raw_time = signals[:, 0]
    grid = np.linspace(raw_time[0], raw_time[-1], n_samples)
    out = np.empty((n_samples, signals.shape[1]), dtype=np.float64)
    out[:, 0] = grid
    for j in range(1, signals.shape[1]):
        out[:, j] = np.interp(grid, raw_time, signals[:, j])
    return out


def generate_clean_dataset(
    config: DatasetConfig,
    runs: list[RunSpec],
    verbose: bool = True,
) -> tuple[np.ndarray, np.ndarray, list[RunSpec]]:
    """Solve every run, returning ``(clean, duration_s, completed_runs)``.

    ``clean`` is ``(n_runs, samples_per_run, n_channels)`` float32. Runs whose
    solve fails are skipped; the returned run list matches the first axis.
    """
    n_channels = len(CHANNELS)
    blocks: list[np.ndarray] = []
    durations: list[float] = []
    completed: list[RunSpec] = []

    for index, spec in enumerate(runs, start=1):
        try:
            record = simulate_run(spec, config.samples_per_run, config.model)
        except Exception as exc:  # a single bad sweep point must not lose the build
            if verbose:
                print(f"[{index}/{len(runs)}] SKIP {spec.run_id}: {type(exc).__name__}: {exc}")
            continue

        blocks.append(record)
        durations.append(float(record[-1, 0]))
        completed.append(spec)
        if verbose:
            print(
                f"[{index}/{len(runs)}] {spec.run_id:<44} "
                f"dur={record[-1, 0]:8.0f}s  V=[{record[:, 2].min():.3f},{record[:, 2].max():.3f}]  "
                f"T=[{record[:, 3].min():.1f},{record[:, 3].max():.1f}]K",
                flush=True,
            )

    if not blocks:
        raise RuntimeError("every simulation failed; nothing to write")

    clean = np.stack(blocks).astype(np.float32)
    duration_s = np.asarray(durations, dtype=np.float32)
    assert clean.shape[1:] == (config.samples_per_run, n_channels)
    return clean, duration_s, completed
