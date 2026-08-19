# Cimbarpunk Desktop Region Decoder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a tray-only desktop application that selects one monitor region, captures it continuously, reconstructs the first completed cimbar file, and commits that file safely to disk.

**Architecture:** A Qt tray/overlay frontend drives a `CaptureSession` state machine. Replaceable capture, frame-processing, decoder, settings, and output interfaces keep Qt Multimedia and `libcimbar` details behind focused adapters; a one-slot mailbox gives the decoder worker bounded backpressure.

**Tech Stack:** C++20, Qt 6.8.4 (Core, Gui, Widgets, Quick, Qml, Multimedia, Test), CMake/CTest/Ninja, vcpkg, OpenCV, pinned `libcimbar`.

**Spec:** `docs/superpowers/specs/2026-08-19-desktop-region-decoder-design.md`

## Global Constraints

- Cimbarpunk-owned source is licensed `GPL-3.0-only`; retain `libcimbar` under MPL-2.0 and ship third-party notices.
- Use C++20 and exactly Qt 6.8.4 for M1 builds; do not vendor Qt into Git.
- Pin `libcimbar` to `c509e0bb142bfd20e22583fb96f520e8083f3fba`.
- Pin vcpkg to builtin baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`.
- The production application has no main window, no autostart, no updater, no telemetry, and no network service.
- M1 contains no encoding service, encoding UI, or continuous multi-file receive mode.
- One capture task and one output file are allowed at a time; the first completed stream ends the task.
- Selection is constrained to one screen; full-screen selection is valid.
- The decode mailbox holds at most one pending frame and replaces stale frames.
- Output uses a registered temporary file in the destination directory, atomic rename, and never overwrites an existing file.
- Windows receives full local build, automated, packaging, and live-capture validation.
- Linux receives a clean SSH build and non-GUI automated tests on `whale@192.168.43.201`; do not claim Linux GUI validation.
- macOS remains behind portable interfaces but is not built or claimed supported in M1.

---

## Planned File Structure

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
├── THIRD_PARTY_NOTICES.md
├── vcpkg.json
├── cmake/Deploy.cmake
├── external/libcimbar
├── resources/icons/tray.svg
├── scripts/{configure,verify}-windows.ps1
├── scripts/{configure,verify}-linux.sh
├── src/
│   ├── CMakeLists.txt
│   ├── app/AppRuntime.{h,cpp}
│   ├── app/main.cpp
│   ├── capture/ICaptureSource.h
│   ├── capture/QtScreenCaptureSource.{h,cpp}
│   ├── core/SessionTypes.h
│   ├── core/Version.h
│   ├── decoder/IDecoder.h
│   ├── decoder/CimbarDecoderAdapter.{h,cpp}
│   ├── diagnostics/RotatingLogger.{h,cpp}
│   ├── output/IOutputStore.h
│   ├── output/OutputStore.{h,cpp}
│   ├── pipeline/LatestFrameMailbox.{h,cpp}
│   ├── pipeline/FramePipeline.{h,cpp}
│   ├── pipeline/IFrameProcessor.h
│   ├── pipeline/DecodeWorker.{h,cpp}
│   ├── selection/ScreenIdentity.{h,cpp}
│   ├── selection/SelectionModel.{h,cpp}
│   ├── selection/SelectionOverlayController.{h,cpp}
│   ├── selection/qml/SelectionOverlay.qml
│   ├── session/CaptureSession.{h,cpp}
│   ├── settings/SettingsStore.{h,cpp}
│   └── tray/TrayController.{h,cpp}
└── tests/
    ├── CMakeLists.txt
    ├── fakes/*.h
    ├── fixtures/cimbar/{manifest.json,source.bin,mode68/*.png}
    ├── integration/tst_decoder.cpp
    ├── integration/tst_session.cpp
    ├── manual/frame_player/{CMakeLists.txt,main.cpp}
    └── unit/tst_*.cpp
```

## Task 1: Reproducible Project Foundation

**Files:**
- Create: `.gitattributes`
- Create: `.gitignore`
- Create: `.gitmodules`
- Create: `LICENSE`
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `vcpkg.json`
- Create: `src/CMakeLists.txt`
- Create: `src/core/Version.h`
- Create: `tests/CMakeLists.txt`
- Create: `tests/unit/tst_version.cpp`
- Add submodule: `external/libcimbar`

**Interfaces:**
- Produces: `constexpr std::string_view cimbarpunk::versionString() noexcept` returning `"0.1.0"`.
- Produces: CMake targets `cimbarpunk_core` and `cimbarpunk_tests`, plus Windows/Linux debug/release presets.

- [ ] **Step 0: Provision the exact Windows Qt SDK outside Git**

```powershell
py -m venv .deps\aqt
& .deps\aqt\Scripts\python.exe -m pip install --disable-pip-version-check aqtinstall==3.3.0
& .deps\aqt\Scripts\python.exe -m aqt install-qt windows desktop 6.8.4 win64_msvc2022_64 -O .deps\Qt --archives qtbase qtdeclarative qtmultimedia qtsvg qttools
$env:CIMBARPUNK_QT_ROOT = (Resolve-Path '.deps\Qt\6.8.4\msvc2022_64').Path
```

Verify: `Test-Path "$env:CIMBARPUNK_QT_ROOT\lib\cmake\Qt6\Qt6Config.cmake"` returns `True`. Keep `.deps/` ignored; do not commit Qt or the virtual environment.

- [ ] **Step 1: Add the pinned upstream dependency**

```powershell
git submodule add https://github.com/sz3/libcimbar.git external/libcimbar
git -C external/libcimbar checkout c509e0bb142bfd20e22583fb96f520e8083f3fba
git add .gitmodules external/libcimbar
git -C external/libcimbar rev-parse HEAD
```

