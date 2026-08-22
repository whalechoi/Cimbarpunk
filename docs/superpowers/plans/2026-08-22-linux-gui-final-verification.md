# Linux GUI Final Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:executing-plans` to implement this plan task-by-task. This
> repository requires inline execution on `main`: do not create subagents,
> branches, worktrees, or commits. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** Install and validate a reproducible Ubuntu/Xfce/X11 GUI environment,
then prove Cimbarpunk builds, installs, captures, decodes, and runs from the
system tray on that environment.

**Architecture:** Keep the application architecture unchanged. Extend the
Linux build boundary with strict Qt XCB/FFmpeg provisioning checks, one
Linux-only real-capture QtTest labeled `linux_gui`, a display-aware verification
script, and evidence-backed documentation. Run the final GUI exercise in an
isolated Xvfb-backed Xfce/D-Bus session while leaving xrdp enabled for later
interactive access.

**Tech Stack:** Ubuntu 24.04.4 LTS, Bash, Xfce, Xorg/Xvfb, xrdp, Qt 6.8.4,
Qt Multimedia FFmpeg, C++20, CMake 3.25+, Ninja, CTest, vcpkg, xdotool.

**Spec:**
`docs/superpowers/specs/2026-08-22-linux-gui-final-verification-design.md`

## Global Constraints

- Work directly on `main`; do not create branches/worktrees or commit.
- Preserve all pre-existing tracked and untracked worktree changes.
- Qt must remain exactly 6.8.4 from the documented official source archive
  with SHA-256
  `1da37a32a583e7856d6fc13357c8ff6ad3ef7b877b8d276713b85026426d5246`.
- vcpkg must remain at
  `9e593bb18ea69cc5095e012465dcd675a822ed0d`.
- `libcimbar` must remain at
  `c509e0bb142bfd20e22583fb96f520e8083f3fba` with nested samples initialized.
- Use the `linux-release` preset and `out/build/linux-release` /
  `out/install/linux-release` paths.
- Linux claims are limited to the actual Ubuntu 24.04.4 Xfce/X11/Xvfb
  environment. Wayland, a physical Linux display, and macOS remain unverified.
- Cleanup may target only validated task-created paths and processes.

---

### Task 1: Harden the Linux Qt GUI SDK Contract

**Files:**

- Modify: `scripts/provision-qt-source-linux.sh`
- Modify: `scripts/configure-linux.sh`

**Interfaces:**

- Consumes: `CIMBARPUNK_QT_ROOT`, `VCPKG_ROOT`, and the fixed Qt source build.
- Produces: a configure-time contract requiring Qt 6.8.4, SVG icon support,
  XCB QPA, and the FFmpeg Multimedia plugin.

- [ ] **Step 1: Record the current negative probe**

Run on the remote host:

```bash
qt=/home/whale/.local/share/cimbarpunk/Qt/6.8.4/gcc_64
test ! -f "$qt/plugins/platforms/libqxcb.so"
test -f "$qt/plugins/multimedia/libffmpegmediaplugin.so"
```

Expected: both assertions pass, proving that the current SDK has Multimedia
but cannot start an XCB Qt application.

- [ ] **Step 2: Add required plugin checks to configuration**

Extend `required_files` in `scripts/configure-linux.sh` with:

```bash
"$CIMBARPUNK_QT_ROOT/plugins/platforms/libqxcb.so"
"$CIMBARPUNK_QT_ROOT/plugins/multimedia/libffmpegmediaplugin.so"
```

Keep the existing exact version and fixed vcpkg checks unchanged.

- [ ] **Step 3: Add post-install SDK dependency validation**

After the existing SVG checks in `scripts/provision-qt-source-linux.sh`, require:

```bash
required_gui_plugins=(
    "$install_prefix/plugins/platforms/libqxcb.so"
    "$install_prefix/plugins/multimedia/libffmpegmediaplugin.so"
)
for plugin in "${required_gui_plugins[@]}"; do
    [[ -f $plugin ]] || { printf 'Required Linux GUI plugin is missing: %s\n' "$plugin" >&2; exit 1; }
    if missing=$(ldd "$plugin" | awk '/not found/ { print }') && [[ -n $missing ]]; then
        printf 'Unresolved dependencies for %s:\n%s\n' "$plugin" "$missing" >&2
        exit 1
    fi
done
```

- [ ] **Step 4: Check shell syntax and diff hygiene**

Run:

```powershell
bash -n scripts/provision-qt-source-linux.sh
bash -n scripts/configure-linux.sh
git diff --check
```

Expected: all commands exit zero.

### Task 2: Add a Real Linux GUI and Capture Integration Test

**Files:**

