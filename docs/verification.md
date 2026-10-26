# 验证记录

## 1.0 首个正式版本：悬停提示与更新检查

- 工作区开始时干净；修改前 x64 的 19 组回归全部通过。
- 新增 `Tooltip/Enabled`、`Tooltip/Fields` 可选字段，继续使用配置格式版本 2。旧文件缺项默认开启悬停，仅展示上次刷新和状态；保存仍保留其他原字段，取消/关闭回滚内存预览。
- 7 个可选项：今日最高/最低、当前小时紫外线指数、风向、风速、相对湿度、体感温度、降水概率。风速 m/s，缺失或无效的可选值显示 `--`，不丢弃有效温度。
- `Weather::SetURL` 在内部请求中补齐字段，设置页及 XML 地址保持简短；不改变刷新周期，不增加独立请求。
- 布局：使用原生提示字体测量，最多两项一行，以完整刷新时间行为宽度基准；长状态由原生提示在该宽度内折行。未刷新时按完整时间格式预留宽度。
- 已实测 Open-Meteo：默认公开示例地点的六项新字段各返回 168 个小时数据；单位与程序请求一致。
- 已复现旧更新失败：匿名 GitHub API 返回 HTTP 403，`X-RateLimit-Remaining: 0`。同一 WinHTTP 路径加入固定官方最新发布页 HEAD 跳转备用后，API 403 情况下成功得到 `v0.2.0`，无需账户或 Token。仅接受本仓库 HTTPS 前缀及正式版本标签，不跟随任意响应地址，不下载程序。
- 三语言回归覆盖无数据、正常、失败缓存、过期、全部可选值、关闭提示，以及每种语言 128 种勾选组合的实际字体宽度；配置读写、保存/取消、旧配置缺项、外观资源复用继续通过。
- 最终 x64 与 Win32：各 19 组核心回归全部通过。x64 Release 单 EXE 构建通过，MSVC 静态分析无新增告警，原有 5 条保留。
- 安装器额外验证：现存旧测试二进制先后在 x64、Win32 的退出后清理阶段返回 4294967295；重建两种架构后分别复测通过，包括图标保留、DLL 占用、目录联接保护、退出后删除 EXE 与无关文件保护。仅凭重建后通过不能确认先前失败根因，不掩盖该记录，也未修改安装器清理逻辑。
- 已查看中文原生设置预览；原生布局测试检查三语言控件不重叠。桌面输入保护阻止继续点击更新，因此未声称完成设置按钮端到端验证。
- 未安装到任务栏、未修改系统注册/启动项、未重启 Explorer。真实任务栏开关、跨屏 DPI、小屏滚动、PAC/直连等不同网络环境仍需人工验证。
- 首个正式版本使用 `v1.0` 标签，程序显示 `1.0`。更新比较兼容两段和三段版本号（`1.0` 与 `1.0.0` 等价），不接受预发布或格式错误的标签。
- 首发附件仅 `WeatherBar.exe`，SHA-256 放入发行说明，项目及图标许可证保留于源码和 EXE 内。三语言 README 同步说明下载、悬停、手动更新和兼容限制。

本次修改文件：`src/AppText.h`、`src/ImportConfig.h/.cpp`、`src/Weather.h/.cpp`、`src/DeskBand.cpp`、`src/Settings.cpp`、`src/Update.h/.cpp`；`tests/CoreTests.cpp`、`tests/DisplayTests.cpp`、`tests/SettingsTests.cpp`；三语言 README 和本文，共 16 个文件。

可复现命令（在 Visual Studio Developer PowerShell 中）：

```powershell
MSBuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /v:minimal
MSBuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /p:TargetName=RegressionTestsV1 /v:minimal
.\tests\x64\Release\RegressionTestsV1.exe
.\tests\x64\Release\RegressionTestsV1.exe --check-update
MSBuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=Win32 /p:TargetName=RegressionTestsV1 /v:minimal
.\tests\Win32\Release\RegressionTestsV1.exe
MSBuild tests\LauncherTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe
MSBuild tests\LauncherTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=Win32 /v:minimal
.\tests\Win32\LauncherRelease\LauncherTests.exe
```

环境：本机 Windows 10.0.26300 x64；Win32 为该系统上的 32 位程序验证，不等同于独立 32 位 Windows 或 Windows 10 传统任务栏实装验证。

人工验证：备份 XML/icons 后按 README 更新；分别切换中英日，勾选全部悬停项并观察正常/失败状态折行；关闭悬停后确认不弹出；修改后取消或关闭设置确认恢复；保存后重开确认持久化；点击检查更新验证页面结果。

以下为上一版的历史验证记录。

# 早期开发构建验证记录（非当前发行版）

## 变更范围