Expected final output: `c509e0bb142bfd20e22583fb96f520e8083f3fba`.

- [ ] **Step 2: Write the first failing smoke test and initial CMake graph**

Create `tests/unit/tst_version.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "core/Version.h"
#include <QtTest/QTest>

class VersionTest final : public QObject {
    Q_OBJECT
private slots:
    void exposesPinnedApplicationVersion() {
        QCOMPARE(cimbarpunk::versionString(), std::string_view{"0.1.0"});
    }
};

QTEST_GUILESS_MAIN(VersionTest)
#include "tst_version.moc"
```

Create the root graph with these required lines:

```cmake
cmake_minimum_required(VERSION 3.25)
project(Cimbarpunk VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
include(CTest)
find_package(Qt6 6.8.4 EXACT REQUIRED COMPONENTS Core Gui Widgets Quick Qml Multimedia Test)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs photo calib3d)
qt_standard_project_setup(REQUIRES 6.8)
set(DISABLE_TESTS ON CACHE BOOL "Disable libcimbar tests in the parent build" FORCE)
add_subdirectory(external/libcimbar EXCLUDE_FROM_ALL)
add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

Run before creating `Version.h`: `cmake --preset windows-debug`

Expected: FAIL because `src/core/Version.h` is missing.

- [ ] **Step 3: Add the minimal version target and manifests**

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>

namespace cimbarpunk {
constexpr std::string_view versionString() noexcept { return "0.1.0"; }
}
```

Use this manifest:

```json
{
  "name": "cimbarpunk",
  "version-string": "0.1.0",
  "builtin-baseline": "9e593bb18ea69cc5095e012465dcd675a822ed0d",
  "dependencies": [
    { "name": "angle", "platform": "windows & !mingw" },
    "glfw3",
    "opencv4"
  ]
}
```

Presets use Ninja, `${sourceDir}/out/build/${presetName}`, `${sourceDir}/out/install/${presetName}`, `$env{CIMBARPUNK_QT_ROOT}` as `CMAKE_PREFIX_PATH`, and `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` as the toolchain. Windows uses `x64-windows`; Linux uses `x64-linux`.

Create the first source target:

```cmake
add_library(cimbarpunk_core INTERFACE)
target_include_directories(cimbarpunk_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
```

Create the first test and aggregate target:

```cmake
qt_add_executable(tst_version unit/tst_version.cpp)
target_link_libraries(tst_version PRIVATE cimbarpunk_core Qt6::Core Qt6::Test)
add_test(NAME version COMMAND tst_version)
add_custom_target(cimbarpunk_tests DEPENDS tst_version)
```

Each later test executable is added to `cimbarpunk_tests` with `add_dependencies` so the aggregate target always compiles the full application test suite.

- [ ] **Step 4: Add repository hygiene and licensing**

Add the unmodified canonical GNU GPL version 3 text to `LICENSE`. Normalize source, Markdown, JSON, QML, shell, and CMake files to LF; retain CRLF for PowerShell. Ignore `/out/`, `/.deps/`, editor state, CMake user presets, and generated package directories. Do not ignore test fixtures.

- [ ] **Step 5: Build and run the smoke test**

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --target cimbarpunk_tests
ctest --preset windows-debug --output-on-failure
```

Expected: the version test passes.

- [ ] **Step 6: Commit**

```powershell
git add .gitattributes .gitignore .gitmodules LICENSE CMakeLists.txt CMakePresets.json vcpkg.json src tests external/libcimbar
git commit -m "build: establish reproducible Qt project"
```

## Task 2: Cross-Component Contracts

**Files:**
- Create: `src/core/SessionTypes.h`
- Create: `src/capture/ICaptureSource.h`
- Create: `src/decoder/IDecoder.h`
- Create: `src/output/IOutputStore.h`
- Create: `src/pipeline/IFrameProcessor.h`
- Create: `tests/unit/tst_contracts.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: the exact value types and abstract interfaces below; all later tasks consume them.

- [ ] **Step 1: Write a compile-level failing contract test**

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "core/SessionTypes.h"
#include <QtTest/QTest>

class ContractsTest final : public QObject {
    Q_OBJECT
private slots:
    void payloadPreservesSenderMetadata() {
        const cimbarpunk::DecodedPayload value{
            .suggestedName = QStringLiteral("report.txt"),
            .fallbackName = QStringLiteral("17.4096"),
            .compressedBytes = QByteArray("zstd")
        };
        QCOMPARE(value.suggestedName, QStringLiteral("report.txt"));
        QCOMPARE(value.fallbackName, QStringLiteral("17.4096"));
    }
};

QTEST_GUILESS_MAIN(ContractsTest)
#include "tst_contracts.moc"
```

Run: `cmake --build --preset windows-debug --target cimbarpunk_tests`

Expected: FAIL because `SessionTypes.h` is missing.

- [ ] **Step 2: Define the shared value types exactly once**

```cpp
namespace cimbarpunk {
enum class SessionState { Idle, Selecting, Adjusting, Capturing, Completed, Error, Cancelled };

struct ScreenSelection {
    QString screenId;
    QRectF screenGeometry;
    QRectF logicalRect;
};

struct DecodedPayload {
    QString suggestedName;
    QString fallbackName;
    QByteArray compressedBytes;
};

struct DecodeUpdate {
    bool recognized = false;
    std::optional<double> progress;
    std::optional<DecodedPayload> completed;
};

struct OutputResult {
    bool ok = false;
    QString finalPath;
    QString error;
};
}
```

Register `SessionState`, `ScreenSelection`, `DecodedPayload`, and `OutputResult` with `Q_DECLARE_METATYPE`.

- [ ] **Step 3: Define the capture, decoder, output, and worker contracts**

```cpp
class ICaptureSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ICaptureSource() override = default;
    virtual bool start(QScreen* screen, QString* error) = 0;
    virtual void stop() = 0;