- Create: `tests/integration/tst_linux_screen_capture.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: a real XCB display, X11 system tray, `QScreenCapture`, and
  `QtScreenCaptureSource`.
- Produces: CTest `linux_screen_capture` with label `linux_gui`.

- [ ] **Step 1: Register the missing test first**

Add under `if(UNIX AND NOT APPLE)`:

```cmake
qt_add_executable(tst_linux_screen_capture
    integration/tst_linux_screen_capture.cpp
)
target_link_libraries(tst_linux_screen_capture PRIVATE
    cimbarpunk_core
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Test
    Qt6::Widgets
)
add_test(NAME linux_screen_capture COMMAND tst_linux_screen_capture)
set_tests_properties(linux_screen_capture PROPERTIES LABELS linux_gui)
```

Also add `tst_linux_screen_capture` to `cimbarpunk_tests` only on supported
Unix hosts.

- [ ] **Step 2: Verify that the registered target cannot build yet**

Transfer the worktree to the unique remote source directory, configure, and run:

```bash
cmake --build --preset linux-release --target tst_linux_screen_capture
```

Expected: failure because `tests/integration/tst_linux_screen_capture.cpp` is
not present.

- [ ] **Step 3: Implement the real GUI/capture assertions**

Create a QtTest with these slots and assertions:

```cpp
private slots:
    void hasXcbDisplayAndSystemTray();
    void receivesARealDesktopFrame();
    void productionCaptureSourceForwardsARealDesktopFrame();
```

`hasXcbDisplayAndSystemTray()` must compare
`QGuiApplication::platformName()` to `xcb`, require a valid primary screen,
require `QSystemTrayIcon::isSystemTrayAvailable()`, show a temporary tray icon,
process events, assert `isVisible()`, and hide it.

`receivesARealDesktopFrame()` must mirror the bounded Windows integration test:
reject `CapturingNotSupported`, attach `QScreenCapture` to `QVideoSink` through
`QMediaCaptureSession`, require `isActive()` within 5 seconds, require at least
one frame whose `toImage()` is non-null within 10 seconds, then stop within 5
seconds.

`productionCaptureSourceForwardsARealDesktopFrame()` must instantiate
`QtScreenCaptureSource`, record `activeChanged`, `frameReady`, and `failed`,
require an active notification and a non-null forwarded frame, then stop.

- [ ] **Step 4: Build the test after the GUI SDK is repaired**

Run:

```bash
cmake --build --preset linux-release --target tst_linux_screen_capture
```

Expected: build succeeds.

- [ ] **Step 5: Run the test in an X11 session**

Run inside the Xfce/D-Bus session:

```bash
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
  ctest --preset linux-release -L linux_gui --output-on-failure
```

Expected: `linux_screen_capture` passes and receives real display frames.

### Task 3: Split Offscreen and GUI Verification Paths

**Files:**

- Modify: `scripts/verify-linux.sh`
- Create: `scripts/verify-linux-gui.sh`

**Interfaces:**

- Consumes: the Linux preset and, for GUI verification, an existing X11
  `DISPLAY` with D-Bus and a system tray manager.
- Produces: a full non-GUI build/test/install verifier and a bounded GUI verifier.

- [ ] **Step 1: Protect the offscreen suite from GUI-only tests**

Change the CTest call in `scripts/verify-linux.sh` to:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    ctest --preset linux-release -LE linux_gui --output-on-failure
cmake --install out/build/linux-release
```

- [ ] **Step 2: Write the GUI verifier preconditions**

Create `scripts/verify-linux-gui.sh` with `set -euo pipefail`. Require absolute,
existing `CIMBARPUNK_QT_ROOT` and `VCPKG_ROOT`, non-empty `DISPLAY`, an available
session D-Bus, the exact Qt version, `libqxcb.so`, `libqsvgicon.so`, and
`libffmpegmediaplugin.so`. Reject any `ldd` line containing `not found` for the
application or required plugins.

- [ ] **Step 3: Run GUI CTest and a bounded installed-app launch**

The GUI verifier must run:

```bash
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
    ctest --preset linux-release -L linux_gui --output-on-failure

set +e
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
    timeout --signal=TERM --kill-after=5s 8s \
    out/install/linux-release/bin/cimbarpunk
launch_status=$?
set -e
[[ $launch_status -eq 124 ]] || {
    printf 'Installed tray application did not remain active for the bounded check: %s\n' \
        "$launch_status" >&2
    exit 1
}
```

The preceding GUI test makes tray availability an independent assertion, so a
timeout alone is never the sole startup evidence.

- [ ] **Step 4: Validate both scripts**

Run:

```bash
bash -n scripts/verify-linux.sh
bash -n scripts/verify-linux-gui.sh
```

Expected: both scripts parse successfully.

### Task 4: Install the Remote GUI and Repair Qt 6.8.4

**Files:** none in the repository.

**Interfaces:**

