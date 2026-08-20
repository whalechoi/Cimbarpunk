# Windows Screen Capture Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the packaged Windows application capture real desktop frames through Qt 6.8.4's FFmpeg backend and complete the deferred Windows live validation.

**Architecture:** Keep `QtScreenCaptureSource` and the application runtime unchanged. Add a reproducible FFmpeg 7.1.1 input to the official Qt build, assert the backend at the real Qt boundary, and verify the staged package in an SDK-sanitized process before running the live decode workflow.

**Tech Stack:** C++20, Qt 6.8.4 Multimedia, FFmpeg 7.1.1, vcpkg, CMake/CTest/Ninja, PowerShell.

**Spec:** `docs/superpowers/specs/2026-08-20-windows-screen-capture-backend-design.md`

## Global Constraints

- Windows only; do not modify or run Linux project validation.
- Pin vcpkg to `9e593bb18ea69cc5095e012465dcd675a822ed0d` and FFmpeg to `7.1.1#6`.
- Keep FFmpeg dynamically linked and enable Qt deployment with `QT_DEPLOY_FFMPEG=ON`.
- The real capture regression must receive a valid `QVideoFrame`, not merely observe an active flag or a fake callback.
- Staged verification must clear developer Qt/vcpkg/QML search paths.
- Preserve and restore user settings and delete only uniquely created test output directories.

---

### Task 1: Real Windows Capture Contract and Reproducible Qt Input

**Files:**
- Create: `tests/integration/tst_windows_screen_capture.cpp`
- Create: `cmake/qt-ffmpeg/vcpkg.json`
- Modify: `tests/CMakeLists.txt`
- Modify: `scripts/provision-qt-source-windows.ps1`
- Modify: `scripts/WindowsBuildHelpers.psm1`
- Test: `tests/scripts/tst_windows_build_helpers.ps1`

**Interfaces:**
- Consumes: exact Qt source archive, exact vcpkg checkout, primary `QScreen`.
- Produces: dual-config Qt SDK with FFmpeg backend and CTest `windows_screen_capture`.

- [ ] **Step 1: Write the failing real-capture test**

Create a Windows-only QtTest that uses `QScreenCapture`, `QMediaCaptureSession`, and `QVideoSink`; reject `CapturingNotSupported`, start the primary screen, and require a valid frame within 10 seconds before stopping cleanly.

- [ ] **Step 2: Run the test against the current Qt SDK and verify RED**

Run:

```powershell
pwsh -NoProfile -File scripts/configure-windows.ps1 -QtRoot C:\cqp-684\Qt -VcpkgRoot .deps\vcpkg -Preset windows-release
cmake --build --preset windows-release --target tst_windows_screen_capture
ctest --test-dir out/build/windows-release -C Release --output-on-failure -R '^windows_screen_capture$'
```

Expected: the executable builds, then fails because the real capture error is `CapturingNotSupported`; a compile/configuration failure is not an acceptable RED.

- [ ] **Step 3: Add the fixed FFmpeg manifest and helper tests**

Use a dedicated manifest with this dependency contract:

```json
{
  "builtin-baseline": "9e593bb18ea69cc5095e012465dcd675a822ed0d",
  "dependencies": [{
    "name": "ffmpeg",
    "default-features": false,
    "features": ["avcodec", "avformat", "swresample", "swscale"]
  }],
  "overrides": [{"name": "ffmpeg", "version": "7.1.1", "port-version": 6}]
}
```

Pressure-test the PowerShell boundary with a good fixed checkout, a wrong checkout, missing FFmpeg artifacts, and a complete controlled fixture. Assert exit behavior and files, not source text.

- [ ] **Step 4: Provision FFmpeg and configure Qt with it**

Extend the Windows provisioner with absolute `VcpkgRoot` and `FfmpegInstallRoot` parameters. Run the manifest install for `x64-windows`, validate version/layout, configure Qt with `-DFFMPEG_DIR=<install-root>/x64-windows` and `-DQT_DEPLOY_FFMPEG=ON`, and copy the complete dependency license corpus into `share/licenses/qt/qt-ffmpeg/vcpkg`.

- [ ] **Step 5: Rebuild exact Qt 6.8.4 and verify GREEN**

Resume the trusted source/build directories, rebuild/install Release and Debug, then require Release/Debug FFmpeg multimedia plugins, FFmpeg DLLs, `qsvgicon/qsvgicond`, clean SPDX, and mirrored license hashes. Reconfigure/rebuild the test and require `windows_screen_capture` to pass.

- [ ] **Step 6: Run focused and full automated tests**

