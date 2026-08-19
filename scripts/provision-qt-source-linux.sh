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

install_prefix=$(realpath -m -- "$install_prefix")
work_directory=$(realpath -m -- "$work_directory")

is_same_or_ancestor() {
    local candidate_parent=$1
    local candidate_child=$2
    [[ $candidate_child == "$candidate_parent" || $candidate_child == "$candidate_parent"/* ]]
}

reject_overlap() {
    local first_name=$1
    local first_path=$2
    local second_name=$3
    local second_path=$4
    if is_same_or_ancestor "$first_path" "$second_path" || is_same_or_ancestor "$second_path" "$first_path"; then
        printf '%s and %s must be distinct, non-overlapping paths: %s ; %s\n' \
            "$first_name" "$second_name" "$first_path" "$second_path" >&2
        exit 2
    fi
}

archive_name=qt-everywhere-opensource-src-6.8.4.tar.xz
archive_url=https://download.qt.io/official_releases/qt/6.8/6.8.4/single/$archive_name
archive_sha256=1da37a32a583e7856d6fc13357c8ff6ad3ef7b877b8d276713b85026426d5246
archive_path=$(realpath -m -- "$work_directory/$archive_name")
archive_part=$archive_path.part
source_directory=$(realpath -m -- "$work_directory/qt-everywhere-src-6.8.4")
build_directory=$(realpath -m -- "$work_directory/build-release")
extraction_directory=$(realpath -m -- "$work_directory/.extract-qt-everywhere-src-6.8.4")
source_marker=.cimbarpunk-extraction-complete

reject_overlap source "$source_directory" build "$build_directory"
reject_overlap source "$source_directory" install "$install_prefix"
reject_overlap build "$build_directory" install "$install_prefix"

for child in "$archive_path" "$archive_part" "$source_directory" "$build_directory" "$extraction_directory"; do
    if [[ $child != "$work_directory"/* ]]; then
        printf 'Resolved work target escaped the work directory: %s\n' "$child" >&2
        exit 2
    fi
done

mkdir -p "$work_directory"
if [[ -f $archive_path ]] && ! printf '%s  %s\n' "$archive_sha256" "$archive_path" | sha256sum --check --strict; then
    invalid_archive=$archive_path.invalid.$(date +%s)
    mv -- "$archive_path" "$invalid_archive"
    printf 'Moved invalid completed archive aside: %s\n' "$invalid_archive" >&2
fi
if [[ ! -f $archive_path ]]; then
    if ! curl --fail --location --retry 3 --continue-at - --output "$archive_part" "$archive_url"; then
        printf 'Download interrupted; resumable partial retained at %s\n' "$archive_part" >&2
        exit 1
    fi
    if ! printf '%s  %s\n' "$archive_sha256" "$archive_part" | sha256sum --check --strict; then
        if [[ $archive_part == "$work_directory"/*.part ]]; then
            rm -f -- "$archive_part"
        fi
        printf 'Downloaded Qt archive checksum mismatch; partial removed for a clean retry.\n' >&2
        exit 1
    fi
    mv -- "$archive_part" "$archive_path"
fi
printf '%s  %s\n' "$archive_sha256" "$archive_path" | sha256sum --check --strict

if [[ -e $source_directory && ! -f $source_directory/$source_marker ]]; then
    printf 'Unmarked Qt source directory cannot be trusted after an interrupted extraction: %s\n' \
        "$source_directory" >&2
    exit 2
fi
if [[ ! -d $source_directory ]]; then
    if [[ -e $extraction_directory ]]; then
        resolved_extraction=$(realpath -m -- "$extraction_directory")
        if [[ $resolved_extraction != "$work_directory"/.extract-qt-everywhere-src-6.8.4 ]]; then
            printf 'Refusing to clean unexpected extraction path: %s\n' "$resolved_extraction" >&2
            exit 2
        fi
        rm -rf -- "$resolved_extraction"
    fi
    mkdir -p "$extraction_directory"
    tar -xJf "$archive_path" -C "$extraction_directory"
    extracted_source=$extraction_directory/qt-everywhere-src-6.8.4
    required_source_files=(
        "$extracted_source/configure"
        "$extracted_source/qtbase/LICENSES/LGPL-3.0-only.txt"
        "$extracted_source/qtdeclarative/LICENSES/LGPL-3.0-only.txt"
        "$extracted_source/qtmultimedia/LICENSES/LGPL-3.0-only.txt"
        "$extracted_source/qtsvg/LICENSES/LGPL-3.0-only.txt"
    )
    for required_source in "${required_source_files[@]}"; do
        [[ -f $required_source ]] || { printf 'Incomplete Qt extraction: %s\n' "$required_source" >&2; exit 1; }
    done
    : > "$extracted_source/$source_marker"
    mv -- "$extracted_source" "$source_directory"
    rmdir -- "$extraction_directory"
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

qt_version=$("$install_prefix/bin/qtpaths" --qt-version)
[[ $qt_version == 6.8.4 ]] || { printf 'Qt install version mismatch: %s\n' "$qt_version" >&2; exit 1; }
test -f "$install_prefix/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake"
test -f "$install_prefix/plugins/iconengines/libqsvgicon.so"

qt_license_root=$install_prefix/share/licenses/qt
mkdir -p "$qt_license_root/qt-distribution"
cp -a -- "$source_directory/LICENSES/." "$qt_license_root/qt-distribution/"
shopt -s nullglob
sbom_files=("$install_prefix"/sbom/*-6.8.4.spdx)
shopt -u nullglob
if [[ ${#sbom_files[@]} -eq 0 ]]; then
    printf 'Qt installation did not produce any module SPDX documents.\n' >&2
    exit 1
fi
for sbom in "${sbom_files[@]}"; do
    module=$(basename -- "$sbom" -6.8.4.spdx)
    module_licenses=$source_directory/$module/LICENSES
    [[ -d $module_licenses ]] || { printf 'Missing licenses for installed Qt module: %s\n' "$module" >&2; exit 1; }
    mkdir -p "$qt_license_root/$module"
    cp -a -- "$module_licenses/." "$qt_license_root/$module/"
done
