#!/usr/bin/env python3
"""Flash separate images without erasing NVS or the Puppy calibration sector."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=230400)
    parser.add_argument("--update", action="store_true", help="App-only update; requires an existing RIG-Muse partition layout")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "manifest.json").read_text())
    expected_files = [["0x0", "bootloader.bin"], ["0x10000", "partition-table.bin"],
                      ["0x17000", "ota_data_initial.bin"], ["0x19000", "phy_init_data.bin"],
                      ["0x20000", "muse-gadget.bin"]]
    if manifest.get("flash_files") != expected_files or manifest.get("board") != "RIG-Puppy" or manifest.get("flash_mb") != 16:
        raise RuntimeError("Unsafe or incompatible flash layout")
    for filename, maximum in [("bootloader.bin", 0x10000), ("partition-table.bin", 0x1000),
                              ("ota_data_initial.bin", 0x2000), ("phy_init_data.bin", 0x1000),
                              ("muse-gadget.bin", 0x200000)]:
        if not 0 < (root / filename).stat().st_size <= maximum:
            raise RuntimeError("Image exceeds its allowed flash region: " + filename)
    for filename, expected in manifest["sha256"].items():
        if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
            raise RuntimeError("Checksum mismatch: " + filename)
    base = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port,
            "--baud", str(args.baud), "--before", "default-reset", "--after", "no-reset"]
    identity = subprocess.run([*base, "flash-id"], capture_output=True, text=True, check=True)
    print(identity.stdout)
    if "Detected flash size: 16MB" not in identity.stdout:
        raise RuntimeError("This release requires a 16 MB RIG-Puppy")
    # Logs remain local; no calibration values or credentials are uploaded.
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp)
        subprocess.run([*base, "read-flash", "0xfff000", "0x1000", str(p / "cal-before.bin")], check=True)
        subprocess.run([*base, "read-flash", "0x10000", "0x1000", str(p / "part-before.bin")], check=True)
        expected_table = (root / "partition-table.bin").read_bytes()
        current_table = (p / "part-before.bin").read_bytes()
        if args.update and current_table[:len(expected_table)] != expected_table:
            raise RuntimeError("App-only update refused: install the RIG-Muse partition layout first")
        files = [("0x20000", "muse-gadget.bin")] if args.update else manifest["flash_files"]
        flash = [*base, "write-flash", "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB"]
        for offset, filename in files:
            flash.extend([offset, str(root / filename)])
        subprocess.run(flash, check=True)
        subprocess.run([*base, "read-flash", "0xfff000", "0x1000", str(p / "cal-after.bin")], check=True)
        if (p / "cal-before.bin").read_bytes() != (p / "cal-after.bin").read_bytes():
            raise RuntimeError("Calibration changed; leave the robot stationary and investigate")
        print("Flash verified. Original calibration is unchanged.")
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port,
                    "--before", "default-reset", "--after", "hard-reset", "read-mac"], check=True)


if __name__ == "__main__":
    main()
