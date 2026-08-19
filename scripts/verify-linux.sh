#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
repository_root=$(cd -- "$script_dir/.." && pwd -P)

"$script_dir/configure-linux.sh"
cd "$repository_root"
cmake --build --preset linux-release --target cimbarpunk cimbarpunk_tests cimbarpunk_frame_player
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --preset linux-release --output-on-failure
