# Cimbarpunk 开发规范

## 适用范围与优先级

- 本文件适用于仓库根目录及全部子目录。
- 用户当前请求、系统级约束和更深层目录中的 `AGENTS.md` 优先于本文件。
- 修改前先阅读 `README.md`、相关设计文档、目标代码和测试，不根据文件名猜测行为。
- 对不确定的事实先检查代码、构建缓存或命令输出；不要把推测写成结论。

## Git 工作方式

- **直接使用 main 分支开发，不提交。**
- 不创建、切换或删除分支，不创建 Git worktree。
- 不执行 `git commit`、`git push`、合并、变基、打标签或创建发布。
- 修改完成后将变更保留在工作区，报告修改文件、验证结果和未解决事项，由用户决定后续 Git 操作。
- 开始和结束工作时检查 `git status --short` 与当前分支；如果不在 `main`，停止并向用户说明。
- 用户已有的已跟踪或未跟踪变更均视为用户数据。不要覆盖、还原、暂存或删除无关改动。
- 禁止使用 `git reset --hard`、`git clean -fd`、`git checkout -- <path>` 等破坏性命令。

## 项目边界

- Cimbarpunk 是 C++20、Qt 6 和 Qt Quick/QML 实现的常驻系统托盘 cimbar 解码客户端。
- 默认不显示主窗口；主要入口是系统托盘菜单。
- 当前里程碑只实现解码。未经用户明确要求，不扩展编码、网络传输或后台服务。
- Windows 10/11 x64 是当前主要验证平台。
- Linux 仅能声称已实际完成的验证范围。当前已验证 Ubuntu 24.04.4 x64 上的 Xfce/X11/Xvfb、XCB 托盘与覆盖层、Qt 屏幕捕获和夹具端到端解码；不得由此推断 Wayland、物理显示器、其他桌面环境或 XRDP 登录会话可用。
- macOS 未构建、未测试时必须明确说明，不得宣称支持。

## 固定工具链与依赖

- 使用 C++20、CMake 3.25+、CTest 和 Ninja。
- Qt 必须是精确版本 6.8.4，并同时提供 Windows Release/Debug exports、Qt Svg、`qsvgicon/qsvgicond` 和 Multimedia FFmpeg 插件。
- Windows Qt Multimedia 使用动态 FFmpeg 7.1.1#6；不得静默切换到 Windows Media Foundation 或其他未验证后端。
- vcpkg checkout 固定为 `9e593bb18ea69cc5095e012465dcd675a822ed0d`。
- `libcimbar` 子模块固定为 `c509e0bb142bfd20e22583fb96f520e8083f3fba`，嵌套 samples 子模块也必须初始化。
- 不浮动依赖版本，不替换固定源码、哈希或 triplet，除非用户明确批准并同步更新验证与许可证材料。
- 当前 Windows 本机 Qt 根目录是 `D:\Softwares\cqp-684\Qt`，vcpkg 根目录是 `D:\Projects\Cimbarpunk\.deps\vcpkg`。这些是本机环境路径，不得硬编码进产品源码。

## 代码与架构规范

- 遵循现有模块边界：`app`、`capture`、`decoder`、`diagnostics`、`output`、`pipeline`、`selection`、`session`、`settings`、`tray`。
- 优先扩展现有小型类和接口；避免把捕获、解码、文件写入和 UI 状态耦合到同一个对象。
- 使用 RAII、明确所有权和确定性清理；避免裸 `new`/`delete`。Qt `QObject` 所有权必须由父对象或智能指针清楚表达。
- 跨线程只传递有明确生命周期的数据；GUI、屏幕和托盘对象只能在其要求的线程使用。
- 捕获线程不得因解码或磁盘写入而无界阻塞；保留“最新帧优先”和有界队列语义。
- 错误必须沿现有结果、状态或信号通道传播。不得吞掉失败或用无条件重试掩盖根因。
- 保持现有命名与格式风格。不要进行与当前任务无关的大规模重排、重命名或格式化。
- 注释解释约束、原因和非显然边界，不复述代码表面行为。

## UI、捕获与隐私

