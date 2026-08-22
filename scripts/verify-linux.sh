#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
repository_root=$(cd -- "$script_dir/.." && pwd -P)

"$script_dir/configure-linux.sh"
cd "$repository_root"
cmake --build --preset linux-release --target cimbarpunk cimbarpunk_tests cimbarpunk_frame_player
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    ctest --preset linux-release -LE linux_gui --output-on-failure

installed_root_lexical="$repository_root/out/install/linux-release"
for install_ancestor in \
    "$repository_root/out" \
    "$repository_root/out/install" \
    "$installed_root_lexical"; do
    if [[ -L $install_ancestor ]]; then
        printf 'Refusing symlinked Linux install path component: %s\n' \
            "$install_ancestor" >&2
        exit 1
    fi
done
if ! installed_root=$(realpath -m -- "$installed_root_lexical") \
    || [[ $installed_root != "$installed_root_lexical" ]]; then
    printf 'Refusing Linux install path outside the canonical workspace: %s\n' \
        "$installed_root_lexical" >&2
    exit 1
fi
if [[ -e $installed_root ]]; then
    [[ -d $installed_root && ! -L $installed_root ]] || {
        printf 'Linux install path is not a regular directory: %s\n' "$installed_root" >&2
        exit 1
    }
    rm -rf -- "$installed_root"
fi
cmake --install out/build/linux-release

required_installed_files=(
    "$installed_root/bin/cimbarpunk"
    "$installed_root/bin/qt.conf"
    "$installed_root/plugins/platforms/libqxcb.so"
    "$installed_root/plugins/iconengines/libqsvgicon.so"
    "$installed_root/plugins/multimedia/libffmpegmediaplugin.so"
    "$installed_root/share/cimbarpunk/qml/SelectionOverlay.qml"
    "$installed_root/share/icons/hicolor/scalable/apps/cimbarpunk.svg"
    "$installed_root/share/licenses/cimbarpunk/LICENSE"
    "$installed_root/share/licenses/cimbarpunk/THIRD_PARTY_NOTICES.md"
    "$installed_root/share/licenses/cimbarpunk/libcimbar-MPL-2.0.txt"
)
for required in "${required_installed_files[@]}"; do
    [[ -f $required ]] || { printf 'Required installed file is missing: %s\n' "$required" >&2; exit 1; }
done
[[ -x $installed_root/bin/cimbarpunk ]] || {
    printf 'Installed application is not executable: %s\n' "$installed_root/bin/cimbarpunk" >&2
    exit 1
}
if ! installed_dependencies=$(env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH \
    ldd "$installed_root/bin/cimbarpunk" 2>&1); then
    printf 'Could not inspect installed application dependencies:\n%s\n' \
        "$installed_dependencies" >&2
    exit 1
fi
if grep -Fq 'not found' <<<"$installed_dependencies"; then
    printf 'Installed application has unresolved dependencies:\n%s\n' \
        "$installed_dependencies" >&2
    exit 1
fi
foreign_qt_libraries=
while IFS= read -r qt_library_path; do
    [[ -n $qt_library_path ]] || continue
    if ! resolved_qt_library=$(realpath "$qt_library_path" 2>/dev/null) \
        || [[ $resolved_qt_library != "$installed_root/lib/"* ]]; then
        foreign_qt_libraries+="${foreign_qt_libraries:+$'\n'}$qt_library_path"
    fi
done < <(awk '$1 ~ /^libQt6/ && $2 == "=>" { print $3 }' \
    <<<"$installed_dependencies")
if [[ -n $foreign_qt_libraries ]]; then
    printf 'Installed application resolved Qt outside its package:\n%s\n' \
        "$foreign_qt_libraries" >&2
    exit 1
fi

compare_file() {
    local source=$1
    local installed=$2
    local label=$3
    cmp -s -- "$source" "$installed" || {
        printf 'Installed %s does not match its source: %s\n' "$label" "$installed" >&2
        exit 1
    }
}

tree_manifest() {
    local root=$1
    local pattern=$2
    (
        cd "$root"
        find . -type f -name "$pattern" -print0 \
            | LC_ALL=C sort -z \
            | xargs -0 -r sha256sum
    )
}

vendored_license_manifest() {
    local root=$1
    (
        cd "$root"
        find . -type f \
            \( -name LICENSE -o -name 'LICENSE.*' -o -name COPYING \
                -o -name 'NOTICE*' -o -name base.hpp \) -print0 \
            | LC_ALL=C sort -z \
            | xargs -0 -r sha256sum
    )
}

compare_tree() {
    local source=$1
    local installed=$2
    local pattern=$3
    local label=$4
    [[ -d $source && -d $installed ]] || {
        printf '%s corpus directory is missing: %s or %s\n' \
            "$label" "$source" "$installed" >&2
        exit 1
    }
    local source_manifest
    local installed_manifest
    source_manifest=$(tree_manifest "$source" "$pattern")
    installed_manifest=$(tree_manifest "$installed" "$pattern")
    [[ -n $source_manifest ]] || {
        printf 'Source %s corpus is empty: %s\n' "$label" "$source" >&2
        exit 1
    }
    if [[ $source_manifest != "$installed_manifest" ]]; then
        printf 'Installed %s corpus does not exactly match its source\n' "$label" >&2
        diff -u <(printf '%s\n' "$source_manifest") \
            <(printf '%s\n' "$installed_manifest") >&2 || true
        exit 1
    fi
}

license_root="$installed_root/share/licenses/cimbarpunk"
compare_file "$repository_root/LICENSE" "$license_root/LICENSE" 'project license'
compare_file "$repository_root/THIRD_PARTY_NOTICES.md" \
    "$license_root/THIRD_PARTY_NOTICES.md" 'third-party notices'
compare_file "$repository_root/external/libcimbar/LICENSE" \
    "$license_root/libcimbar-MPL-2.0.txt" 'libcimbar license'
compare_file "$repository_root/resources/icons/tray.svg" \
    "$installed_root/share/icons/hicolor/scalable/apps/cimbarpunk.svg" 'tray icon'
compare_file "$repository_root/src/selection/qml/SelectionOverlay.qml" \
    "$installed_root/share/cimbarpunk/qml/SelectionOverlay.qml" 'selection QML'

compare_tree "$CIMBARPUNK_QT_ROOT/share/licenses/qt" "$license_root/qt" '*' \
    'Qt license'
compare_tree "$CIMBARPUNK_QT_ROOT/sbom" "$license_root/qt-sbom" '*.spdx' \
    'Qt SPDX'
compare_tree "$repository_root/out/build/linux-release/vcpkg_installed/x64-linux/share" \
    "$license_root/vcpkg" copyright 'vcpkg license'

source_vendored="$repository_root/external/libcimbar/src/third_party_lib"
installed_vendored="$license_root/libcimbar-vendored"
source_vendored_manifest=$(vendored_license_manifest "$source_vendored")
installed_vendored_manifest=$(vendored_license_manifest "$installed_vendored")
[[ -n $source_vendored_manifest ]] || {
    printf 'Source libcimbar vendored license corpus is empty\n' >&2
    exit 1
}
if [[ $source_vendored_manifest != "$installed_vendored_manifest" ]]; then
    printf 'Installed libcimbar vendored license corpus does not exactly match its source\n' >&2
    diff -u <(printf '%s\n' "$source_vendored_manifest") \
        <(printf '%s\n' "$installed_vendored_manifest") >&2 || true
    exit 1
fi
