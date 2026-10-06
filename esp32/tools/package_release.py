#!/usr/bin/env python3
"""Package only credential-free public build artifacts; never NVS, ELF or sdkconfig."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", args.version):
        parser.error("Use a semantic version, e.g. 0.1.0")
    root = Path(__file__).resolve().parents[1]
    build = root / "build-rig-puppy-release"
    config = dict(line.split("=", 1) for line in (build / "sdkconfig").read_text().splitlines() if line.startswith("CONFIG_") and "=" in line)
    for key in ["GADGET_SDK_TOKEN", "HOMEHUB_WIFI_SSID", "HOMEHUB_WIFI_PASSWORD", "RIG_HTTP_PROXY_HOST"]:
        if config.get("CONFIG_" + key, '""') != '""':
            raise RuntimeError("Public packaging refused: " + key + " is not empty")
    for key in ["SECURE_BOOT", "SECURE_FLASH_ENC_ENABLED", "HOMEHUB_PAIRING_EFUSE_AUTH", "HOMEHUB_OTA_ENABLED"]:
        if config.get("CONFIG_" + key) == "y":
            raise RuntimeError("Public packaging refused: " + key + " is enabled")
    if config.get("CONFIG_RIG_HTTP_PROXY") != "y":
        raise RuntimeError("Public image must include optional runtime proxy support")
    out = root.parent / "dist" / ("rig-muse-puppy-" + args.version)
    out.mkdir(parents=True, exist_ok=True)
    files = [("0x0", "bootloader/bootloader.bin", "bootloader.bin"),
             ("0x10000", "partition_table/partition-table.bin", "partition-table.bin"),
             ("0x17000", "ota_data_initial.bin", "ota_data_initial.bin"),
             ("0x19000", "phy_init_data.bin", "phy_init_data.bin"),
             ("0x20000", "muse-gadget.bin", "muse-gadget.bin")]
    hashes = {}
    for offset, source, target in files:
        data = (build / source).read_bytes()
        if re.search(rb'mgst_[A-Za-z0-9_-]{43}', data) or re.search(rb'/(?:Users|home)/[A-Za-z0-9._-]+/', data):
            raise RuntimeError("Public packaging refused: credential or personal build data in " + target)
        (out / target).write_bytes(data)
        hashes[target] = hashlib.sha256(data).hexdigest()
    for source, target in [(root / "tools/provision_rig.py", "provision_rig.py"),
                           (root / "tools/flash_release.py", "flash.py"),
                           (root.parent / "docs/FLASHING.md", "FLASHING.md"),
                           (root.parent / "LICENSE", "LICENSE")]:
        shutil.copyfile(source, out / target)
        hashes[target] = hashlib.sha256(source.read_bytes()).hexdigest()
    manifest = {"version": args.version, "board": "RIG-Puppy", "target": "esp32s3", "flash_mb": 16,
                "esp_idf": "6.0.1", "sdk_token_embedded": False, "wifi_credentials_embedded": False,
                "default_network_mode": "direct", "runtime_http_proxy_supported": True,
                "calibration_address": "0xfff000", "flash_files": [[o, t] for o, s, t in files], "sha256": hashes}
    manifest["source_commit"] = subprocess.check_output(["git", "-C", str(root.parent), "rev-parse", "HEAD"], text=True).strip()
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (out / "SHA256SUMS").write_text("".join(v + "  " + k + "\n" for k, v in sorted(hashes.items())))
    archive = out.parent / (out.name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(out.iterdir()):
            z.write(f, out.name + "/" + f.name)
    print("Public firmware: " + str(archive))
    print("Defaults: no token, no Wi-Fi credentials, direct network; optional proxy configured over USB")


if __name__ == "__main__":
    main()