signals:
    void frameReady(const QImage& frame);
    void activeChanged(bool active);
    void failed(const QString& message);
};

class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual void reset() = 0;
    virtual DecodeUpdate decode(const QImage& rgbFrame) = 0;
};

class IOutputStore {
public:
    virtual ~IOutputStore() = default;
    virtual bool prepareDirectory(const QString& directory, QString* error) = 0;
    virtual OutputResult commit(const DecodedPayload& payload, const QString& directory) = 0;
    virtual void cleanupRegisteredTemporaryFiles() = 0;
};

class IFrameProcessor : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IFrameProcessor() override = default;
    virtual bool start(const ScreenSelection& selection, const QString& outputDirectory, QString* error) = 0;
    virtual void submitFrame(const QImage& fullScreenFrame) = 0;
    virtual void stop() = 0;
signals:
    void frameAccepted();
    void progressChanged(double progress);
    void completed(const OutputResult& result);
    void failed(const QString& message);
};
```

- [ ] **Step 4: Run focused and full tests**

Run: `ctest --preset windows-debug --output-on-failure`

Expected: version and contract tests pass.

- [ ] **Step 5: Commit**

```powershell
git add src/core src/capture src/decoder src/output src/pipeline src/CMakeLists.txt tests
git commit -m "feat: define capture session contracts"
```

## Task 3: Single-Screen Selection Geometry

**Files:**
- Create: `src/selection/ScreenIdentity.h`
- Create: `src/selection/ScreenIdentity.cpp`
- Create: `src/selection/SelectionModel.h`
- Create: `src/selection/SelectionModel.cpp`
- Create: `tests/unit/tst_selection_model.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ScreenSelection`.
- Produces: `ScreenIdentity::fromScreen(const QScreen&) -> QString`.
- Produces: `SelectionModel` properties `screenGeometry`, `selection`, `hasSelection`; methods `beginDrag`, `updateDrag`, `endDrag`, `moveBy`, `resizeBy`, `normalizedRect`, `restoreNormalized`, `toSelection`, and `mapToFrame`.

- [ ] **Step 1: Write geometry tests before the model**

Cover reversed drag, a screen at `(-1920, 0, 1920, 1080)`, movement clamping, all eight handles, 32 logical-pixel minimum size, normalization, and asymmetric physical scaling. Include:

```cpp
void mapsLogicalSelectionToActualFramePixels() {
    SelectionModel model;
    model.setScreenGeometry(QRectF(-1920, 0, 1920, 1080));
    model.setSelection(QRectF(-1440, 270, 960, 540));
    QCOMPARE(model.mapToFrame(QSize(2560, 1440)), QRect(640, 360, 1280, 720));
}
```

Run: `ctest --preset windows-debug -R selection_model --output-on-failure`

Expected: FAIL because `SelectionModel` is missing.

- [ ] **Step 2: Implement drag, move, and resize with one clamp function**

Use `enum class ResizeHandle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };`. Normalize drag endpoints, enforce 32 logical pixels on both axes, then clamp the result to `screenGeometry`. `moveBy` preserves size. `resizeBy` keeps the opposite edge or corner fixed.

- [ ] **Step 3: Implement normalized persistence and physical mapping**

```cpp
const double sx = frameSize.width() / screenGeometry.width();
const double sy = frameSize.height() / screenGeometry.height();
const int left = qFloor((selection.x() - screenGeometry.x()) * sx);
const int top = qFloor((selection.y() - screenGeometry.y()) * sy);
const int right = qCeil((selection.x() + selection.width() - screenGeometry.x()) * sx);
const int bottom = qCeil((selection.y() + selection.height() - screenGeometry.y()) * sy);
return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1))
    .intersected(QRect(QPoint(0, 0), frameSize));
```

Reject normalized rectangles with non-finite values, non-positive dimensions, or coordinates outside `[0, 1]`.

- [ ] **Step 4: Implement stable screen identity**

Use manufacturer, model, and serial when all are non-empty. Otherwise hash `name`, full geometry, and DPR with SHA-256 and prefix `fallback:`. Unit-test both paths through a pure helper; keep the `QScreen` overload thin.

- [ ] **Step 5: Run tests and commit**

```powershell
ctest --preset windows-debug -R selection_model --output-on-failure
git add src/selection src/CMakeLists.txt tests
git commit -m "feat: add single-screen selection geometry"
```

## Task 4: Persistent Settings and Bounded Diagnostics

**Files:**
- Create: `src/settings/SettingsStore.h`
- Create: `src/settings/SettingsStore.cpp`
- Create: `src/diagnostics/RotatingLogger.h`
- Create: `src/diagnostics/RotatingLogger.cpp`
- Create: `tests/unit/tst_settings_store.cpp`
- Create: `tests/unit/tst_rotating_logger.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: normalized rectangles from `SelectionModel`.
- Produces: `QString outputDirectory() const`, `void setOutputDirectory(const QString&)`, `void saveSelection(QStringView screenId, const QRectF& normalizedRect)`, `std::optional<QRectF> restoreSelection(QStringView screenId) const`, `void registerTemporaryFile(const QString&)`, `void unregisterTemporaryFile(const QString&)`, and `QStringList registeredTemporaryFiles() const`.
- Produces: `RotatingLogger::install`, `write`, and `uninstall` with 1 MiB current file and three backups.

- [ ] **Step 1: Write isolated INI-backed settings tests**

