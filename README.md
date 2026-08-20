# Cimbarpunk

Cimbarpunk 是一个常驻系统托盘的桌面 cimbar 解码客户端。它不打开主窗口，而是持续捕获用户在单个屏幕上选定的桌面区域，把动态 cimbar 帧送入固定版本的 `libcimbar`，并在第一个完整文件解码成功后自动停止。

当前里程碑只实现解码。项目自有代码以 `GPL-3.0-only` 发布；固定依赖及其许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 平台状态

| 平台 | M1 状态 | 边界 |
|---|---|---|
| Windows 10/11 x64 | 完整构建、自动测试、部署与有限人工启动验证 | 主要支持平台；需要桌面捕获与系统托盘权限 |
| Linux x64 | 仅 SSH 构建和 `offscreen` 自动测试 | 未运行托盘、覆盖层或桌面捕获；不宣称 Linux GUI 可用 |
| macOS | 未构建、未测试 | 当前不宣称支持 |

Linux Wayland 下的屏幕捕获通常由桌面门户/PipeWire、合成器授权和会话实现共同决定。当前实现尚未接入或验证 Wayland portal 的交互流程，因此即使构建通过，也不能据此推断 GUI 捕获可用。

## 固定工具链与依赖

- C++20、CMake 3.25+、Ninja。
- Qt **6.8.4 exact**：Core、Gui、Widgets、Quick、Qml、Multimedia、Svg、Test；发布包必须包含 `iconengines/qsvgicon`。
- Windows Qt Multimedia：动态 FFmpeg **7.1.1#6 exact**，仅启用 `avcodec`、`avformat`、`swresample`、`swscale`；这是 Qt 6.8.4 在 Windows 上提供 `QScreenCapture` 的必需后端。
- `libcimbar`：`c509e0bb142bfd20e22583fb96f520e8083f3fba`。
- vcpkg baseline：`9e593bb18ea69cc5095e012465dcd675a822ed0d`。
- Windows：Visual Studio 2022 x64 C++ Build Tools 和 Windows SDK。
- Linux：GCC、构建工具，以及 M1 SSH/offscreen 验证所需的最小 Qt 构建依赖和运行时探针；这不是完整 XCB QPA GUI 开发环境，不能据此推断 Linux GUI 已验证。

克隆后必须初始化固定子模块：

```bash
git submodule update --init --recursive
git -C external/libcimbar rev-parse HEAD
```

第二条命令应输出上述 `libcimbar` 提交。

vcpkg 也必须固定到清单中的 baseline，而不是使用浮动的默认分支。Windows PowerShell：

```powershell
$vcpkg = 'C:\src\vcpkg-cimbarpunk'
git clone https://github.com/microsoft/vcpkg.git $vcpkg
git -C $vcpkg checkout --detach 9e593bb18ea69cc5095e012465dcd675a822ed0d
& "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics
$actual = (git -C $vcpkg rev-parse HEAD).Trim()
if ($actual -ne '9e593bb18ea69cc5095e012465dcd675a822ed0d') { throw "Unexpected vcpkg commit: $actual" }
```

Linux：

```bash
git clone https://github.com/microsoft/vcpkg.git /home/whale/.local/share/cimbarpunk/vcpkg
git -C /home/whale/.local/share/cimbarpunk/vcpkg checkout --detach 9e593bb18ea69cc5095e012465dcd675a822ed0d
/home/whale/.local/share/cimbarpunk/vcpkg/bootstrap-vcpkg.sh -disableMetrics
test "$(git -C /home/whale/.local/share/cimbarpunk/vcpkg rev-parse HEAD)" = 9e593bb18ea69cc5095e012465dcd675a822ed0d
```

### Qt 6.8.4 官方源码回退

aqtinstall 3.3.0 的官方 Windows/Linux desktop 元数据没有 Qt 6.8.4 二进制套件，不能使用计划中的 `aqt install-qt ... 6.8.4` 命令。已验证的回退是 Qt 官方源码包：

- Windows ZIP：`qt-everywhere-opensource-src-6.8.4.zip`，SHA-256 `f56ea93356ece3bca727815233b86d9e1242d28d418074389fadce683227c87c`。
- Linux tar.xz：`qt-everywhere-opensource-src-6.8.4.tar.xz`，SHA-256 `1da37a32a583e7856d6fc13357c8ff6ad3ef7b877b8d276713b85026426d5246`。
- 官方目录：<https://download.qt.io/official_releases/qt/6.8/6.8.4/single/>。