- Consumes: passwordless sudo and the existing validated Qt source/archive.
- Produces: Xfce/Xorg/Xvfb/xrdp plus a Qt 6.8.4 prefix with XCB and FFmpeg
  plugins.

- [ ] **Step 1: Install the deterministic package set**

Run non-interactively:

```bash
sudo apt-get update
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y \
  xfce4 xfce4-goodies xrdp xorgxrdp xvfb dbus-x11 x11-utils xdotool \
  imagemagick scrot ffmpeg \
  libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev libxrender-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev \
  libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev \
  libxcb-render0-dev libxcb-render-util0-dev libxcb-shape0-dev \
  libxcb-shm0-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-xinerama0-dev \
  libxcb-xinput-dev libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev
sudo systemctl enable --now xrdp
```

Do not set or print a password.

- [ ] **Step 2: Validate the exact Qt rebuild target before cleanup**

Resolve and require:

```bash
work=/home/whale/.local/share/cimbarpunk/qt-source-6.8.4
build=$(realpath -m "$work/build-release")
[[ $build == "$work/build-release" ]]
[[ -f "$work/qt-everywhere-opensource-src-6.8.4.tar.xz" ]]
sha256sum --check <<'EOF'
1da37a32a583e7856d6fc13357c8ff6ad3ef7b877b8d276713b85026426d5246  /home/whale/.local/share/cimbarpunk/qt-source-6.8.4/qt-everywhere-opensource-src-6.8.4.tar.xz
EOF
```

- [ ] **Step 3: Recreate only the validated Qt build directory**

Remove `/home/whale/.local/share/cimbarpunk/qt-source-6.8.4/build-release`
only after Step 2 succeeds. Copy the current worktree's provisioner to the
task-owned path
`/home/whale/.local/share/cimbarpunk/qt-source-6.8.4/provision-qt-source-linux.cimbarpunk.sh`,
run it with the following arguments, and remove only that copied script after
it exits:

```bash
./scripts/provision-qt-source-linux.sh \
  /home/whale/.local/share/cimbarpunk/Qt/6.8.4/gcc_64 \
  /home/whale/.local/share/cimbarpunk/qt-source-6.8.4
```

- [ ] **Step 4: Record GUI and SDK evidence**

Record `systemctl is-enabled/is-active xrdp`, Qt version, the QPA and Multimedia
plugin paths, `ldd` checks, CMake/GCC/Ninja versions, and the Qt configuration
summary lines for XCB, FFmpeg, and PipeWire.

### Task 5: Perform a Fresh Remote Release Build, Test, and Install

**Files:** none beyond the current repository changes.

**Interfaces:**

- Consumes: the current local worktree plus fixed `libcimbar` contents.
- Produces: a unique remote source tree and `out/install/linux-release`.

- [ ] **Step 1: Create and validate a unique remote directory**

Create `/home/whale/cimbarpunk-linux-gui.XXXXXX`, canonicalize it, and reject it
unless it matches `^/home/whale/cimbarpunk-linux-gui\.[A-Za-z0-9]+$`.

- [ ] **Step 2: Transfer the exact current worktree**

Use a tar stream that excludes `.git`, `out`, and `vcpkg_installed`, then archive
the fixed `external/libcimbar` worktree separately so nested source and fixtures
are present. Verify the remote files and locally recorded commit IDs; do not
copy Git metadata or local build outputs.

- [ ] **Step 3: Run the full non-GUI verifier**

Run:

```bash
export CIMBARPUNK_QT_ROOT=/home/whale/.local/share/cimbarpunk/Qt/6.8.4/gcc_64
export VCPKG_ROOT=/home/whale/.local/share/cimbarpunk/vcpkg
./scripts/verify-linux.sh
```

Expected: fresh configure, all required targets build, every non-GUI test
passes, and installation completes.

- [ ] **Step 4: Inspect installed output**

Require the installed binary, SVG icon, QML source, project/license notices,
Qt license corpus, Qt SPDX corpus, libcimbar licenses, and vcpkg copyright
files. Run `ldd` on the installed binary and reject unresolved libraries.

### Task 6: Run the Xfce/X11 GUI and End-to-End Decode Exercise

**Files:** evidence is stored beneath the unique remote validation directory.

**Interfaces:**

- Consumes: installed app, frame player, fixtures, Xvfb, Xfce panel, D-Bus,
  and xdotool.
- Produces: GUI test output, bounded logs/screenshots, decoded output, and
  process-cleanup evidence.

- [ ] **Step 1: Start an isolated graphical session**

Use a free display number, a task-specific runtime directory, Xvfb at
`1920x1080x24`, and `dbus-run-session startxfce4`. Wait until `xdpyinfo`, the
window manager, and the X11 tray selection owner are all available. Do not reuse
or terminate an unrelated display session.

