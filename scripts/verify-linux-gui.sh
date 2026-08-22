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

check_runtime_dependencies() {
    local binary=$1
    local expected_qt_root=$2
    local missing
    if ! missing=$(env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH \
        -u QT_QPA_PLATFORM_PLUGIN_PATH ldd "$binary" 2>&1); then
        printf 'Could not inspect runtime dependencies for %s:\n%s\n' "$binary" "$missing" >&2
        exit 2
    fi
    if grep -Fq 'not found' <<<"$missing"; then
        printf 'Unresolved runtime dependencies for %s:\n%s\n' "$binary" "$missing" >&2
        exit 2
    fi
    local foreign_qt_libraries
    foreign_qt_libraries=
    local qt_library_path
    local resolved_qt_library
    while IFS= read -r qt_library_path; do
        [[ -n $qt_library_path ]] || continue
        if ! resolved_qt_library=$(realpath "$qt_library_path" 2>/dev/null) \
            || [[ $resolved_qt_library != "$expected_qt_root/"* ]]; then
            foreign_qt_libraries+="${foreign_qt_libraries:+$'\n'}$qt_library_path"
        fi
    done < <(awk '$1 ~ /^libQt6/ && $2 == "=>" { print $3 }' <<<"$missing")
    if [[ -n $foreign_qt_libraries ]]; then
        printf '%s resolved Qt outside %s:\n%s\n' \
            "$binary" "$expected_qt_root" "$foreign_qt_libraries" >&2
        exit 2
    fi
}

: "${CIMBARPUNK_QT_ROOT:?CIMBARPUNK_QT_ROOT must be set to an absolute Qt 6.8.4 prefix}"
: "${VCPKG_ROOT:?VCPKG_ROOT must be set to an absolute vcpkg checkout}"
: "${DISPLAY:?DISPLAY must refer to an existing X11 display}"
require_absolute_dir CIMBARPUNK_QT_ROOT "$CIMBARPUNK_QT_ROOT"
require_absolute_dir VCPKG_ROOT "$VCPKG_ROOT"
CIMBARPUNK_QT_ROOT=$(realpath "$CIMBARPUNK_QT_ROOT")
VCPKG_ROOT=$(realpath "$VCPKG_ROOT")
export CIMBARPUNK_QT_ROOT VCPKG_ROOT

for tool in ctest dbus-send grep ldd timeout xdpyinfo xwininfo; do
    command -v "$tool" >/dev/null || { printf '%s was not found in PATH\n' "$tool" >&2; exit 2; }
done
xdpyinfo -display "$DISPLAY" >/dev/null 2>&1 || {
    printf 'DISPLAY is not a reachable X11 display: %s\n' "$DISPLAY" >&2
    exit 2
}
: "${DBUS_SESSION_BUS_ADDRESS:?A session D-Bus address is required}"
dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply \
    / org.freedesktop.DBus.ListNames >/dev/null 2>&1 || {
    printf 'The session D-Bus is not reachable\n' >&2
    exit 2
}

qtpaths="$CIMBARPUNK_QT_ROOT/bin/qtpaths"
[[ -x $qtpaths ]] || qtpaths="$CIMBARPUNK_QT_ROOT/bin/qtpaths6"
[[ -x $qtpaths ]] || {
    printf 'qtpaths was not found under %s/bin\n' "$CIMBARPUNK_QT_ROOT" >&2
    exit 2
}
qt_version=$("$qtpaths" --qt-version)
[[ $qt_version == 6.8.4 ]] || {
    printf 'Exact Qt 6.8.4 required; found %s\n' "$qt_version" >&2
    exit 2
}

required_plugins=(
    "$CIMBARPUNK_QT_ROOT/plugins/platforms/libqxcb.so"
    "$CIMBARPUNK_QT_ROOT/plugins/iconengines/libqsvgicon.so"
    "$CIMBARPUNK_QT_ROOT/plugins/multimedia/libffmpegmediaplugin.so"
)
for plugin in "${required_plugins[@]}"; do
    [[ -f $plugin ]] || { printf 'Required Qt GUI plugin is missing: %s\n' "$plugin" >&2; exit 2; }
    check_runtime_dependencies "$plugin" "$CIMBARPUNK_QT_ROOT/lib"
done

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
repository_root=$(cd -- "$script_dir/.." && pwd -P)
installed_app="$repository_root/out/install/linux-release/bin/cimbarpunk"
installed_root="$repository_root/out/install/linux-release"
[[ -x $installed_app ]] || {
    printf 'Installed application is missing or not executable: %s\n' "$installed_app" >&2
    exit 2
}
installed_plugins=(
    "$installed_root/plugins/platforms/libqxcb.so"
    "$installed_root/plugins/iconengines/libqsvgicon.so"
    "$installed_root/plugins/multimedia/libffmpegmediaplugin.so"
)
for plugin in "${installed_plugins[@]}"; do
    [[ -f $plugin ]] || { printf 'Required installed Qt GUI plugin is missing: %s\n' "$plugin" >&2; exit 2; }
    check_runtime_dependencies "$plugin" "$installed_root/lib"
done
check_runtime_dependencies "$installed_app" "$installed_root/lib"

sdk_library_path="$CIMBARPUNK_QT_ROOT/lib"
sdk_plugin_path="$CIMBARPUNK_QT_ROOT/plugins"

cd "$repository_root"
timeout --signal=TERM --kill-after=5s 75s \
    env -u QML2_IMPORT_PATH -u QML_IMPORT_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
        LD_LIBRARY_PATH="$sdk_library_path" QT_PLUGIN_PATH="$sdk_plugin_path" \
        QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
        ctest --preset linux-release -L linux_gui --output-on-failure

launch_log=$(mktemp "${TMPDIR:-/tmp}/cimbarpunk-linux-gui-launch.XXXXXX")
cleanup_launch_log() {
    rm -f -- "$launch_log"
}
trap cleanup_launch_log EXIT
set +e
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
    -u QML2_IMPORT_PATH -u QML_IMPORT_PATH \
    QT_DEBUG_PLUGINS=1 QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
    timeout --signal=TERM --kill-after=5s 8s "$installed_app" \
    >"$launch_log" 2>&1
launch_status=$?
set -e
if [[ $launch_status -ne 124 ]]; then
    printf 'Installed tray application did not remain active for the bounded check: %s\n' \
        "$launch_status" >&2
    cat "$launch_log" >&2
    exit 1
fi
loaded_xcb_record="qt.core.library: \"$installed_root/plugins/platforms/libqxcb.so\" loaded library"
if ! grep -Fq "$loaded_xcb_record" "$launch_log"; then
    printf 'Installed application did not load its packaged XCB plugin\n' >&2
    cat "$launch_log" >&2
    exit 1
fi
if grep -Fq "$CIMBARPUNK_QT_ROOT" "$launch_log"; then
    printf 'Installed application loaded a plugin from the development Qt prefix\n' >&2
    cat "$launch_log" >&2
    exit 1
fi