Windows Debug 构建不能链接仅 Release 的 Qt SDK；否则 MSVC Debug/Release CRT 与 STL ABI 会混用。`scripts/provision-qt-source-windows.ps1` 使用 `Ninja Multi-Config`、`-debug-and-release`，先后构建并安装 Release 和 Debug，再验证两套 CMake exports 以及 `qsvgicon/qsvgicond`。源码和构建目录应使用短绝对路径（必要时先用 `subst` 映射短盘符）以避开 Windows 最大路径问题。已验证的布局为：

```powershell
pwsh -File scripts/provision-qt-source-windows.ps1 `
  -Archive 'Q:\downloads\qt-everywhere-opensource-src-6.8.4.zip' `
  -SourceDirectory 'Q:\qt-everywhere-src-6.8.4' `
  -BuildDirectory 'Q:\qt-build-6.8.4' `
  -InstallPrefix 'Q:\Qt\6.8.4\msvc2022_64' `
  -VcpkgRoot 'C:\src\vcpkg-cimbarpunk' `
  -FfmpegInstallRoot 'Q:\ffmpeg-7.1.1'
```

脚本校验 ZIP 后在 `SourceDirectory` 的同级临时目录自行解压，确认关键源码和许可证存在、写入绑定归档哈希的可信标记，再原子移动到目标。源码父目录必须位于所有 Git 工作树之外；构建时还设置 Git ceiling，防止 Qt SBOM 向上发现宿主仓库。脚本拒绝未带精确标记的已有源码、重解析源码，以及源码、构建、安装、FFmpeg 目录相同或互为祖先。它先用仓库内固定清单和固定 vcpkg checkout 构建动态 FFmpeg 7.1.1#6，再强制以 `FEATURE_ffmpeg=ON` 重建 Qt；Release/Debug 插件、五个 FFmpeg DLL、许可证镜像或 SPDX 任一不完整都会使 provision 失败。构建前应保证目标路径没有另一套不完整 Qt。

```text
<QtRoot>/bin/Qt6Core.dll
<QtRoot>/bin/Qt6Cored.dll
<QtRoot>/lib/cmake/Qt6Core/Qt6CoreTargets-release.cmake
<QtRoot>/lib/cmake/Qt6Core/Qt6CoreTargets-debug.cmake
<QtRoot>/plugins/iconengines/qsvgicon.dll
<QtRoot>/plugins/iconengines/qsvgicond.dll
<QtRoot>/plugins/multimedia/ffmpegmediaplugin.dll
<QtRoot>/plugins/multimedia/ffmpegmediaplugind.dll
<QtRoot>/bin/avcodec-61.dll
```

Linux M1 只使用 Release 预设；`scripts/provision-qt-source-linux.sh ABSOLUTE_PREFIX ABSOLUTE_WORKDIR` 以可续传 `.part` 下载、校验后原子改名，并在临时目录完整解压后带完成标记原子移动源码，再构建同一官方源码的 Release 套件。脚本拒绝源码、构建、安装目录相同或祖先重叠；中断后只清理严格确认位于工作目录内的临时解压目标。若以后需要 `linux-debug`，应在独立前缀构建 Debug Qt，不要把不同 ABI 的库混入同一非多配置 Unix 前缀。

## Windows 构建与验证

准备两个绝对路径：

```powershell
$qt = 'D:\SDK\Qt\6.8.4\msvc2022_64'
$vcpkg = 'C:\src\vcpkg'
& .\scripts\configure-windows.ps1 -QtRoot $qt -VcpkgRoot $vcpkg -Preset windows-debug
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build --preset windows-debug --target cimbarpunk cimbarpunk_tests
ctest --preset windows-debug --output-on-failure
```

这里必须在同一个 PowerShell 进程中用 `&` 调用配置脚本；不要用 `pwsh -File` 后再从父 shell 构建，因为子进程导入的 Visual Studio 环境不会回传。脚本通过 `vswhere` 导入并验证 VS 2022 x64 开发环境，检查 CMake、Ninja、精确 Qt、Svg/qsvgicon 和 vcpkg，并用 `cmake --fresh` 防止旧 cache 保留另一套 SDK。若 OpenCV 首次配置只因 pkg-config 路径解析失败，脚本会发现并实际运行 `pkgconf.exe`/`pkg-config.exe --version`，再以不带字面引号的 `PKG_CONFIG` 只重试一次；其他失败不会被掩盖。

完整 Release 验证和暂存部署：

```powershell
pwsh -File scripts/verify-windows.ps1 -QtRoot $qt -VcpkgRoot $vcpkg
```

