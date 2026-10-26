# WeatherBar

轻巧的 Windows 传统任务栏天气条。显示当前小时预报、今日最高/最低温和天气图标，不需要常驻独立主窗口。

**简体中文** | [English](README.en.md) | [日本語](README.ja.md)

## 下载与安装

1. 从 [Releases](https://github.com/zdxxm10/WeatherBar/releases/latest) 下载 `WeatherBar.exe`，放进一个长期保留且可写的独立文件夹。
2. 双击运行。程序在旁边释放 `WeatherBar.dll` 和 `icons`，写入当前用户的组件注册和开机启动。
3. 如果 Windows 询问是否显示工具栏，选择“是”；也可在传统任务栏右键菜单 → 工具栏 → WeatherBar 中勾选。
4. 右键天气条打开设置、立即刷新或卸载。

发布版本为 x64，面向 Windows 10 的传统任务栏。Windows 11 原生新任务栏不支持 DeskBand；其他传统任务栏实现需自行验证。构建工具仍为 Visual Studio v145，不额外引入 UI 框架。

## 设置

- Open-Meteo API 地址：填写 `/v1/forecast` 预报接口的完整 URL。三行显示，按可用宽度自动折行；更长的地址可滚动查看。默认地点为北京市东城区，UTC 偏移为 8。
- 必要数据包含 `hourly=temperature_2m,weather_code`、`daily=temperature_2m_max,temperature_2m_min,sunrise,sunset` 及 `timeformat=unixtime`。请让地址中的 `timezone` 与 UTC 时区偏移对应；UTC 偏移沿用整数小时，夏令时需手动调整。
- 大小、间距、显示开关和颜色即时预览。点击色块打开调色盘，可点选预置色或调整 RGB 数值/滑块；点击其他位置自动收起。
- 中文、English、日本語，默认跟随系统；中文地区统一简体中文，日语系统使用日语，其余回退英语。语言切换立即替换当前界面。
- **保存**才写入 `WeatherBar.xml`；**取消**或关闭窗口会恢复打开前的设置和预览数据。**恢复默认**只重置待保存的外观，不改地址、时区和语言。
- 地址和时区输入稍作停顿后预览；输入不完整时保留上一个有效地点，不打断外观预览。天气请求失败时保留已有缓存并提示。
- 预报按当地 0/6/12/18 时调度，加默认 15 分钟错峰；隐藏的配置字段 `StaggerSeconds` 仍可设置 0–1800 秒。
- 悬停查看天气、最近成功获取时间与状态。“检查更新”只在点击时访问 GitHub，不自动下载或安装。

## 更新与卸载

手动更新：默认卸载（不勾选彻底卸载）→ 等资源管理器重启 → 替换同目录 EXE → 双击重装。同名自定义 SVG 保留，缺失图标补齐；配置缺项使用新版默认值，损坏或更高格式版本的文件不会被覆盖。

默认卸载保留 XML、迁移备份、日志、自定义图标和 EXE，移除 DLL、注册及启动项。勾选**彻底卸载**会删除本软件配置、备份、日志和 icons；关闭完成窗口后，Windows 自带 PowerShell 删除 EXE，并删除空目录。其他资料不会删除，目录联接不会被递归跟随。组织策略禁止 PowerShell 时，可能需要手动删除 EXE。

卸载会重启资源管理器并关闭文件夹窗口，操作前请保存工作。首次从旧版升级前，务必备份 XML 和 icons，再用旧 EXE 卸载；把新 EXE 放回原目录，旧配置会迁移到新名称并保留原件。兼容代码集中在 `src/LegacyNames.h`；注册 CLSID 保持不变，避免重复组件。

## 构建与测试

在带 C++ 桌面开发组件、Windows SDK 和 v145 工具集的 Visual Studio Developer PowerShell 中运行：

```powershell
MSBuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
MSBuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\Release\RegressionTests.exe
MSBuild tests\LauncherTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe
```

发布文件为 `x64/Release/WeatherBar.exe`，内含 DLL 和 SVG。插件使用 C++20，启动器使用 C++17。无需安装旧依赖清单中的第三方包。测试中的 `Platform` 可改为 Win32，验证 32 位逻辑；不等同于 32 位任务栏安装验证。

测试只操作自己的输出目录，不安装插件、不改启动项、不重启资源管理器。UI 预览：`RegressionTests.exe --preview-settings zh-CN`（或 `en` / `ja`）。真实任务栏、开机启动、网络与跨屏 DPI 仍需运行验证，见 [验证记录](docs/verification.md)。

## 项目结构

- `src/`：任务栏组件、天气请求、配置、设置界面和手动更新。
- `launcher/`：单 EXE 打包、安装与卸载。
- `tests/`：配置故障、旧数据迁移、资源与界面行为测试。
- `docs/`：[架构图](docs/architecture.md)与验证记录。

## 来源与许可

基于 [Nusudo-junior/WeatherDeskBand](https://github.com/Nusudo-junior/WeatherDeskBand)，保留原作者及 [MIT 许可证](LICENSE)。上游仓库名属于来源链接，不进行错误替换。

天气数据：[Open-Meteo](https://open-meteo.com/)。图标：Bas Milius 的 [Meteocons](https://github.com/basmilius/meteocons)，许可证见 [METEOCONS-LICENSE.txt](src/icons/METEOCONS-LICENSE.txt)。问题反馈：[Issues](https://github.com/zdxxm10/WeatherBar/issues)。
