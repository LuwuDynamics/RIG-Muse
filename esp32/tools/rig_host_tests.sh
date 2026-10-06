#!/usr/bin/env bash
# Run upstream and RIG host tests. Recent macOS SDKs deprecate sprintf in
# upstream cJSON; suppress that host-only warning without editing vendor code.
set -euo pipefail
cd "$(dirname "$0")/.."
rig_test_dir=$(mktemp -d)
trap 'rm -rf "$rig_test_dir"' EXIT
cat > "$rig_test_dir/rig-cc" <<'EOF'
#!/usr/bin/env bash
exec cc -Wno-deprecated-declarations "$@"
EOF
cat > "$rig_test_dir/rig-cxx" <<'EOF'
#!/usr/bin/env bash
exec c++ -Wno-deprecated-declarations "$@"
EOF
chmod +x "$rig_test_dir/rig-cc" "$rig_test_dir/rig-cxx"
export CC="$rig_test_dir/rig-cc" CXX="$rig_test_dir/rig-cxx"
python3 -m unittest discover -s tests -p 'test_*.py' -v
