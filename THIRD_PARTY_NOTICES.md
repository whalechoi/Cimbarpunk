# Third-party notices

This file identifies the third-party code used to build or run Cimbarpunk M1. It is a notice, not a replacement for the corresponding license texts. Source redistributors must retain the license files that accompany each source tree. Packaged builds install this notice, Cimbarpunk's GPL-3.0-only license, and the exact `libcimbar` MPL-2.0 license under `share/licenses/cimbarpunk`.

## Direct runtime and build dependencies

| Component | Fixed source / version | Source | License |
|---|---|---|---|
| Qt | 6.8.4 exact; modules Core, Gui, Widgets, Quick, Qml, Multimedia, Svg, Test and their Qt-declared module dependencies | <https://download.qt.io/official_releases/qt/6.8/6.8.4/single/> | GNU LGPL-3.0-only or GNU GPL-2.0-only/GPL-3.0-only, depending on module/file; see the Qt source `LICENSES/` directory and <https://www.qt.io/licensing/open-source-lgpl-obligations> |
| libcimbar | Git commit `c509e0bb142bfd20e22583fb96f520e8083f3fba` | <https://github.com/sz3/libcimbar> | Mozilla Public License 2.0; exact text installed as `libcimbar-MPL-2.0.txt` |
| OpenCV | vcpkg `opencv4` resolved by baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d` (Windows M1 resolution: 4.12.0#7) | <https://github.com/opencv/opencv> and <https://github.com/opencv/opencv_contrib> | Apache-2.0; individual third-party files retain their own notices |
| Zstandard | Source vendored by the fixed libcimbar commit | <https://github.com/facebook/zstd> | BSD-3-Clause or GPL-2.0-only; Cimbarpunk builds the BSD-licensed library sources (`external/libcimbar/src/third_party_lib/zstd/LICENSE`) |
| Wirehair | Source vendored by the fixed libcimbar commit | <https://github.com/catid/wirehair> | BSD-3-Clause (`external/libcimbar/src/third_party_lib/wirehair/LICENSE`) |
| libcorrect | Source vendored by the fixed libcimbar commit | <https://github.com/quiet/libcorrect> | BSD-3-Clause (`external/libcimbar/src/third_party_lib/libcorrect/LICENSE`) |
| GLFW | vcpkg `glfw3` resolved by the fixed baseline (Windows M1 resolution: 3.4#1); required by libcimbar's Windows build graph | <https://github.com/glfw/glfw> | Zlib |
| ANGLE | vcpkg `angle` resolved by the fixed baseline (Windows M1 resolution: Chromium 7258#2); required by libcimbar's Windows build graph | <https://chromium.googlesource.com/angle/angle> | BSD-3-Clause; ANGLE's `README.chromium`/third-party notices cover imported code |

`qsvgicon` is part of Qt Svg and is deliberately included in Windows deployment so the SVG tray icon remains visible on machines without a Qt installation.

## Sources vendored inside libcimbar

The fixed libcimbar tree also contains the following source components. Their authoritative license texts remain in the listed repository paths and must accompany source distributions.

| Component | Repository path | Upstream | License |
|---|---|---|---|
| base91 | `external/libcimbar/src/third_party_lib/base91/LICENSE` | <https://github.com/deepin-community/base91> | BSD-3-Clause |
| moodycamel ConcurrentQueue | `external/libcimbar/src/third_party_lib/concurrentqueue/LICENSE.md` | <https://github.com/cameron314/concurrentqueue> | BSD-2-Clause |
| cxxopts | `external/libcimbar/src/third_party_lib/cxxopts/LICENSE` | <https://github.com/jarro2783/cxxopts> | MIT |
| fmt | `external/libcimbar/src/third_party_lib/fmt/LICENSE` | <https://github.com/fmtlib/fmt> | MIT |
| intx | `external/libcimbar/src/third_party_lib/intx/LICENSE` | <https://github.com/chfast/intx> | Apache-2.0 |
| libpopcnt | `external/libcimbar/src/third_party_lib/libpopcnt/LICENSE` | <https://github.com/kimwalisch/libpopcnt> | MIT |
| PicoSHA2 | `external/libcimbar/src/third_party_lib/PicoSHA2/LICENSE` | <https://github.com/okdshin/PicoSHA2> | MIT |
| stb | `external/libcimbar/src/third_party_lib/stb/LICENSE` | <https://github.com/nothings/stb> | MIT or public domain |

## vcpkg-resolved OpenCV dependencies

The exact dependency closure is recorded by each configured build in `out/build/<preset>/vcpkg_installed/vcpkg/status`. The fixed Windows M1 resolution also contains Abseil (Apache-2.0), FlatBuffers (Apache-2.0), libjpeg-turbo (IJG/BSD-3-Clause/zlib), XZ/liblzma (0BSD), libpng (libpng-2.0), libwebp (BSD-3-Clause), protobuf (BSD-3-Clause), quirc (ISC), libtiff (libtiff), utf8_range (Apache-2.0), and zlib (Zlib). These are pulled by OpenCV/vcpkg; deployed binaries and source offers must retain the notices installed by vcpkg under `share/<port>/copyright`.

The Qt and ANGLE distributions additionally carry their own third-party notices (for example ICU, HarfBuzz, libjpeg/libpng, Vulkan/OpenGL headers, and platform media components when enabled). Because the verified Qt kit is built from official source, its `LICENSES/` and per-module `src/3rdparty` notices are the authoritative inventory for the features actually enabled on that platform.