输出目录是 `out/install/windows-release`。验证脚本会以全新 CMake cache 重新配置、构建应用/测试/播放器、运行 CTest、安装，再调用 `windeployqt --release --qmldir src/selection/qml --include-plugins qsvgicon`，并检查 Qt Svg、`qsvgicon`、平台插件、FFmpeg Multimedia 插件、五个 FFmpeg DLL、传递 DLL 和逐文件哈希一致的完整许可证材料。随后真实捕获探针会在暂存包 Qt/插件优先且开发 Qt/QML 路径已清理的环境中捕获一帧桌面，并核对已加载的 FFmpeg 插件确实来自暂存目录。最后的有界启动验证托盘应用没有主窗口且关键模块来自暂存目录；脚本用强制终止收尾，这不等同于人工验证托盘“退出”菜单。

## Linux 构建验证

Ubuntu M1 SSH/offscreen 构建主机的最小依赖如下；其中 XCB 包仅用于运行时探针，并非完整的 XCB QPA GUI 开发依赖。Linux GUI 尚未验证：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build curl git pkg-config python3-venv \
  p7zip-full zip unzip libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 \
  libxcb-cursor0 libxcb-xinerama0 libxcb-keysyms1 libxcb-image0 \
  libxcb-render-util0 libxcb-icccm4
```

在用户目录安装 Qt 源码套件和固定 vcpkg 后运行：

```bash
export CIMBARPUNK_QT_ROOT=/home/whale/.local/share/cimbarpunk/Qt/6.8.4/gcc_64
export VCPKG_ROOT=/home/whale/.local/share/cimbarpunk/vcpkg
./scripts/verify-linux.sh
```

脚本要求两个变量均为已存在的绝对路径，配置当前 `linux-release` preset，构建应用、测试和手动播放器，并以 `QT_QPA_PLATFORM=offscreen`、`QT_QUICK_BACKEND=software` 运行非 GUI 测试。它不会启动托盘、选择覆盖层或桌面捕获。

## CMake presets

仓库保留四个当前 preset：`windows-debug`、`windows-release`、`linux-debug`、`linux-release`。构建目录统一位于 `out/build/<preset>`；不要手工把另一套 Qt/vcpkg 缓存复用于已有 preset。切换 SDK 时删除对应的单个构建目录后重新配置。

## 使用

应用启动后只有系统托盘图标。右键菜单提供：

- `开始捕获…`：在鼠标所在屏幕（找不到时用主屏）进入全屏选择覆盖层；
- `停止捕获`：终止当前任务；
- `打开保存目录`、`更改保存目录…`；
- `退出`。

覆盖层操作：鼠标拖拽创建区域；拖动内部移动；拖动八个边/角手柄调整；`Enter` 确认并开始捕获；`Esc` 取消。选择始终限制在当前单屏范围。确认后覆盖层会进入对桌面捕获透明的隐藏模式；成功、失败或取消后停止捕获，选择会按屏幕身份保存，下一次在同一屏幕恢复。

默认输出目录是系统下载目录中的 `Cimbarpunk`。发送端文件名会去除路径和 Windows 危险名称；同名文件永不覆盖，依次生成 `name (1).ext`、`name (2).ext`。写入先在同一目录使用 `.cimbarpunk-<UUID>.part`，校验成功后原子改名；启动时只清理应用登记且匹配该格式的临时文件。

日志位于 Qt `AppLocalDataLocation` 下的 `cimbarpunk.log`，最多约 1 MiB，并保留 `.1`～`.3`。日志只记录状态、尺寸和像素格式等诊断，不写入截图像素或解码内容。

Windows 首次捕获若失败，请检查“设置 → 隐私和安全性 → 屏幕截图和应用录制”以及系统托盘可用性。Linux 构建通过不代表已经取得 X11/Wayland 捕获权限。

## 手动夹具播放器

播放器仅在 `BUILD_TESTING=ON` 时生成，不安装到发布目录：

```powershell
cmake --build --preset windows-release --target cimbarpunk_frame_player
out\build\windows-release\cimbarpunk_frame_player.exe tests\fixtures\cimbar
```

它只接受 `orderedFrames` 中不含目录/绝对路径的 PNG basename，按清单顺序在黑色背景中央以 10 FPS 循环显示；按 `Esc` 退出。它只用于验证动态桌面捕获，不是产品组件。

## 源码提供

分发 Cimbarpunk 二进制时，应同时提供与该二进制完全对应的本项目源代码、构建脚本、vcpkg baseline、固定 `libcimbar` 子模块内容和 GPL-3.0-only 许可证；Qt LGPL、libcimbar MPL 及其他第三方义务仍分别适用。发布物中的 `share/licenses/cimbarpunk` 包含项目许可证、第三方声明、固定 libcimbar 的完整 MPL-2.0 文本、libcimbar 内嵌组件许可证/NOTICE、实际 Qt 模块的完整许可证与 SPDX 文件，以及每个已解析 vcpkg port 的完整 `copyright`。
