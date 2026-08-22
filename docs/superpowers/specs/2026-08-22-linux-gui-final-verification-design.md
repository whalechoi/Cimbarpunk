# Linux GUI Final Verification Design

**Date:** 2026-08-22

**Status:** Approved in chat; implemented and independently reviewed

## Problem

Cimbarpunk currently has repeatable Linux Release builds and offscreen tests on
`whale@192.168.43.201`, but the host has no desktop session and its installed
Qt 6.8.4 prefix does not contain the XCB platform plugin. The repository
therefore correctly avoids claiming that the Linux tray, selection overlay, or
desktop capture works.

The final Linux verification must install a usable GUI on that sandbox, repair
the exact Qt 6.8.4 SDK without floating any pinned dependency, perform a clean
Release build and installation, and exercise the product through a real X11
display server. Documentation must state precisely what was and was not
verified.

## Scope

This work covers:

- Ubuntu 24.04.4 LTS x86_64 at `whale@192.168.43.201`;
- Xfce running on Xorg/Xvfb, with D-Bus and an X11 system-tray manager;
- xrdp installation and service enablement for later interactive access;
- the official Qt 6.8.4 source build already prescribed by the repository;
- the pinned vcpkg checkout
  `9e593bb18ea69cc5095e012465dcd675a822ed0d`;
- the pinned `libcimbar` checkout
  `c509e0bb142bfd20e22583fb96f520e8083f3fba`;
- clean Linux Release configuration, build, CTest, installation, runtime
  dependency inspection, GUI startup, overlay, tray, screen capture, decode,
  and output verification;
- updates to `AGENTS.md`, `README.md`, Linux verification scripts/tests where
  required, and a dated verification record.

This work does not claim:

- Wayland or XDG Desktop Portal capture support;
- behavior on a physical Linux monitor or GPU;
- compatibility with desktop environments other than the tested Xfce setup;
- macOS build or runtime support;
- Linux support beyond facts observed on the named sandbox.

## Selected Approach

Install the Xfce desktop stack, xrdp, Xorg/Xvfb, D-Bus X11 support, an X11 tray
manager, and bounded GUI automation tools. Automated verification runs in a
fresh Xvfb-backed Xfce session so the display geometry, process lifetime, and
test inputs are deterministic. xrdp remains enabled as a separate interactive
entry point, but an xrdp login is not used as proof of automated verification.

The existing Qt source and installed prefix are preserved. After installing the
missing XCB development packages, only the exact, validated Qt build directory
may be recreated. Qt is then configured from the already checksum-bound 6.8.4
source and installed into the documented `gcc_64` prefix. The result must
contain both `plugins/platforms/libqxcb.so` and
`plugins/multimedia/libffmpegmediaplugin.so`, report Qt 6.8.4, and have no
unresolved runtime dependencies.

This approach is preferred over a full GNOME/Wayland install because the
current application has no separately implemented portal authorization flow
and the SSH sandbox cannot provide a reliable unattended Wayland consent
interaction. It is preferred over Xvfb alone because the requested environment
should also provide a persistent desktop stack and an interactive remote login
service.

## Repository Changes

### Linux SDK provisioning

The Linux Qt provisioner will retain the official archive URL and SHA-256,
module set, Release-only ABI, trusted extraction marker, license corpus, and
SPDX validation. Its final checks will also reject a Qt installation that lacks
the XCB QPA plugin or FFmpeg Multimedia plugin, or whose required shared
objects have unresolved dependencies.

Linux prerequisites in `README.md` will distinguish build-time XCB development
packages from runtime packages. The documented package list must be sufficient
to reproduce `libqxcb.so`; it must not rely on packages that happened to be
installed earlier on the sandbox.

### Automated tests

A Linux-only Qt integration test will use the real `QScreenCapture`,
`QMediaCaptureSession`, and `QVideoSink` against the current X11 display. It
must:

1. resolve a primary `QScreen`;
2. reject `QScreenCapture::CapturingNotSupported`;
3. become active within a bounded timeout;
4. receive a valid frame convertible to `QImage`;
5. repeat the check through the production `QtScreenCaptureSource`;
6. stop cleanly without leaving capture resources active.

