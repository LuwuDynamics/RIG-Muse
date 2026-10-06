"""Installer refuses unsafe images/updates and preserves calibration, without USB."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class ReleaseInstallerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.folder = Path(self.tmp.name)
        spec = importlib.util.spec_from_file_location("rig_flash_test", ROOT / "tools/flash_release.py")
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.module.__file__ = str(self.folder / "flash.py")
        self.files = [["0x0", "bootloader.bin"], ["0x10000", "partition-table.bin"],
                      ["0x17000", "ota_data_initial.bin"], ["0x19000", "phy_init_data.bin"],
                      ["0x20000", "muse-gadget.bin"]]
        self.manifest = {"board": "RIG-Puppy", "flash_mb": 16, "flash_files": self.files, "sha256": {}}
        for i, (_, name) in enumerate(self.files):
            data = bytes([i]) * 4096
            (self.folder / name).write_bytes(data)
            self.manifest["sha256"][name] = hashlib.sha256(data).hexdigest()
        self.save()
        self.calls = []
        self.wrong_table = self.changed_cal = False

    def tearDown(self):
        self.tmp.cleanup()

    def save(self):
        (self.folder / "manifest.json").write_text(json.dumps(self.manifest))

    def device(self, cmd, **kwargs):
        self.calls.append(cmd)
        if "flash-id" in cmd:
            return subprocess.CompletedProcess(cmd, 0, stdout="Detected flash size: 16MB\n")
        if "read-flash" in cmd:
            offset = cmd[cmd.index("read-flash") + 1]
            file = Path(cmd[-1])
            if offset == "0x10000":
                data = (self.folder / "partition-table.bin").read_bytes()
                file.write_bytes(bytes([9]) * 4096 if self.wrong_table else data)
            else:
                file.write_bytes(bytes([7 if self.changed_cal and "after" in file.name else 6]) * 4096)
        return subprocess.CompletedProcess(cmd, 0)

    def run_installer(self, *args):
        with patch("sys.argv", ["flash.py", "--port", "TEST_PORT", *args]), patch.object(self.module.subprocess, "run", side_effect=self.device):
            self.module.main()

    def test_separate_images_never_write_calibration_or_nvs(self):
        self.run_installer()
        writes = [c for c in self.calls if "write-flash" in c]
        self.assertEqual(len(writes), 1)
        self.assertEqual(writes[0][-10::2], [o for o, f in self.files])
        self.assertFalse(any("erase-flash" in c for c in self.calls))
        self.assertNotIn("0xfff000", writes[0])
        self.assertNotIn("0x11000", writes[0])

    def test_update_refuses_wrong_partition_before_write(self):
        self.wrong_table = True
        with self.assertRaisesRegex(RuntimeError, "partition layout"):
            self.run_installer("--update")
        self.assertFalse(any("write-flash" in c for c in self.calls))

    def test_unsafe_manifest_and_bad_checksum_refused_without_usb(self):
        self.manifest["flash_files"][4][0] = "0xfff000"
        self.save()
        with self.assertRaisesRegex(RuntimeError, "Unsafe"):
            self.run_installer()
        self.assertFalse(self.calls)
        self.manifest["flash_files"][4][0] = "0x20000"
        self.save()
        (self.folder / "muse-gadget.bin").write_bytes(b"corrupted")
        with self.assertRaisesRegex(RuntimeError, "Checksum"):
            self.run_installer()
        self.assertFalse(self.calls)

    def test_calibration_change_reported_and_no_boot_reset(self):
        self.changed_cal = True
        with self.assertRaisesRegex(RuntimeError, "Calibration changed"):
            self.run_installer()
        self.assertFalse(any("hard-reset" in c for c in self.calls))
