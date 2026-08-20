# Windows Screen Capture Backend Design

**Problem:** The packaged Windows application cannot capture the desktop. A real Qt probe reports `QScreenCapture::CapturingNotSupported`, and the Qt multimedia loader reports that only the native `windows` backend is available. The exact Qt 6.8.4 source shows that the Windows native backend does not implement screen capture; the FFmpeg backend does.

**Approved scope:** Repair Windows only. Linux project validation and macOS remain deferred. Keep the existing `QtScreenCaptureSource`, session, overlay, decoder, and output architecture unchanged.

## Design

1. Provision FFmpeg 7.1.1 from the repository's exact vcpkg checkout `9e593bb18ea69cc5095e012465dcd675a822ed0d`. Use a dedicated manifest and dynamic `x64-windows` triplet so Qt can deploy the runtime libraries.
2. Configure exact Qt 6.8.4 with `FFMPEG_DIR` and `QT_DEPLOY_FFMPEG=ON`. Reject a Qt build unless the configure result enables FFmpeg, the FFmpeg multimedia plugin exists in both Release and Debug, and the required FFmpeg runtime libraries are installed.
3. Preserve every vcpkg `copyright` file from the FFmpeg provisioning closure under the Qt SDK license corpus. The existing deployment license mirror will then ship and hash-check those authoritative texts.
4. Add a Windows-only integration test that instantiates real `QScreenCapture`, attaches a real `QVideoSink`, selects the primary screen, starts capture, and receives at least one valid frame within a bounded timeout. This test must fail with the current native-only SDK and pass with the rebuilt SDK.
5. Extend staged-package verification to require the FFmpeg plugin/runtime closure and run the real capture integration test with the staged package's Qt/plugin paths. A developer SDK on `PATH` must not be able to rescue a broken package.
6. After automated verification, resume the previously unfinished Windows tests: selection drag/move/eight handles, Enter/Escape and screen bounds, capture overlay pollution, live fixture playback/decode/hash, automatic and manual stop, retained selection, duplicate numbering, tray Exit, and residual-process checks.

## Constraints

- Use FFmpeg 7.1.1, not the newer FFmpeg 8 port at the fixed baseline.
- Do not add FFmpeg to the application's own link interface; it remains a Qt Multimedia backend dependency.
- Do not change Linux scripts or claim Linux project validation in this work.
- Do not launch or modify user data without bounded cleanup and settings restoration.
- Package size growth from FFmpeg DLLs is accepted.