Construct `QSettings` with a `QTemporaryDir` file and inject it. Assert the default equals `QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/Cimbarpunk"`; assert output directory, screen ID, normalized rect, and an absolute pending path survive a second store instance. Assert malformed rectangles return `std::nullopt`.

- [ ] **Step 2: Implement settings keys and sync behavior**

Use only:

```text
output/directory
selection/screenId
selection/normalizedRect
output/pendingTemporaryFiles
```

Call `sync()` after registering or unregistering a temporary file.

- [ ] **Step 3: Write logger rotation tests**

Inject a 128-byte limit, write three 100-byte messages, and assert `cimbarpunk.log`, `.1`, `.2`, and no `.4`. Assert image diagnostics contain dimensions and format, never pixel bytes.

- [ ] **Step 4: Implement deterministic rotation**

Before a write exceeds the limit, close the file, delete the oldest backup, rename `.2` to `.3`, `.1` to `.2`, the current file to `.1`, then reopen the current file. Protect writes with `QMutex`. Production uses 1 MiB and three backups.

- [ ] **Step 5: Run tests and commit**

```powershell
ctest --preset windows-debug -R "settings_store|rotating_logger" --output-on-failure
git add src/settings src/diagnostics src/CMakeLists.txt tests
git commit -m "feat: persist settings and rotate diagnostics"
```

## Task 5: Safe Atomic Output Store

**Files:**
- Create: `src/output/OutputStore.h`
- Create: `src/output/OutputStore.cpp`
- Create: `src/output/LibcimbarPayloadWriter.h`
- Create: `src/output/LibcimbarPayloadWriter.cpp`
- Create: `tests/unit/tst_output_store.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IOutputStore`, `DecodedPayload`, and `SettingsStore` pending-file methods.
- Produces: `OutputStore`, `sanitizeFilename(QStringView) -> QString`, and `uniqueDestination(QStringView, QStringView) -> QString`.
- Injected writer: `std::function<bool(const QString&, QByteArrayView, QString*)>`.
- Produces: `makeLibcimbarPayloadWriter()` with the injected-writer signature for production wiring.

- [ ] **Step 1: Write failing filename and collision tests**

Test `../CON.txt`, `a<b>:c?.txt`, empty sender names, Unicode basename, `report.txt`, `report (1).txt`, and `archive.tar.zst`. Assert numbering appears before the final extension and existing files remain byte-for-byte unchanged.

- [ ] **Step 2: Write failing lifecycle tests with a fake writer**

The fake writes `"decoded"` to the temporary path. Assert the registry contains the absolute `.cimbarpunk-<UUID>.part` path during the write, contains no path afterward, and writer failure leaves no file. Assert cleanup ignores unregistered files and paths outside the recorded output directory.

- [ ] **Step 3: Implement directory and name safety**