Run the helper test, `windows_screen_capture`, `qt_screen_capture_source` repeated 20 times, then full Windows Release CTest. All must pass without residual test or capture processes.

- [ ] **Step 7: Commit the independently working Qt/capture boundary**

Stage only the manifest, helper/provisioner, integration test, and test registration; review `git diff --cached --check`, then commit `fix: enable the Windows screen capture backend`.

---

### Task 2: Package the FFmpeg Backend and Prove Package Isolation

**Files:**
- Modify: `scripts/verify-windows.ps1`
- Modify: `cmake/Deploy.cmake` only if recursive deployment does not copy the discovered FFmpeg closure
- Modify: `README.md`
- Modify: `THIRD_PARTY_NOTICES.md`

**Interfaces:**
- Consumes: the Qt SDK produced by Task 1 and existing `cimbarpunk` install target.
- Produces: a self-contained staged Windows directory whose real capture test passes without developer SDK paths.

- [ ] **Step 1: Add staged-package failure assertions**

Before changing deployment, require the staged tree to contain the FFmpeg multimedia plugin, all DLLs resolved from that plugin, and authoritative FFmpeg dependency license files. Run the real capture test with `PATH`, Qt plugin paths, and QML paths restricted to the staged package and Windows system directories.

- [ ] **Step 2: Observe the packaging RED**

Run `scripts/verify-windows.ps1` against the rebuilt Qt SDK. Expected: any missing plugin, runtime DLL, license, or staged real-frame capability fails with the exact missing package boundary.

- [ ] **Step 3: Apply the minimal deployment fix**

Let `windeployqt` deploy the Qt FFmpeg plugin and Qt-deployed FFmpeg DLLs. If its output is incomplete, extend the existing recursive runtime-dependency scan using the Qt `bin` and multimedia plugin directories; do not hard-code a developer-machine lookup path into the final package.

- [ ] **Step 4: Run fresh package verification**

From a fresh CMake configure and empty validated staging directory, build all targets, run full CTest, install, run `windeployqt`, hash-check license mirrors, launch the tray process with sanitized environment, and run the real capture test against staged modules. Verify loaded module paths are inside staging and no process remains.

- [ ] **Step 5: Document and commit the package boundary**

Document fixed FFmpeg 7.1.1 provisioning and the package-size/backend rationale. Commit the verified packaging/doc changes as `build: package the Windows capture backend`.

---

### Task 3: Complete Deferred Windows Live Validation

**Files:**
- Modify: `.superpowers/sdd/2026-08-19-desktop-region-decoder/task-13-report.md` (ignored evidence only)
- Test: `tests/fixtures/cimbar/manifest.json`
- Test: `tests/manual/frame_player/cimbarpunk_frame_player.exe`
- Test: staged `cimbarpunk.exe`

**Interfaces:**
- Consumes: the staged package, deterministic cimbar fixture, tray UI, and uniquely created output directory.
- Produces: bounded evidence for selection, capture, decode, output, lifecycle, and cleanup.

- [ ] **Step 1: Establish reversible live-test state**

Record staged executable hashes and PIDs. Export the exact `HKCU\Software\Cimbarpunk\Cimbarpunk` key if it exists, create a unique output directory, and arrange `finally` restoration. Do not remove any pre-existing user output.

- [ ] **Step 2: Exercise selection interaction**

From the real tray menu, test drag selection, move, every one of eight resize handles, Enter accept, Escape cancel, negative/edge screen clamping, retained selection, and full-screen behavior. Assert window/selection geometry after each operation.

- [ ] **Step 3: Prove capture UI does not pollute frames**

Accept a partial region containing the fixture player. During active capture, assert the selection overlay is hidden or input-transparent/non-focused as designed and cannot appear in the captured crop.

- [ ] **Step 4: Decode the deterministic fixture end to end**

Run the frame player in manifest order at 100 ms. Require output completion, byte count, and SHA-256 to match `source.bin`, then require automatic capture stop and the tray to return to idle.

- [ ] **Step 5: Exercise retained, duplicate, and manual-stop paths**

Start again with retained selection, decode to the same directory and require non-overwriting duplicate numbering. Start a third session, manually stop before completion, and require no committed or orphan temporary output.

- [ ] **Step 6: Exit and clean up through the real tray**

Use the real tray Exit command. Require player/application termination and no residual `cimbarpunk`, frame-player, test, capture, CMake, Ninja, or `rcc` processes. Restore the registry key exactly and remove only unique test directories.

- [ ] **Step 7: Record evidence and hand off**

Record commands, hashes, geometries, output paths, and limitations in the ignored report. Re-run `git status --short` and report the final staged artifact path and SHA-256 without claiming Linux validation.
