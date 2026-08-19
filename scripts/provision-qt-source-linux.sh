#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    printf 'Usage: %s ABSOLUTE_INSTALL_PREFIX ABSOLUTE_WORK_DIRECTORY\n' "$0" >&2
    exit 2
fi
install_prefix=$1
work_directory=$2
if [[ $install_prefix != /* || $work_directory != /* ]]; then
    printf 'Both paths must be absolute.\n' >&2
    exit 2
fi

archive_name=qt-everywhere-opensource-src-6.8.4.tar.xz
archive_url=https://download.qt.io/official_releases/qt/6.8/6.8.4/single/$archive_name
archive_sha256=1da37a32a583e7856d6fc13357c8ff6ad3ef7b877b8d276713b85026426d5246
archive_path=$work_directory/$archive_name
source_directory=$work_directory/qt-everywhere-src-6.8.4
build_directory=$work_directory/build-release

mkdir -p "$work_directory"
if [[ ! -f $archive_path ]]; then
    curl --fail --location --retry 3 --output "$archive_path" "$archive_url"
fi
printf '%s  %s\n' "$archive_sha256" "$archive_path" | sha256sum --check --strict
if [[ ! -d $source_directory ]]; then
    tar -xJf "$archive_path" -C "$work_directory"
fi
mkdir -p "$build_directory" "$install_prefix"

cd "$build_directory"
"$source_directory/configure" \
    -prefix "$install_prefix" \
    -release \
    -opensource -confirm-license \
    -nomake examples -nomake tests \
    -submodules qtbase,qtdeclarative,qtmultimedia,qtsvg,qttools \
    -- -G Ninja
cmake --build . --parallel
cmake --install .

"$install_prefix/bin/qtpaths" --qt-version
test -f "$install_prefix/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake"
test -f "$install_prefix/plugins/iconengines/libqsvgicon.so"
