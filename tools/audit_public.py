#!/usr/bin/env python3
"""Scan publishable files/history without displaying credential values."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = {
    "SDK token": rb"mgst_[A-Za-z0-9_-]{43}",
    "GitHub token": rb"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{30,})",
    "API key": rb"sk-[A-Za-z0-9_-]{20,}",
    "AWS access key": rb"AKIA[0-9A-Z]{16}",
    "private key": rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
    "personal home path": rb"/(?:Users|home)/[A-Za-z0-9._-]+/",
}
DEV_KEY_SHA = "e57311281ea0478a80be57036768ef71481804eab73182b2b70255311a2ded6a"


def findings(path, data):
    hits = []
    for kind, pattern in PATTERNS.items():
        for match in re.finditer(pattern, data):
            if kind == "private key" and path == "esp32/dev_signing_key.pem" and hashlib.sha256(data).hexdigest() == DEV_KEY_SHA:
                continue
            if kind == "SDK token" and path.startswith("esp32/tests/") and match.group() == b"mgst_" + b"A" * 43:
                continue
            if kind == "personal home path" and path == "linux/tests/test_link_client.py" and match.group() == b"/home/" + b"pi/":
                continue  # Upstream fake error/command fixture, not a developer's home.
            hits.append((kind, data[:match.start()].count(b"\n") + 1))
    return hits


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true")
    args = parser.parse_args()
    count = bad = 0
    paths = set(p.decode() for p in git("ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0") if p)
    for path in sorted(paths):
        file = ROOT / path
        if not file.is_file():
            continue
        count += 1
        for kind, line in findings(path, file.read_bytes()):
            bad += 1
            print(f"FLAG {path}:{line}: {kind} (value withheld)")
    history_count = 0
    if args.history:
        for row in git("rev-list", "--objects", "--all").decode().splitlines():
            parts = row.split(" ", 1)
            if len(parts) != 2:
                continue
            oid, path = parts
            if git("cat-file", "-t", oid).strip() != b"blob":
                continue
            history_count += 1
            for kind, line in findings(path, git("cat-file", "blob", oid)):
                bad += 1
                print(f"FLAG history {oid[:12]} {path}:{line}: {kind} (value withheld)")
    print(f"Scanned {count} publishable files and {history_count} historical blobs; flags={bad}")
    if bad:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
