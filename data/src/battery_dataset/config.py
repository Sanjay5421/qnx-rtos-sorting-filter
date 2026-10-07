"""Configuration for the synthetic battery-sensor dataset.

The generator produces large float32 time-series blocks that act as the client
payload for the downstream sorting / median-filter service: multi-channel
battery sensor recordings carrying additive Gaussian noise and impulsive
outliers that the median filter is expected to reject.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

#: Signal channels, in storage order. ``time_s`` is the abscissa and is never
#: corrupted; the remaining channels are physical sensor readings.
TIME_CHANNEL = "time_s"
SENSOR_CHANNELS: tuple[str, ...] = ("current_A", "voltage_V", "temperature_K")
CHANNELS: tuple[str, ...] = (TIME_CHANNEL,) + SENSOR_CHANNELS

#: Payload block size used by the service transport (float32 samples).
BLOCK_BYTES = 64 * 1024

DEFAULT_OUTPUT_DIR = Path(__file__).resolve().parents[2] / "artifacts"


@dataclass(frozen=True)
class CellSpec:
    """A PyBaMM parameter set together with the physical cell it describes.

    ``cell`` and ``source`` are taken from the docstrings shipped with PyBaMM
    (``pybamm.parameter_sets.get_docstring``), cross-checked against the bundled
    parameter-set manual. ``nominal_capacity_ah`` is read from the parameter
    values themselves. ``thermal_rise_2c_k`` is measured by this project
    (2C discharge from full, 25 C ambient) and is the criterion that selected
    which sets are used, because the temperature channel must carry a real
    signal rather than only noise.
    """

    parameter_set: str
    cell: str
    source: str
    nominal_capacity_ah: float
    thermal_rise_2c_k: float


#: Selected cells: three physically distinct cells, each with usable thermal
#: dynamics and a complete lumped-thermal parameterisation.
#:
#: Deliberately excluded, with the reason:
#:   * OKane2022, ORegan2022 -- the same LG M50 cell as Chen2020 (would
#:     duplicate a cell, not add spectrum)
#:   * Ecker2015 (0.23 K), Marquis2019 (0.61 K), NCA_Kim2011 (0.01 K) -- thermal
#:     rise at 2C is below or near the 0.06 K sensor noise floor, so their
#:     temperature channel would be noise-dominated
#:   * Prada2013 (LFP), Ramadass2004, Xu2019, MSMR_Example, half-cell sets --
#:     no cell volume / cooling area / heat-transfer coefficient, so the lumped
#:     thermal model cannot be solved
#:   * Sulzer2019 -- lead-acid, not a lithium-ion cell
CELLS: dict[str, CellSpec] = {
    "Chen2020": CellSpec("Chen2020", "LG M50", "Chen et al. (2020)",
                         nominal_capacity_ah=5.000, thermal_rise_2c_k=19.92),
    "Mohtat2020": CellSpec("Mohtat2020", "graphite/NMC532 pouch cell", "Mohtat et al. (2020)",
                           nominal_capacity_ah=5.000, thermal_rise_2c_k=5.80),
    "Ai2020": CellSpec("Ai2020", "Enertech cell", "Ai et al. (2019), Rieger et al. (2016)",
                       nominal_capacity_ah=2.280, thermal_rise_2c_k=7.37),
}

#: Experiment templates, and the state of charge each one starts from.
PROFILES: tuple[str, ...] = ("discharge", "cccv")
PROFILE_INITIAL_SOC: dict[str, float] = {"discharge": 1.0, "cccv": 0.0}


@dataclass(frozen=True)
class NoiseConfig:
    """Additive noise plus impulsive-outlier model applied per sensor channel.

    Gaussian noise emulates sensor electronics; the impulsive component
    emulates dropouts, stuck readings and transient spikes that a rolling
    median filter is designed to remove without smearing the underlying signal.
    """

    #: Gaussian sensor noise standard deviation, in physical units.
    voltage_sigma_v: float = 2.0e-3
    current_sigma_a: float = 8.0e-3
    temperature_sigma_k: float = 6.0e-2

    #: Fraction of samples per sensor channel replaced by an outlier.
    outlier_fraction: float = 0.015

    #: Spike magnitude drawn uniformly, expressed as multiples of sigma.
    spike_sigma_range: tuple[float, float] = (10.0, 40.0)

    #: Probability that a spike becomes a contiguous burst of samples.
    burst_probability: float = 0.25
    burst_max_len: int = 6

    #: Probability that an outlier is a stuck/dropout plateau instead of a spike.
    dropout_probability: float = 0.12
    dropout_max_len: int = 4

    seed: int = 20251007

    @property
    def sigma_by_channel(self) -> dict[str, float]:
        """Gaussian sigma keyed by sensor channel name."""
        return {
            "current_A": self.current_sigma_a,
            "voltage_V": self.voltage_sigma_v,
            "temperature_K": self.temperature_sigma_k,
        }


@dataclass(frozen=True)
class RunSpec:
    """One simulated cell recording: parameter set x C-rate x temperature x profile."""

    parameter_set: str
    c_rate: float
    ambient_c: float
    profile: str

    @property
    def run_id(self) -> str:
        return f"{self.parameter_set}_c{self.c_rate:g}_T{self.ambient_c:g}_{self.profile}"

    @property
    def cell(self) -> CellSpec:
        return CELLS[self.parameter_set]


@dataclass(frozen=True)
class DatasetConfig:
    """Full description of a dataset build; serialised into ``metadata.json``."""

    parameter_sets: tuple[str, ...] = tuple(CELLS)
    c_rates: tuple[float, ...] = (0.5, 1.0, 2.0)
    ambient_c: tuple[float, ...] = (5.0, 25.0, 45.0)
    profiles: tuple[str, ...] = PROFILES
    samples_per_run: int = 60000
    model: str = "SPM"
    noise: NoiseConfig = field(default_factory=NoiseConfig)
    output_dir: Path = DEFAULT_OUTPUT_DIR
    figures_dir: Path | None = None

    def runs(self, limit: int | None = None) -> list[RunSpec]:
        """Cartesian product of the sweep axes, in a stable order."""
        specs = [
            RunSpec(ps, c, t, p)
            for ps in self.parameter_sets
            for c in self.c_rates
            for t in self.ambient_c
            for p in self.profiles
        ]
        return specs if limit is None else specs[:limit]

    @property
    def n_channels(self) -> int:
        return len(CHANNELS)

    def dataset_dir(self) -> Path:
        """Directory holding the payload, its manifest and the CSV rendering."""
        return self.output_dir / "dataset"

    def csv_dir(self) -> Path:
        return self.dataset_dir() / "csv"

    def figure_dir(self) -> Path:
        return self.figures_dir or (self.output_dir / "figures")

    def estimated_bytes(self, n_runs: int) -> int:
        """Payload size if clean float32 + noisy float32 + boolean mask are stored."""
        samples = n_runs * self.samples_per_run * self.n_channels
        return int(samples * (4 + 4 + 1))

    def to_dict(self) -> dict:
        payload = asdict(self)
        payload["output_dir"] = str(self.output_dir)
        payload["figures_dir"] = str(self.figures_dir) if self.figures_dir else None
        payload["channels"] = list(CHANNELS)
        payload["sensor_channels"] = list(SENSOR_CHANNELS)
        payload["block_bytes"] = BLOCK_BYTES
        payload["profile_initial_soc"] = dict(PROFILE_INITIAL_SOC)
        payload["cells"] = {
            name: asdict(spec) for name, spec in CELLS.items() if name in self.parameter_sets
        }
        return payload

    def write(self, path: Path) -> Path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_dict(), indent=2), encoding="utf-8")
        return path