The test is labeled `gui`. The normal SSH/offscreen suite excludes this label,
so it remains useful on hosts without a display. A GUI verification script
requires an existing X11 `DISPLAY` and D-Bus session, runs only GUI-labeled
tests, and performs a bounded launch of the installed application. It must fail
clearly if XCB, the tray manager, the multimedia backend, or the display is
unavailable.

### End-to-end GUI exercise

The final sandbox verification will start a clean Xvfb display and Xfce/D-Bus
session, then launch the committed frame player and the installed Cimbarpunk
binary. Bounded GUI automation will exercise:

- tray-only startup without a normal application window;
- visibility and usability of the tray menu;
- starting capture from the tray;
- drawing a selection, moving it, resizing it from representative edges and
  corners, cancelling with Escape, and confirming with Enter;
- hiding or making the overlay capture-transparent before real capture;
- delivery of real X11 desktop frames through Qt Multimedia;
- decoding the fixture stream, stopping at first completion, and writing the
  output through the existing safe output path;
- SHA-256 equality between decoded output and the fixture source;
- absence of leftover application, player, capture, or test processes.

Screenshots may be retained as visual evidence, but they must not contain
decoded file contents, screenshot pixel dumps in logs, or unnecessary user
paths. Process logs must remain bounded.

## Build and Verification Flow

1. Confirm local `main` and record the pre-existing worktree state.
2. Confirm remote OS, available disk/memory, passwordless sudo, Qt/vcpkg
   versions, and the absence or presence of desktop components.
3. Install the exact GUI, XCB development, runtime, and automation package set.
4. Reconfigure and rebuild Qt 6.8.4 from the validated official source; verify
   XCB, SVG icon, and FFmpeg Multimedia plugins and their dependency closure.
5. Create a unique directory strictly below `/home/whale`, transfer the current
   main worktree and fixed `libcimbar` contents without Git metadata or local
   build artifacts, and verify the submodule commit used as the source.
6. Run a fresh `linux-release` configure, build the application/tests/player,
   run all non-GUI CTest cases, install to `out/install/linux-release`, and
   inspect the installed artifacts and license tree.
7. Start the isolated X11 desktop and run the GUI integration and end-to-end
   exercises.
8. Stop only processes created for this verification and retain evidence paths
   under the unique remote validation directory.
9. Update repository documentation and the dated evidence record, then rerun
   the affected local static checks and remote verification using the final
   worktree.

## Error Handling and Safety

- Every remote script or command sequence uses bounded timeouts and exits on
  failure; a timed-out tray application is considered success only when its
  startup conditions and lack of an error window were independently checked.
- Destructive cleanup is limited to explicitly resolved paths beneath the
  Cimbarpunk SDK work directory or the unique validation directory. The
  absolute path and parent containment are checked before removal.
- Existing Qt source archives, trusted source trees, vcpkg, unrelated home
  directories, and unrelated user processes are not removed.
- xrdp is exposed only according to the sandbox's existing network/firewall
  policy. This task does not set or disclose a user password.
- A failed X11 capture is reported as a failure with backend diagnostics; it is
  not reclassified as a successful offscreen test.

## Documentation Outcome

`AGENTS.md` will replace the blanket Linux build-only restriction with an
evidence rule: only the exact desktop/session/backend combinations actually
verified may be claimed. `README.md` will document the installed GUI stack,
reproducible build and verification commands, xrdp boundary, X11 result, and
remaining Wayland/physical-display/macOS limitations. A dated verification
record will include versions, commands, test counts, output hashes, artifact
paths, and any failed or skipped checks.

## Acceptance Criteria

- The remote host provides Xfce/Xorg components and an enabled xrdp service.
- Qt reports exactly 6.8.4 and supplies working XCB, SVG icon, and FFmpeg
  Multimedia plugins with no missing shared libraries.
- A fresh Linux Release build and installation complete from the transferred
  source and fixed dependencies.
- Every non-GUI CTest case passes in offscreen mode.
- The GUI-labeled real capture test passes in the isolated X11 session.
- The installed tray application starts without a main window, the selection
  overlay responds to the required controls, and an actual captured fixture
  sequence decodes to a file with the expected SHA-256.
- Verification-created processes are stopped and no unrelated processes are
  terminated.
- `AGENTS.md`, `README.md`, scripts/tests, and the verification record agree
  with the observed Linux support boundary.
- No branch, worktree, commit, push, merge, rebase, or tag is created.