Create missing directories with `QDir::mkpath`, then verify writability with a private probe file. Use `QFileInfo(name).fileName()`, replace Windows-illegal characters and trailing dots/spaces, reject `CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, and `LPT1`–`LPT9`, and fall back to `cimbar-<fallbackName>-<UTC yyyyMMdd-HHmmss>.bin`.

- [ ] **Step 4: Implement temporary write and atomic commit**

Register the absolute path before opening it. Invoke the writer, verify the result exists, select a unique final path, and call `QFile::rename` in the same directory. On every return path unregister it; on failure remove only that exact path.

- [ ] **Step 5: Implement the production decompression writer**

Open the temporary path with `cimbar::zstd_decompressor<std::ofstream>`, call `write(compressedBytes.data(), static_cast<size_t>(compressedBytes.size()))`, call `flush`, and require both `write` and `good()` to succeed. Return `last_error()` through the error parameter. Link this source to the pinned `zstd` target; the fake writer remains the default unit-test seam.

- [ ] **Step 6: Run tests and commit**

```powershell
ctest --preset windows-debug -R output_store --output-on-failure
git add src/output src/CMakeLists.txt tests
git commit -m "feat: commit decoded files atomically"
```

## Task 6: Frame Crop and One-Slot Backpressure

**Files:**
- Create: `src/pipeline/LatestFrameMailbox.h`
- Create: `src/pipeline/LatestFrameMailbox.cpp`
- Create: `src/pipeline/FramePipeline.h`
- Create: `src/pipeline/FramePipeline.cpp`
- Create: `tests/unit/tst_latest_frame_mailbox.cpp`
- Create: `tests/unit/tst_frame_pipeline.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ScreenSelection` and Task 3 mapping semantics.
- Produces: `LatestFrameMailbox::replace`, `take`, `stop`, `reset`, `droppedCount`.
- Produces: `FramePipeline::configure(ScreenSelection)` and `prepare(QImage) -> std::optional<QImage>`.

- [ ] **Step 1: Write mailbox replacement and stop tests**

Insert red, green, and blue images without taking. Assert `take()` returns blue and `droppedCount()` is 2. Start a waiting thread, call `stop()`, and assert it returns `std::nullopt` within 250 ms. Assert `reset()` accepts a new frame.

- [ ] **Step 2: Implement one optional frame**

Use `QMutex`, `QWaitCondition`, `std::optional<QImage>`, a stop flag, and a 64-bit dropped counter. `replace` overwrites and increments only when a pending frame existed. `take` blocks until frame or stop, moves the frame, and clears the optional.

- [ ] **Step 3: Write crop and ownership tests**

Feed a 2560×1440 ARGB image and Task 3's logical selection. Assert 1280×720 RGB888 output, valid pixels after the source is destroyed, and null/zero-sized crops return `std::nullopt`.

- [ ] **Step 4: Implement frame preparation**

Map against actual frame size, call `copy(crop)`, convert to `QImage::Format_RGB888`, and call `detach()`. Do not retain multimedia frame storage.

- [ ] **Step 5: Run tests and commit**

```powershell
ctest --preset windows-debug -R "latest_frame_mailbox|frame_pipeline" --output-on-failure
git add src/pipeline src/CMakeLists.txt tests
git commit -m "feat: add bounded frame processing pipeline"
```

## Task 7: `libcimbar` Decoder Adapter and Fixed Fixtures

**Files:**
- Create: `src/decoder/CimbarDecoderAdapter.h`
- Create: `src/decoder/CimbarDecoderAdapter.cpp`
- Create: `tests/tools/generate_cimbar_fixture.cpp`
- Create: generated `tests/fixtures/cimbar/source.bin`
- Create: generated `tests/fixtures/cimbar/mode68/*.png`
- Create: generated `tests/fixtures/cimbar/manifest.json`
- Create: `tests/integration/tst_decoder.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IDecoder`, `DecodeUpdate`, and RGB888 `QImage`.
- Produces: `CimbarDecoderAdapter::reset()` and `decode()` with detection sequence `68, 66, 67, 4`, locking the first mode that yields valid bytes.
- Produces: fixture manifest fields `sourceSha256`, `mode`, `orderedFrames`, and `dropSafeFrames`.

- [ ] **Step 1: Build a deterministic fixture generator**

Generate exactly 32768 source bytes:

```cpp
QByteArray source(32768, Qt::Uninitialized);
for (qsizetype i = 0; i < source.size(); ++i) {
    source[i] = static_cast<char>((i * 131 + 17) & 0xff);
}
```

Use pinned `EncoderPlus` in mode 68 with redundancy 2.0, save zero-padded PNG names, and write SHA-256 plus order to `manifest.json`. Build and run once, then commit the binary, PNGs, and manifest. The production target must not link this generator.

- [ ] **Step 2: Write integration tests against committed fixtures**

Load frames in manifest order, then a deterministic shuffled order with the first two `dropSafeFrames` omitted. For both runs, stop at the first completed payload, decompress with the pinned zstd helper, and assert SHA-256 equals `sourceSha256`. Feed five duplicate frames first and assert no duplicate completion.

Run: `ctest --preset windows-debug -R decoder --output-on-failure`

Expected: FAIL because `CimbarDecoderAdapter` is missing.

- [ ] **Step 3: Implement extraction and mode detection**

For each RGB888 image, create a non-owning `cv::Mat`, scan four anchors, deskew with `Corners` and `Deskewer`, and skip failed extraction. Before each candidate mode call `cimbar::Config::update(mode)`. Use color correction 1 for mode 4 and 2 for modes 66–68. When a candidate returns positive decoded bytes, lock it; if the detection sink chunk size mismatched, recreate the sink with `Config::temp_conf(mode).fountain_chunk_size()` and consume subsequent frames.

- [ ] **Step 4: Implement progress and completion callback**

Construct `fountain_decoder_sink` with an `on_store` callback that copies the recovered compressed vector into `DecodedPayload::compressedBytes`, preserves the numeric fallback name, and extracts the embedded filename with `cimbar::zstd_header_check::get_filename`. Return maximum active progress clamped to `[0.0, 1.0]`. After completion, ignore frames until `reset()`.

Link exactly:

```cmake
cimb_translator
extractor
correct_static
wirehair
zstd
${OpenCV_LIBS}
```

Add `${OpenCV_INCLUDE_DIRS}`, `${PROJECT_SOURCE_DIR}/external/libcimbar/src/lib`, and `${PROJECT_SOURCE_DIR}/external/libcimbar/src/third_party_lib` as private include directories. Add `ZSTD_STATIC_LINKING_ONLY` only to the adapter target.

- [ ] **Step 5: Run decoder and adjacent tests**

```powershell
cmake --build --preset windows-debug --target cimbarpunk_tests
ctest --preset windows-debug -R "decoder|selection_model|frame_pipeline" --output-on-failure
```

Expected: both fixture orders produce the source SHA-256.

- [ ] **Step 6: Commit**

```powershell
git add src/decoder src/CMakeLists.txt tests
git commit -m "feat: decode cimbar frame sequences"
```

## Task 8: Decode Worker and Safe Stop

**Files:**
- Create: `src/pipeline/DecodeWorker.h`
- Create: `src/pipeline/DecodeWorker.cpp`
- Create: `tests/fakes/FakeDecoder.h`
- Create: `tests/unit/tst_decode_worker.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IFrameProcessor`, `IDecoder`, `IOutputStore`, `FramePipeline`, and `LatestFrameMailbox`.
- Produces: thread-safe, idempotent `DecodeWorker::start`, `submitFrame`, and `stop`.

- [ ] **Step 1: Write worker tests with injected fakes**

Use a fake decoder that blocks on its first call until a latch opens. Submit red, green, and blue while blocked; release and assert the decoder sees red followed by blue, never green. Assert completion calls `IOutputStore::commit` once, emits one `completed`, and rejects later frames. Assert `stop()` waits for the current decode call and returns with no running thread.

- [ ] **Step 2: Implement lifecycle with `std::jthread`**

`start` synchronously calls `prepareDirectory`, configures the pipeline, resets decoder and mailbox, stores the output directory, and starts one `std::jthread`. `submitFrame` only replaces the mailbox image. The loop takes the newest full-screen image, prepares the crop, emits `frameAccepted`, calls `decode`, emits valid progress, and commits the first completed payload on the same worker thread.

- [ ] **Step 3: Implement error and stop semantics**

Catch `std::exception` and unknown exceptions at the loop boundary, emit a stable Chinese error plus a detailed log entry. `stop` requests stop, wakes the mailbox, joins, resets decoder state, and is safe to repeat. Never detach.

- [ ] **Step 4: Run tests and commit**

```powershell
ctest --preset windows-debug -R decode_worker --output-on-failure
git add src/pipeline src/CMakeLists.txt tests
git commit -m "feat: run bounded decoding off the UI thread"
```

## Task 9: Capture Session State Machine

**Files:**
- Create: `src/session/CaptureSession.h`
- Create: `src/session/CaptureSession.cpp`
- Create: `tests/fakes/FakeCaptureSource.h`
- Create: `tests/fakes/FakeFrameProcessor.h`
- Create: `tests/integration/tst_session.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ICaptureSource`, `IFrameProcessor`, `SettingsStore`, `ScreenSelection`, injected `std::function<QScreen*(QStringView)> screenResolver`, and constructor timeout `std::chrono::milliseconds noFrameTimeout` defaulting to 5000 ms.
- Produces: commands `beginSelection`, `selectionCreated`, `confirmSelection`, `cancel`, `stop`, `shutdown`; signals `stateChanged`, `selectionRequested`, `progressChanged`, `completed`, and `failed`.

- [ ] **Step 1: Write state-transition tests**

Test exactly:

```text
Idle -> Selecting -> Adjusting -> Capturing -> Completed -> Idle
Idle -> Selecting -> Cancelled -> Idle
Adjusting -> Cancelled -> Idle
Capturing -> Cancelled -> Idle
Capturing -> Error -> Idle
```

Assert `beginSelection` is rejected outside `Idle`, only one source start occurs, output directory is snapshotted at confirmation, selection persists only after success, and shutdown leaves source and worker stopped.

Construct with a 20 ms frame timeout, emit `frameAccepted` every 10 ms without decoder progress for 60 ms, and assert the session remains `Capturing`. This protects the requirement that decoding itself has no timeout without slowing the test suite.

- [ ] **Step 2: Implement the legal transition table**

Keep validation in one private `bool transitionTo(SessionState)` function. Emit once per accepted transition. Treat `Completed`, `Error`, and `Cancelled` as observable transient states, queueing the final `Idle` transition with `QMetaObject::invokeMethod` and `Qt::QueuedConnection`.

- [ ] **Step 3: Wire frames and first completion**

On confirmation, resolve the selected `QScreen` through the injected resolver, start worker before capture, then connect source frames to `submitFrame`. A null resolution produces a controlled error. On completion, stop source and worker before emitting success. Source and worker errors use the same stop-and-cleanup function.

- [ ] **Step 4: Add the no-frame watchdog**

Start a single-shot five-second `QTimer` after `activeChanged(true)` and reset it on every `frameAccepted`. Timeout text is `连续 5 秒未收到可用画面`. Do not add a decode-progress timeout.

- [ ] **Step 5: Run tests and commit**

```powershell
ctest --preset windows-debug -R session --output-on-failure
git add src/session src/CMakeLists.txt tests
git commit -m "feat: orchestrate one-shot capture sessions"
```

## Task 10: Qt Screen Capture Backend

**Files:**
- Create: `src/capture/QtScreenCaptureSource.h`
- Create: `src/capture/QtScreenCaptureSource.cpp`
- Create: `tests/unit/tst_qt_screen_capture_source.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ICaptureSource`.
- Produces: `QtScreenCaptureSource` using `QScreenCapture`, `QMediaCaptureSession`, and `QVideoSink`.

- [ ] **Step 1: Write lifecycle tests around an injected seam**

Introduce private `IScreenCaptureBackend`, with a Qt implementation and test fake. Assert null-screen rejection, double-start rejection, start/stop order, one copied `QImage` per valid frame, error propagation, and disconnected screen signals after stop.

- [ ] **Step 2: Implement Qt Multimedia capture**

Set the selected screen on `QScreenCapture`, attach it and `QVideoSink` to `QMediaCaptureSession`, and activate. On `videoFrameChanged`, call `toImage()`, reject null, detach, and emit `frameReady`. Do not retain `QVideoFrame`.

- [ ] **Step 3: Abort on display changes**

While active connect `geometryChanged`, `logicalDotsPerInchChanged`, `physicalDotsPerInchChanged`, and `orientationChanged`. Emit `显示器配置在捕获期间发生变化，请重新选择区域` once and stop.

- [ ] **Step 4: Run tests and commit**

```powershell
ctest --preset windows-debug -R qt_screen_capture_source --output-on-failure
git add src/capture src/CMakeLists.txt tests
git commit -m "feat: capture a selected screen with Qt"
```

## Task 11: Full-Screen Selection Overlay

**Files:**
- Create: `src/selection/SelectionOverlayController.h`
- Create: `src/selection/SelectionOverlayController.cpp`
- Create: `src/selection/qml/SelectionOverlay.qml`
- Create: `tests/unit/tst_selection_overlay_controller.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SelectionModel`, `ScreenIdentity`, and session selection commands.
- Produces: `showForScreen(QScreen*, std::optional<QRectF>)`, `enterCaptureMode`, `setProgress`, `hide`; signals `accepted(ScreenSelection)` and `cancelled()`.

- [ ] **Step 1: Write controller tests before QML**

With `QT_QPA_PLATFORM=offscreen`, assert view geometry matches the selected screen, a normalized rect restores only for matching ID, `Enter` emits accepted, `Esc` emits cancelled, and hide releases grabs.

- [ ] **Step 2: Implement the frameless controller**

Use transparent `QQuickView` with `Qt::FramelessWindowHint`, `Qt::WindowStaysOnTopHint`, and no taskbar entry. Expose one `SelectionModel` as a required QML property. Keep mutations in C++; QML sends pointer positions and handle enum values.

QML pointer positions are window-local. Before calling `SelectionModel`, the controller adds `QScreen::geometry().topLeft()`; when publishing rectangles back to QML, it subtracts the same origin. Cover this conversion with the negative-X monitor test from Task 3.

- [ ] **Step 3: Build adjustment UI in QML**

Use four rectangles around the clear selection for dimming. Use four separate two-pixel rectangles outside selection bounds for capture border. Add eight 12-pixel handles, an internal move area, compact “开始”/“取消” toolbar, and `Shortcut` handling for Enter/Escape.

- [ ] **Step 4: Implement contamination rules**

After confirmation hide handles, toolbar, and dimming; make the window mouse-transparent. Place status above, below, left, or right only when its full rectangle fits outside the crop. Hide status when no side fits. Hide the overlay when normalized selection is exactly `(0, 0, 1, 1)`.

- [ ] **Step 5: Run tests and commit**

```powershell
$env:QT_QPA_PLATFORM='offscreen'
ctest --preset windows-debug -R selection_overlay --output-on-failure
Remove-Item Env:QT_QPA_PLATFORM
git add src/selection src/CMakeLists.txt tests
git commit -m "feat: add adjustable full-screen selection overlay"
```

## Task 12: Tray-Only Runtime, Notifications, and Wiring

**Files:**
- Create: `resources/icons/tray.svg`
- Create: `src/tray/TrayController.h`
- Create: `src/tray/TrayController.cpp`
- Create: `src/app/AppRuntime.h`
- Create: `src/app/AppRuntime.cpp`
- Create: `src/app/main.cpp`
- Create: `tests/unit/tst_tray_controller.cpp`
- Create: `tests/integration/tst_app_runtime.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: all production components from Tasks 3–11.
- Produces: executable `cimbarpunk`; tray actions `startCapture`, `stopCapture`, `openOutputDirectory`, `changeOutputDirectory`, and `quitRequested`.

- [ ] **Step 1: Write tray menu state tests**

Without showing a tray icon, assert idle order/text: `状态：空闲`, `开始捕获…`, `打开保存目录`, `更改保存目录…`, separator, `退出`. During capture, start/change are disabled, stop is visible/enabled, and progress appears only when supplied.

- [ ] **Step 2: Implement tray behavior**

Use `QSystemTrayIcon` and `QMenu` with code-native SVG. Open directories through `QDesktopServices::openUrl(QUrl::fromLocalFile(path))`. Change directory through `QFileDialog::getExistingDirectory`, not a settings window. Notify with saved filename; when `messageClicked` is available, open the containing directory.

- [ ] **Step 3: Write runtime wiring tests**

With fakes, assert start chooses `QGuiApplication::screenAt(QCursor::pos())` or primary screen, overlay acceptance reaches session, progress reaches overlay/tray, success hides overlay and notifies once, and quit calls `shutdown` before `QCoreApplication::quit`.

- [ ] **Step 4: Implement `AppRuntime` and `main`**

Create settings, logger, `OutputStore` with `makeLibcimbarPayloadWriter()`, decoder, frame processor, capture, session, overlay, and tray in that order. Clean registered temporary files before showing tray. In `main`:

```cpp
QApplication application(argc, argv);
application.setApplicationName(QStringLiteral("Cimbarpunk"));
application.setOrganizationName(QStringLiteral("Cimbarpunk"));
application.setApplicationVersion(QString::fromLatin1(cimbarpunk::versionString().data()));
application.setQuitOnLastWindowClosed(false);
```

If no system tray is available, show one critical dialog and return nonzero. Never instantiate a normal `QWidget` or `ApplicationWindow`.

- [ ] **Step 5: Run the entire suite**

```powershell
cmake --build --preset windows-debug --target cimbarpunk cimbarpunk_tests
ctest --preset windows-debug --output-on-failure
```

Expected: every automated test passes.

- [ ] **Step 6: Commit**

```powershell
git add resources src tests
git commit -m "feat: wire tray-only decoder application"
```

## Task 13: Fixture Player, Packaging, Docs, and Platform Verification

**Files:**
- Create: `tests/manual/frame_player/CMakeLists.txt`
- Create: `tests/manual/frame_player/main.cpp`
- Create: `cmake/Deploy.cmake`
- Create: `scripts/configure-windows.ps1`
- Create: `scripts/verify-windows.ps1`
- Create: `scripts/configure-linux.sh`
- Create: `scripts/verify-linux.sh`
- Create: `README.md`
- Create: `THIRD_PARTY_NOTICES.md`
- Create: `docs/verification/2026-08-19-m1.md`
- Modify: `CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: built app, committed fixtures, and presets.
- Produces: `cimbarpunk_frame_player`, staged Windows directory, repeatable verification scripts, and documentation.

- [ ] **Step 1: Add the manual frame player**

Create a Qt executable that loads `manifest.json`, displays listed PNGs centered on black at 10 FPS, loops, and exits on `Esc`. Title: `Cimbarpunk Test Frame Player`. Build only with `BUILD_TESTING=ON`; never install it.

- [ ] **Step 2: Add Windows configure and verification scripts**

`configure-windows.ps1` requires absolute `-QtRoot` and `-VcpkgRoot`, verifies `Qt6Config.cmake`, `vcpkg.exe`, CMake, Ninja, and VS 2022 developer environment, sets only `CIMBARPUNK_QT_ROOT` and `VCPKG_ROOT`, then configures. If OpenCV cannot parse pkg-config path, discover installed `pkgconf.exe`, set quoted `PKG_CONFIG`, set `VCPKG_KEEP_ENV_VARS=PKG_CONFIG`, and retry once.

`verify-windows.ps1` configures, builds app/tests, runs CTest, installs to `out/install/windows-release`, and runs `windeployqt --release --qmldir src/selection/qml`. Stop on any nonzero exit.

- [ ] **Step 3: Add Linux configure and verification scripts**

Require absolute `CIMBARPUNK_QT_ROOT` and `VCPKG_ROOT`; use `set -euo pipefail`; configure `linux-release`, build app/tests, and run:

```bash
QT_QPA_PLATFORM=offscreen ctest --preset linux-release --output-on-failure
```

Do not attempt tray/capture/overlay manual tests without GUI.

Provision the SSH host once at user-local paths before validation:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build curl git pkg-config python3-venv p7zip-full zip unzip libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 libxcb-xinerama0 libxcb-keysyms1 libxcb-image0 libxcb-render-util0 libxcb-icccm4
mkdir -p /home/whale/.local/share/cimbarpunk
python3 -m venv /home/whale/.local/share/cimbarpunk/aqt
/home/whale/.local/share/cimbarpunk/aqt/bin/python -m pip install --disable-pip-version-check aqtinstall==3.3.0
/home/whale/.local/share/cimbarpunk/aqt/bin/python -m aqt install-qt linux desktop 6.8.4 linux_gcc_64 -O /home/whale/.local/share/cimbarpunk/Qt --archives qtbase qtdeclarative qtmultimedia qtsvg qttools
git clone https://github.com/microsoft/vcpkg.git /home/whale/.local/share/cimbarpunk/vcpkg
git -C /home/whale/.local/share/cimbarpunk/vcpkg checkout 9e593bb18ea69cc5095e012465dcd675a822ed0d
/home/whale/.local/share/cimbarpunk/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

Verify Qt at `/home/whale/.local/share/cimbarpunk/Qt/6.8.4/gcc_64/lib/cmake/Qt6/Qt6Config.cmake` and vcpkg at `/home/whale/.local/share/cimbarpunk/vcpkg/vcpkg`.

- [ ] **Step 4: Add deploy rules and notices**

Install executable, QML module, icon, `LICENSE`, `THIRD_PARTY_NOTICES.md`, and exact `libcimbar` MPL-2.0 license. Document Qt, OpenCV, zstd, wirehair, libcorrect, GLFW, and ANGLE sources. Use Qt deploy tools rather than hard-coded DLL names.

- [ ] **Step 5: Document setup and support boundaries**

README covers prerequisites, submodule checkout, exact Qt paths, presets, tray menu, controls, output naming, logs, permissions, Windows validation, Linux build-only status, macOS unverified status, and GPL source availability. Include Wayland portal limitations without claiming Linux GUI support.

- [ ] **Step 6: Commit the verification-ready implementation**

```powershell
git add CMakeLists.txt cmake scripts README.md THIRD_PARTY_NOTICES.md tests/manual src tests/CMakeLists.txt
git update-index --chmod=+x scripts/configure-linux.sh scripts/verify-linux.sh
git commit -m "build: add packaging and verification workflows"
```

- [ ] **Step 7: Run full Windows verification**

```powershell
pwsh -File scripts/verify-windows.ps1 -QtRoot $env:CIMBARPUNK_QT_ROOT -VcpkgRoot $env:VCPKG_ROOT
```

Run player and staged app. Verify tray-only startup; drag/move/eight handles; Enter/Esc; screen clamp; full-screen overlay hiding; live decode; SHA-256 equality with fixture source; automatic stop; retained selection; duplicate numbering; manual cleanup; no residual process.

- [ ] **Step 8: Run clean Linux SSH verification**

Create a unique remote directory and verify it is inside `/home/whale` before transfer. Archive the main commit and the submodule separately so no local build products or Git metadata are copied:

```powershell
$remoteDir = (ssh whale@192.168.43.201 'mktemp -d /home/whale/cimbarpunk-m1.XXXXXX').Trim()
if ($remoteDir -notmatch '^/home/whale/cimbarpunk-m1\.[A-Za-z0-9]+$') { throw "Unexpected remote path: $remoteDir" }
git archive --format=tar HEAD | ssh whale@192.168.43.201 "tar -xf - -C '$remoteDir'"
git -C external/libcimbar archive --format=tar HEAD | ssh whale@192.168.43.201 "mkdir -p '$remoteDir/external/libcimbar' && tar -xf - -C '$remoteDir/external/libcimbar'"
```

Then run:

```bash
ssh whale@192.168.43.201 "cd '$remoteDir' && CIMBARPUNK_QT_ROOT=/home/whale/.local/share/cimbarpunk/Qt/6.8.4/gcc_64 VCPKG_ROOT=/home/whale/.local/share/cimbarpunk/vcpkg ./scripts/verify-linux.sh"
```

Expected: configure, build, and all non-GUI tests pass. Record compiler, Qt, CMake, and test summary; state Linux GUI capture was not run.

- [ ] **Step 9: Record evidence, run final regression, and commit**

Write `docs/verification/2026-08-19-m1.md` with the exact Git commit, Windows compiler/Qt/CMake versions, CTest summary, live-decoded source/output SHA-256 values, packaged launch result, Linux host/compiler/Qt/CMake versions, and Linux CTest summary. Include the statements `Linux GUI capture not run` and `macOS not built or tested`.

```powershell
ctest --preset windows-release --output-on-failure
git status --short
git add docs/verification/2026-08-19-m1.md README.md
git commit -m "test: record M1 platform verification"
```

Expected: clean worktree after commit, Windows release tests pass, Linux non-GUI tests pass, and staged Windows app launches.