- 应用保持托盘常驻且无主窗口；不要让测试辅助窗口进入发布产品。
- 区域选择必须保持单屏约束，并支持拖拽创建、内部移动、八方向边缘/角调整、Enter 确认和 Esc 取消。
- 开始真实捕获前必须隐藏选择覆盖层，避免捕获自身 UI。
- 屏幕权限、屏幕消失、后端不可用和插件加载失败必须形成可诊断错误，不能静默退化。
- 日志不得记录截图像素、解码内容、文件正文、用户目录中的敏感路径或其他不必要的个人数据。
- 新增诊断信息时保持日志轮转和大小上限，不引入无界日志增长。

## 文件输出规范

- 输出文件不得覆盖已有文件；保持 `name (1).ext`、`name (2).ext` 形式的冲突处理。
- 文件名必须继续移除路径成分、Windows 危险名称和不安全字符。
- 写入必须先落到同目录的应用专用 `.part` 临时文件，校验后再原子改名。
- 只清理由本应用登记且严格匹配命名约束的临时文件；不得宽泛删除用户目录内容。

## 构建与生成文件

- 使用 `CMakePresets.json` 中的 preset；构建目录固定为 `out/build/<preset>`，安装目录固定为 `out/install/<preset>`。
- 切换 Qt 或 vcpkg 路径时必须全新配置，不能手工修改或复用指向另一套 SDK 的 `CMakeCache.txt`。
- 不直接编辑 `out/`、`vcpkg_installed/`、Qt 构建树、自动生成的 MOC/RCC/QML 文件或部署目录中的生成文件。
- Windows 配置必须通过 `scripts/configure-windows.ps1` 导入并验证 VS 2022 x64 环境。
- PowerShell 配置后继续构建时，应在同一 PowerShell 进程中调用脚本，避免丢失 Visual Studio 环境变量。

## 测试与验证

- 修改行为或修复缺陷时，优先先添加能复现要求或故障的测试，再实现最小改动。
- 至少运行与修改模块直接相关的测试；不能以编译成功代替测试通过。
- Windows 完整交付前运行：

  ```powershell
  pwsh -NoProfile -File scripts/verify-windows.ps1 `
    -QtRoot 'D:\Softwares\cqp-684\Qt' `
    -VcpkgRoot 'D:\Projects\Cimbarpunk\.deps\vcpkg'
  ```

- 完整 Windows 验证必须覆盖：全新配置、应用和测试构建、全部 CTest、安装、`windeployqt`、许可证镜像、包内 FFmpeg 插件真实桌面捕获，以及无主窗口的有界托盘启动。
- 自动强制终止托盘进程只证明有界启动检查完成，不等同于人工验证托盘“退出”菜单。
- Linux 完整交付前先运行 `scripts/verify-linux.sh` 完成全新 Release 配置、构建、非 GUI 测试、安装和部署检查，再在具有会话 D-Bus、系统托盘与可访问 `DISPLAY` 的 X11/Xfce 会话中运行 `scripts/verify-linux-gui.sh`。完整 GUI 结论还必须有托盘菜单、区域选择、真实屏幕捕获、夹具端到端解码、输出哈希和托盘退出的实际证据。
- 测试完成后检查并清理本任务产生的残留应用、播放器、测试和构建进程；不要终止无关用户进程。
- Linux 或 macOS 未实际运行的测试必须在结论中明确列为未验证。

## 许可证与发布材料

- 项目自有代码使用 `GPL-3.0-only`。
- 保留 Qt、FFmpeg、vcpkg 依赖、libcimbar 及其内嵌第三方组件的许可证和 SPDX 材料。
- 修改依赖、安装模块或发布内容时，同步更新 `THIRD_PARTY_NOTICES.md`、许可证收集逻辑和验证规则。
- 不删除版权声明、许可证文件、固定源码出处或构建溯源信息。

## 完成标准

- 报告实际修改内容、执行过的命令、测试数量与结果、生成产物路径和未验证平台。
- 只有在刚刚运行的完整命令返回成功并检查输出后，才能声称构建、测试、捕获或部署通过。
- 不以“应该可用”“看起来正常”代替验证证据。
- 遵守“不提交”要求：最终工作区可以有本任务产生的修改，但不得存在由代理创建的提交。
