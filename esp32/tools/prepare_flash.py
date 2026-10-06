#!/usr/bin/env python3
"""Prepare a local source build for the calibration-checking Puppy installer.

Output stays inside the ignored build directory. Personal configuration may be
embedded in these images; this tool does not create or upload a release.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', default='build-rig-puppy', help='Build directory relative to esp32/ or an absolute path')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = Path(args.build_dir)
    if not build.is_absolute():
        build = root / build
    config = dict(line.split('=', 1) for line in (build / 'sdkconfig').read_text().splitlines() if line.startswith('CONFIG_') and '=' in line)
    if config.get('CONFIG_RIG_PUPPY') != 'y' or config.get('CONFIG_ESPTOOLPY_FLASHSIZE') != '"16MB"':
        raise RuntimeError('Expected a 16 MB RIG-Puppy source build')
    for key in ['SECURE_BOOT', 'SECURE_FLASH_ENC_ENABLED', 'HOMEHUB_PAIRING_EFUSE_AUTH']:
        if config.get('CONFIG_' + key) == 'y':
            raise RuntimeError('Local installer refuses irreversible security provisioning: ' + key)
    out = build / 'flash-rig'
    out.mkdir(parents=True, exist_ok=True)
    files = [('0x0', 'bootloader/bootloader.bin', 'bootloader.bin'),
             ('0x10000', 'partition_table/partition-table.bin', 'partition-table.bin'),
             ('0x17000', 'ota_data_initial.bin', 'ota_data_initial.bin'),
             ('0x19000', 'phy_init_data.bin', 'phy_init_data.bin'),
             ('0x20000', 'muse-gadget.bin', 'muse-gadget.bin')]
    hashes = {}
    for offset, source, target in files:
        data = (build / source).read_bytes()
        (out / target).write_bytes(data)
        hashes[target] = hashlib.sha256(data).hexdigest()
    shutil.copyfile(root / 'tools/flash_rig.py', out / 'flash.py')
    hashes['flash.py'] = hashlib.sha256((out / 'flash.py').read_bytes()).hexdigest()
    description = json.loads((build / 'project_description.json').read_text())
    manifest = {'board': 'RIG-Puppy', 'target': 'esp32s3', 'flash_mb': 16,
                'version': description.get('project_version'), 'esp_idf': description.get('idf_ver'),
                'calibration_address': '0xfff000',
                'flash_files': [[o, t] for o, s, t in files], 'sha256': hashes,
                'source_commit': subprocess.check_output(['git', '-C', str(root.parent), 'rev-parse', 'HEAD'], text=True).strip(),
                'local_build_only': True}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Local flash images prepared: ' + str(out))
    print('Images may contain your local credentials. Keep this directory private.')


if __name__ == '__main__':
    main()
