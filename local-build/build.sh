#!/bin/bash
# Runs inside the zmk-build-arm container. Mirrors ZMK v0.3's build-user-config.yml:
# every build.yaml entry is built with the same west/cmake arguments as CI.
#   /zmk-config  this repo (read-only)
#   /workspace   west workspace, kept in a Docker volume between runs
#   /out         firmware output folder
set -euo pipefail

git config --global --add safe.directory '*'

# The workspace's manifest is a copy of config/west.yml. Copied only when it changed,
# so west and cmake don't see a new file on every run.
mkdir -p /workspace/config
if ! cmp -s /zmk-config/config/west.yml /workspace/config/west.yml; then
    cp /zmk-config/config/west.yml /workspace/config/west.yml
fi

cd /workspace
if [ ! -d .west ]; then
    west init -l /workspace/config
fi
west update --fetch-opt=--filter=tree:0
west zephyr-export

# One line per build: board|shield|snippet|cmake-args|artifact-name
targets=$(python3 - <<'EOF'
import yaml
with open("/zmk-config/build.yaml") as f:
    m = yaml.safe_load(f) or {}
builds = [{"board": b, "shield": s} for b in m.get("board", []) for s in (m.get("shield") or [None])]
builds += m.get("include", [])
for b in builds:
    print("|".join(str(b.get(k) or "") for k in ("board", "shield", "snippet", "cmake-args", "artifact-name")))
EOF
)

rm -f /out/*.uf2
failed=()
while IFS='|' read -r board shield snippet cmake_args artifact_name; do
    [ -n "$board" ] || continue
    artifact_name="${artifact_name:-${shield:+$shield-}${board}-zmk}"
    echo
    echo "=== Building ${artifact_name} ==="
    # Unquoted on purpose: the optional args expand to nothing, or split like CI's shell does.
    # shellcheck disable=SC2086
    if west build -p auto -s zmk/app -d "build/${artifact_name}" -b "$board" \
        ${snippet:+-S "$snippet"} \
        -- -DZMK_CONFIG=/zmk-config/config ${shield:+-DSHIELD="$shield"} \
        -DZMK_EXTRA_MODULES=/zmk-config $cmake_args; then
        cp "build/${artifact_name}/zephyr/zmk.uf2" "/out/${artifact_name}.uf2"
    else
        failed+=("$artifact_name")
    fi
done <<< "$targets"

echo
if [ ${#failed[@]} -gt 0 ]; then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "All builds succeeded."
