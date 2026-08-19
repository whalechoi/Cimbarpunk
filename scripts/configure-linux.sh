#!/usr/bin/env bash
set -euo pipefail

require_absolute_dir() {
    local name=$1
    local value=$2
    if [[ $value != /* ]]; then
        printf '%s must be an absolute path: %s\n' "$name" "$value" >&2
        exit 2
    fi
    if [[ ! -d $value ]]; then
        printf '%s does not exist or is not a directory: %s\n' "$name" "$value" >&2
        exit 2
    fi
}

: "${CIMBARPUNK_QT_ROOT:?CIMBARPUNK_QT_ROOT must be set to an absolute Qt 6.8.4 prefix}"
: "${VCPKG_ROOT:?VCPKG_ROOT must be set to an absolute vcpkg checkout}"
require_absolute_dir CIMBARPUNK_QT_ROOT "$CIMBARPUNK_QT_ROOT"
require_absolute_dir VCPKG_ROOT "$VCPKG_ROOT"
CIMBARPUNK_QT_ROOT=$(realpath "$CIMBARPUNK_QT_ROOT")
VCPKG_ROOT=$(realpath "$VCPKG_ROOT")
export CIMBARPUNK_QT_ROOT VCPKG_ROOT

required_files=(
    "$CIMBARPUNK_QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake"
    "$CIMBARPUNK_QT_ROOT/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake"
    "$CIMBARPUNK_QT_ROOT/plugins/iconengines/libqsvgicon.so"
    "$VCPKG_ROOT/vcpkg"
)
for required in "${required_files[@]}"; do
    if [[ ! -f $required ]]; then
        printf 'Required SDK file was not found: %s\n' "$required" >&2
        printf 'aqt metadata has no Qt 6.8.4 desktop binary; run provision-qt-source-linux.sh.\n' >&2
        exit 2
    fi
done

expected_vcpkg_commit=9e593bb18ea69cc5095e012465dcd675a822ed0d
if ! actual_vcpkg_commit=$(git -C "$VCPKG_ROOT" rev-parse HEAD 2>/dev/null); then
    printf 'VCPKG_ROOT must be the fixed git checkout documented in README.md: %s\n' "$VCPKG_ROOT" >&2
    exit 2
fi
if [[ $actual_vcpkg_commit != "$expected_vcpkg_commit" ]]; then
    printf 'vcpkg commit mismatch: expected %s, found %s\n' \
        "$expected_vcpkg_commit" "$actual_vcpkg_commit" >&2
    exit 2
fi

for tool in cmake ninja g++; do
    command -v "$tool" >/dev/null || { printf '%s was not found in PATH\n' "$tool" >&2; exit 2; }
done
qtpaths="$CIMBARPUNK_QT_ROOT/bin/qtpaths"
[[ -x $qtpaths ]] || qtpaths="$CIMBARPUNK_QT_ROOT/bin/qtpaths6"
[[ -x $qtpaths ]] || { printf 'qtpaths was not found under %s/bin\n' "$CIMBARPUNK_QT_ROOT" >&2; exit 2; }
qt_version=$($qtpaths --qt-version)
[[ $qt_version == 6.8.4 ]] || { printf 'Exact Qt 6.8.4 required; found %s\n' "$qt_version" >&2; exit 2; }
[[ -x $VCPKG_ROOT/vcpkg ]] || { printf 'vcpkg is not executable: %s/vcpkg\n' "$VCPKG_ROOT" >&2; exit 2; }

repository_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
cd "$repository_root"
cmake --fresh --preset linux-release