- [ ] **Step 2: Run automated GUI verification**

Inside the session, export `QT_QPA_PLATFORM=xcb`,
`QT_QUICK_BACKEND=software`, the fixed Qt plugin/library paths, and isolated
XDG config/data/cache directories. Run `scripts/verify-linux-gui.sh` and retain
its CTest and plugin-loader logs.

- [ ] **Step 3: Launch player and product**

Start `cimbarpunk_frame_player tests/fixtures/cimbar`, wait for the window titled
`Cimbarpunk Test Frame Player`, move it wholly inside the virtual screen, then
start the installed `cimbarpunk`. Require the product process to remain alive,
no critical error dialog, and no normal Cimbarpunk main window.

- [ ] **Step 4: Exercise selection controls**

Use the tray menu to start selection. With xdotool, draw a rectangle over the
fixture player, move it, resize it from an edge and a corner, press Escape, and
assert the overlay closes. Start again, select the fixture content, and press
Enter. Capture screenshots before confirmation and after capture begins to
prove that the controls disappear or become capture-transparent.

- [ ] **Step 5: Verify capture, decode, and safe output**

Wait boundedly for the output file. Require the capture and decode log states,
the application returning to idle, no remaining `.part` file, and:

```bash
decoded_path="$test_home/Downloads/Cimbarpunk/source.bin"
[[ -f $decoded_path ]]
printf '%s  %s\n' \
  1ef72c72c51c4bc31e7ebc512f72e67d0a6be1fe8184808ef575c45b56ac4ef6 \
  "$decoded_path" | sha256sum --check --strict
```

The actual absolute path is captured from the isolated output directory before
running the check; no user directory is scanned broadly.

- [ ] **Step 6: Clean up only task-created processes**

Record product, player, Xfce session, D-Bus, and Xvfb PIDs when started. Stop
those exact PIDs in reverse order, wait for exit, and assert none remains. Leave
xrdp running because it is an installed deliverable.

### Task 7: Update Linux Support Documentation and Evidence

**Files:**

- Modify: `AGENTS.md`
- Modify: `README.md`
- Create: `docs/verification/2026-08-22-linux-gui.md`

**Interfaces:**

- Consumes: the exact outputs and hashes from Tasks 4–6.
- Produces: reproducible instructions and narrowly scoped platform claims.

- [ ] **Step 1: Update the repository rules**

Replace the blanket Linux build-only statement in `AGENTS.md` with a rule that
permits claims only for the exact GUI stack actually verified. Preserve explicit
prohibitions on inferring Wayland, physical display, or other desktop support.

- [ ] **Step 2: Update the README platform boundary and prerequisites**

Document Ubuntu 24.04.4, Xfce/X11/Xvfb, the complete XCB development package
set, xrdp's role, the XCB/FFmpeg plugin requirements, non-GUI and GUI verification
commands, and known limitations. Remove only statements contradicted by fresh
evidence.

- [ ] **Step 3: Write the dated evidence record**

Record local source commit, dirty worktree files, remote OS/kernel, compiler,
CMake/Ninja/Qt/vcpkg/libcimbar versions, Qt config features, test names/counts,
install path, GUI session type/geometry, decoded SHA-256, screenshots/log paths,
cleanup result, and the exact unverified boundaries. Do not include secrets or
unnecessary sensitive paths.

- [ ] **Step 4: Run documentation and diff checks**

Run:

```powershell
rg -n "Linux GUI|Xfce|Xvfb|Wayland|macOS" AGENTS.md README.md docs/verification/2026-08-22-linux-gui.md
git diff --check
```

Expected: all claims agree and no whitespace errors are reported.

### Task 8: Final Regression and Handoff

**Files:** all task-modified files.

**Interfaces:**

- Consumes: final worktree and the repaired remote GUI environment.
- Produces: fresh final verification evidence and an uncommitted handoff.

- [ ] **Step 1: Retransfer the final worktree to a second unique remote path**

Do not reuse the earlier CMake cache. Verify transfer boundaries and dependency
commit IDs again.

- [ ] **Step 2: Run final offscreen and GUI verification**

Run `scripts/verify-linux.sh`, then run `scripts/verify-linux-gui.sh` inside a
fresh isolated Xfce/X11 session. Repeat the fixture capture/decode hash check if
any runtime, build, script, test, or documentation assumption changed.

- [ ] **Step 3: Run completion checks**

Run:

```powershell
git branch --show-current
git status --short
git diff --check
```

Expected: branch is `main`, only task files are modified/untracked, there are no
agent-created commits, and every success claim has fresh command output.

- [ ] **Step 4: Report the handoff**

Report modified files, package/service state, exact commands, CTest count and
result, installed artifact path, GUI/capture/decode evidence, cleanup result,
and unverified platforms. Leave all changes uncommitted for the user.