- `src/Settings.cpp`：紧凑布局、圆角分区、三行 URL 按字符折行、颜色按钮与非模态 RGB 调色盘、即时语言切换、保存/取消。
- `src/DeskBand.h/.cpp`：区分内存预览与配置保存，取消恢复配置和原天气缓存，拒绝过期预览请求。
- `src/ImportConfig.cpp`、`LegacyNames.h`：改名后兼容旧配置，保留原件及迁移备份，新配置使用 WeatherBar 名称。
- `src/AppText.h`、`Update.cpp`、`Weather.cpp`、注册与工程文件：产品名、版本、用户代理、安装项及 GitHub 地址统一 WeatherBar。
- `launcher/`：旧启动项兼容、旧 DLL 占用保护、卸载清理及退出后进程身份检查；单 EXE 内包含项目与图标许可证。
- `tests/`：扩展三语言紧凑布局、颜色、预览、失败恢复、改名迁移及安装器文件测试。

保留原 COM CLSID 与配置字段含义、默认值、调度规则和图标映射。不升级依赖、提高工具集要求或增加 UI 框架。仅 `LegacyNames.h` 与上游来源说明保留旧产品名，以免破坏旧数据读取和署名链接。

## 已执行

- 修改前：原 x64 的 18 组核心测试通过；安装器测试在正常权限下通过，受限环境创建目录联接失败单独记录。
- 修改后：Release x64 完整重建通过，最终发布包为 551936 字节（约 539 KiB），内含 DLL、SVG 与许可证。
- 最终 MSVC 静态分析：无新增警告；原有 5 条仍保留：两条 GetTickCount、两条 COM SAL 批注、一条网络函数栈空间提示。
- x64 / Win32 各 19 组核心测试通过：配置故障与原件保护、旧名迁移、语言回退、更新结果分类、天气解析、时间调度、异步生命周期、资源复用、即时预览、保存/取消/关闭、保存失败恢复、地点缓存恢复、过期请求拒绝、打包资源和许可证。
- 三种语言的原生窗口检查：所有控件在最小尺寸内，标签文字不截断、控件不重叠；下拉框收窄且放大窗口后仍保持列表高度；默认 API 地址三行显示，非标点提前折行。
- 调色盘：原生窗口测试验证任意 RGB 滑块修改立即进入内存预览，配置文件不提前变化，失去激活自动销毁；实际查看了中文调色盘及中英紧凑设置页面。
- x64 / Win32 安装器测试：同名图标保留、缺失图标补齐、占用 DLL 拒绝替换、各代备份及旧配置清理、icons 删除、根和嵌套目录联接不触及目标、中文与特殊字符路径、等测试进程退出后删除其 EXE、仅删除空目录，其他文件保留。x64 还验证真实打包 DLL 在加载期间不可替换。
- 源码检查：旧品牌位置、凭据样式与行尾空格检查通过；构建产物、个人配置与旧工作目录不纳入发布仓库。

曾发现并修复标题高度下的中文裁切、收窄后的版本/状态区域重叠。退出后清理测试在本机出现过 20 秒等待超时及一次非零退出；增加进程创建时间检查，避免等待已复用的 PID，测试最大等待改为 60 秒，最终两种架构独立重跑通过。不把先前超时直接解释为特定系统或代码原因。

为避免覆盖正在查看的测试 EXE，部分 x64 检查以 `TargetName=RegressionTestsCompact` 构建；源码、测试集合和目录一致。原有 Debug 导出配置问题未扩大修复，正式发布使用 Release x64。

## 尚未验证

- 本轮没有真实安装、卸载、修改开机启动或重启资源管理器；任务栏内的最终显示、首次启用确认及登录恢复须在传统任务栏实机验收。
- 未完成独立 Windows 10、32 位任务栏、其他传统任务栏实现、跨屏动态 DPI、所有系统缩放比例验证。Win32 逻辑测试通过不代表 32 位安装已支持。
- 实际网络环境、代理、证书、API 异常和夏令时仍需运行验收；固定响应测试不等于真实服务联调。
- Windows 11 原生新任务栏不支持本插件。组织策略禁止 Windows PowerShell 时，彻底卸载后的 EXE 清理可能需要手动完成。
- 未提供代码签名或自动下载安装更新；GitHub Releases 提供 EXE 与 SHA-256 校验文件。

建议实机步骤：备份旧 XML 和 icons → 旧 EXE 默认卸载 → 新 EXE 放回同目录 → 安装 → 修改大小、颜色、语言并观察任务栏即时变化 → 取消恢复 → 再保存并重开确认 → 手动检查新仓库发布页 → 默认卸载重装确认数据保留 → 独立验证彻底卸载，确认其他资料保留。
