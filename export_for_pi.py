"""Export noisy battery sensor readings as raw float32 files for the Pi."""

import pathlib
import numpy as np

# 1. Locate the project folder
root = pathlib.Path(__file__).resolve().parent

# 2. Load the generated dataset
dataset_path = root / "data/artifacts/dataset/battery_sensor_dataset.npz"
dataset = np.load(dataset_path)

noisy = dataset["noisy"]
run_ids = dataset["run_ids"]

# 3. Define the sensor channel names
channel_names = [
    "time",
    "current",
    "voltage",
    "temperature"
]

# 4. Create the output folder
out_dir = root / "pi_data"
out_dir.mkdir(exist_ok=True)

# 5. Export each recording and its sensor channels
manifest_path = out_dir / "manifest.txt"

with open(manifest_path, "w") as manifest:

    for run_index, run_id in enumerate(run_ids):

        # Record the mapping between the filename and original run ID
        manifest.write(f"r{run_index} {run_id}\n")

        # Export each channel into a separate binary file
        for channel_index, channel_name in enumerate(channel_names):

            # Extract all readings for the current channel
            readings = noisy[run_index, :, channel_index]

            # Convert readings to little-endian float32
            readings = readings.astype("<f4")

            # Construct the output filename
            output_path = out_dir / f"r{run_index}_{channel_name}.bin"

            # Write the readings to the binary file
            readings.tofile(output_path)

# 6. Display the number of binary files created
binary_files = list(out_dir.glob("*.bin"))

print("Wrote", len(binary_files), "files to", out_dir)

