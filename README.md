# WeatherBar

轻巧的 Windows 传统任务栏天气条。显示当前小时预报、今日最高/最低温和天气图标，不需要常驻独立主窗口。

**简体中文** | [English](README.en.md) | [日本語](README.ja.md)

**当前版本：1.1** · [正式版本](https://github.com/zdxxm10/WeatherBar/releases/tag/v1.1) · [下载 WeatherBar.exe](https://github.com/zdxxm10/WeatherBar/releases/download/v1.1/WeatherBar.exe)

## 下载与安装

1. 从 [Releases](https://github.com/zdxxm10/WeatherBar/releases/latest) 下载 `WeatherBar.exe`，放进一个长期保留且可写的独立文件夹。
2. 双击运行。程序在旁边释放 `WeatherBar.dll` 和 `icons`，写入当前用户的组件注册和开机启动。
3. 如果 Windows 询问是否显示工具栏，选择“是”；也可在传统任务栏右键菜单 → 工具栏 → WeatherBar 中勾选。
4. 右键天气条打开设置、立即刷新或卸载。

发布版本为 x64，面向 Windows 10 的传统任务栏。Windows 11 原生新任务栏不支持 DeskBand；其他传统任务栏实现需自行验证。构建工具仍为 Visual Studio v145，不额外引入 UI 框架。

发布页只需下载 EXE；其中已内置 DLL、图标及许可证。SHA-256 校验值列在发行说明中，无需额外下载校验或许可 TXT 文件。源码许可见本文末尾。

## 设置

- 自定义地址在“自定义地点设置”内编辑，必须点击应用地址才验证及预览，时区统一自动识别。
- 必要天气字段由请求统一补齐；自定义URL已有timezone时保留，缺失时在实际请求中补auto。
- 大小、间距、显示开关和颜色即时预览。点击色块打开调色盘，可点选预置色或调整 RGB 数值/滑块；点击其他位置自动收起。
- 中文、English、日本語，默认跟随系统；中文地区统一简体中文，日语系统使用日语，其余回退英语。语言切换立即替换当前界面。
- **保存**才写入 `WeatherBar.xml`；**取消**或关闭窗口会恢复打开前的设置和预览数据。**恢复默认**只重置待保存的外观，不改地址、时区和语言。
- 地点输入需明确应用；输入不完整或天气请求失败时保留原有效地点和缓存。
- 预报按当地 0/6/12/18 时调度，加默认 15 分钟错峰；隐藏的配置字段 `StaggerSeconds` 仍可设置 0–1800 秒。
- 悬停提示默认开启，仅默认勾选明天天气、后天天气和上次刷新时间。数据状态及紫外线、风向、风速、湿度、体感、降水概率默认关闭；刷新时间和状态都是独立条目。关闭主开关保留子选项，全部关闭时不弹空提示。旧条目选择保留，新字段缺失时才使用默认值。按实际字体宽度一行最多排列两项，缺失数据显示 `--`。这些选项也支持即时预览、保存与取消恢复。
- 附加天气字段与温度共用原有请求及刷新周期；风速使用 m/s。界面中的地址保留简洁形式，程序请求时补齐所需字段。
- “检查更新”只在点击时访问 GitHub，不自动下载或安装。API 失败时尝试读取本仓库官方最新发布页的版本跳转，避免共享出口的匿名 API 限额影响检查；仍失败时显示 HTTP 或 WinHTTP 错误码。

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

## 1.1 未来天气悬停

明天天气和后天天气各有独立开关，整体控制日期、天气文字及最低温 / 最高温；新安装默认均开启。以配置格式3为一次性兼容迁移分界：旧“今日最高/最低”悬停开关开启时迁移为两天均开启，明确关闭则均关闭，缺失条目使用当前默认值；新版独立选择保留，迁移不生成额外备份文件，原子保存失败时保留原配置。任务栏今日高低温不变。

天气文字直接来自 Open-Meteo 每日 weather_code（当天最严重现象），是单一天气摘要，不描述天气变化过程。毛毛雨和阵雨归为雨，冻毛毛雨归为冻雨，米雪和阵雪归为雪，伴冰雹雷暴归为雷暴；各类不区分强度。未知或缺失代码显示“天气未知”。按配置地点的当地日期匹配三天数据，缺失温度各自显示 `--`，不借用其他日期。用户自定义请求返回天数不足时保留缺失状态。

每个日期独立一行，始终按“最低温 / 最高温”排序，例如 `明天：多云 13℃ / 22℃`。按提示字体实际宽度测量，空间不足时日期与内容分两行，两天之间不加空白行。字段沿用现有请求、缓存、失败状态与刷新周期，不增加请求。

### 刷新时间、状态与国家筛选

“上次刷新时间”只记录成功获取并接受有效天气的实际时间点；失败、过期结果、重绘和语言切换都不会更新它。显示时转换成电脑当前系统本地时间，不附时区名称或UTC偏移；无记录显示“尚未刷新”。北京天气在日本时区电脑上显示日本刷新时间。天气当前小时、今天/明天/后天、日出日落和调度仍按天气地点时区计算。

“数据状态”需单独勾选；优先级为配置受保护、尚未选择地点、正在更新、尚未获取天气、数据可能过期、更新失败使用缓存、正常。关闭此项不影响原有操作错误反馈。关闭任意条目不会留下空白分隔行。

国家/地区、地点和搜索按钮同一行，地点输入使用剩余宽度。国家表固定顺序：不限；中国、中国香港、中国澳门、中国台湾；日本、韩国、新加坡、马来西亚、泰国、印度尼西亚、菲律宾、越南、印度；英国、法国、德国、意大利、西班牙、俄罗斯；美国、加拿大、墨西哥、巴西；澳大利亚、新西兰、南非、阿联酋。三语言顺序相同，按稳定代码保存，不按索引；未列国家可选“不限”搜索。HK/MO/TW显示名称本地化，但筛选代码保持HK/MO/TW，不改CN。新搜索地点按国家代码、名称、行政区分别保存，恢复及语言切换重新组合显示；无法确定国家代码的旧自由文本保留原文，不盲目替换。

## 设置窗口布局

设置使用原生卡片分组：基础设置占整行；宽度足够时，显示与外观、间距位于左列，悬停提示、关于与更新位于右列；双列按两行排列：外观/悬停、间距/关于，同一行取左右内容所需高度的较大值并上下对齐；窄窗口按基础、外观、间距、悬停、关于回到单列。列数使用中、英、日共同的宽度阈值，控件宽度依据实际字体与DPI测量，不缩小主文字。国家下拉框按三语言最长选项定宽，语言框与其左边缘对齐，地点输入获得剩余宽度。

默认宽度取完整双列所需的最小值：左列与右列各自内容最小宽度，加卡片内边距、列间距、窗口边距、滚动条和原生边框，同时满足基础搜索行与单行提示的最小宽度。分别按当前字体和DPI测量中、英、日三种语言，取所需宽度最大值，默认宽度和双列切换阈值共用此要求，切换语言不会改变列数；单列最小宽度独立计算，仅工作区不足时默认回退单列。

悬停主开关带完整文字，位于卡片标题右侧。语言与国家下拉框等宽、左边缘对齐，按三语言完整选项实测，加一次原生箭头/滚动条空间和最小边框留白。国家列表点击后优先完整展开；受内容区和显示器工作区限制时显示滚动条，必要时向上展开。窗口移动、缩放或页面滚动时收起，再展开会重新定位，不覆盖固定操作栏。

关于卡片保留版本号、检查更新、发布页面和结果反馈；数据来源使用紧凑文字链接，仍能访问Open-Meteo、GeoNames与许可说明。搜索状态只由一个原生控件显示，边界或文字变化时只完整重绘该控件，清理旧文字并保持居中单行。

标题栏只保留关闭，边框仍可调整。恢复默认、保存、取消固定在底部；只有上方内容区滚动，小窗口、高DPI或展开自定义地点设置时可继续访问全部内容。候选列表与应用行默认常驻，搜索状态切换不移动下方分组。高级输入仍保留草稿、焦点恢复和原有应用/保存/取消规则。滚动只移动内容区，不重新布局固定底栏。主内容、候选列表和URL输入仅在溢出时显示所需的原生滚动条；空间充足时不保留禁用滚动条。必要时保留系统标准箭头，不引入自绘滚动控件。

## 首次运行与地点设置

仅无配置的新用户，当前温度、图标、最高温、最低温、两项间距默认均为现有“大”档。已有配置保存的档位保留；已有文件缺项沿用旧默认值，不套用新用户默认。“恢复默认”将这六项设为大档，其原有外观范围不变，不重置地点、语言或悬停选项。

首次运行**没有预设地点**：显示“尚未选择地点”，国家为“不限”，城市、URL和经纬度输入为空。选择前任务栏显示无数据占位，不请求天气、不运行天气刷新调度；立即刷新不可用。切换语言或恢复外观默认值都不会设置地点。可以先保存语言、颜色等外观设置。

### 搜索和应用

1. 在基础设置选择国家/地区或“不限”。
2. 输入城市或区县，点击搜索（输入时不联网）。
3. 核对候选的国家、行政区和坐标，明确选择候选，点击应用地点。
4. 等待天气/时区校验成功并预览，再点击主窗口保存。取消或关闭恢复原地点；首次预览取消后恢复未选地点并停止调度。

候选列表、提示与应用地点按钮常驻，列表采用紧凑固定高度并内部滚动；未选择有效候选时应用按钮禁用，不自动选第一项。提示为居中的单行文字，不带滚动箭头；搜索、坐标、地址错误在各自区域。搜索不是完整连续地址解析，区县覆盖取决于服务；无结果请换城市名或坐标。惠城区仅用于下方文档示例，不是软件占位符或默认值。

### WGS84（GPS）坐标与自定义地址

展开“自定义地点设置”，选择手动经纬度或自定义地址；草稿切换不清空，明确应用才预览，两个按钮居中。WGS84纬度-90～90、经度-180～180，使用十进制度，负数和0均合法；缺失不能当作0。度分秒需先正确换算：绝对值为度+分/60+秒/3600，南纬/西经取负号。

可操作的WGS84来源：打开[Open-Meteo地点搜索文档](https://open-meteo.com/en/docs/geocoding-api)，输入城市名、国家和语言，查看生成的API URL及JSON；核对name、country、admin1…admin4，复制latitude和longitude。也可打开[Tokyo查询示例](https://geocoding-api.open-meteo.com/v1/search?name=Tokyo&countryCode=JP&count=10&language=zh&format=json)，核对候选后复制数值。官方明确这些字段是WGS84；[GeoNames](https://www.geonames.org/about.html)同样说明其坐标系。或使用设备说明明确标注WGS84、可导出十进制度的GPS数据，按该设备说明操作。

WGS84是全球参考坐标系；[高德说明](https://developer.amap.com/faq/advisory/others/39838)其服务使用GCJ-02；[百度说明](https://lbsyun.baidu.com/docs/jsapi?title=jsapi4/guide/concept/coord)BD-09在GCJ-02基础上再处理。它们不能未经确认混用。本软件不转换坐标，不内置转换库/API、地图拾取器或位置反查。也不能笼统认为所有Google或Apple地图页面复制值都是可直接使用的WGS84，要核对具体工具/接口。

### 从官方天气页面获得URL

打开[Open-Meteo天气文档](https://open-meteo.com/en/docs)，在Location and Time填写已核实的纬度和经度，Timezone选择auto；Hourly Weather Variables选下方hourly各字段，Daily Weather Variables选daily字段，在Settings选择摄氏度、m/s和Unix时间。页面API Response中的API URL可复制到自定义地址，再点击应用地址、保存。页面语言/布局可能变化，可对照参数名核对最终URL。

当前完整推荐参数（包含未来两天、图标及全部悬停字段）：

```text
hourly=temperature_2m,weather_code,uv_index,wind_direction_10m,wind_speed_10m,relative_humidity_2m,apparent_temperature,precipitation_probability&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset&temperature_unit=celsius&wind_speed_unit=ms&timezone=auto&timeformat=unixtime
```

[可直接打开的东京示例URL](https://api.open-meteo.com/v1/forecast?latitude=35.6895&longitude=139.69171&hourly=temperature_2m,weather_code,uv_index,wind_direction_10m,wind_speed_10m,relative_humidity_2m,apparent_temperature,precipitation_probability&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset&temperature_unit=celsius&wind_speed_unit=ms&timezone=auto&timeformat=unixtime)仅用于说明，不会自动写入配置。latitude是纬度，longitude是经度，不能颠倒。

三种来源统一校验响应timezone和utc_offset_seconds，再进行当地日期归档。自定义URL已有timezone时保留，缺失/空值在内部请求补auto，重复取最后一个非空值；不改原URL及无关参数。首次未取得时区明确提示，失败保留有效缓存，非整数小时支持。Open-Meteo单次响应内偏移可能固定，跨夏令时边界仍有限制，并非完整DST支持。

### 配置保存、更新与重新安装

配置为程序/DLL所在目录的WeatherBar.xml，优先当前名，缺失时仅查同目录旧名文件。保存采用同目录原子写入。配置格式5支持空地点，旧有效URL（包括旧默认北京URL）、地点和外观继续读取，不把它们清空。损坏/过高格式只读保护并提示，不当作正常首次运行覆盖，也不自动生成迁移备份。

默认卸载保留XML和图标，不自动删除目录；保留配置后把新EXE放回同目录安装即可恢复。移动整个目录时同时带上XML和icons；仅把EXE放到新目录不会扫描磁盘寻找旧配置。主动删除XML/整个目录、选择彻底卸载、换Windows用户或文件不可读时，不能保证自动恢复。操作前可自行复制XML/icons留存。软件更新或资源释放不会用空配置覆盖旧XML。恢复默认仍仅重置外观。

所有下拉框保留滚轮保护（展开仅浏览列表、收起滚主页面），未来两天悬停和自动时区不变。关于与更新→数据来源提供Open-Meteo、GeoNames和[CC BY 4.0](https://open-meteo.com/en/licence)来源许可链接。应用版本为1.1；发布附件和校验值见[正式发布页](https://github.com/zdxxm10/WeatherBar/releases/tag/v1.1)。

## 可复制的AI工具提示词

只替换地点名称即可。AI只是辅助：有的工具不能联网，回答也可能不准确，必须核对来源、同名地点和URL。

### A：生成Open-Meteo URL

```text
请联网查询【中国广东省惠州市惠城区】的WGS84十进制度经纬度，核对同名地点和坐标来源，并为WeatherBar生成Open-Meteo天气预报URL。
使用 https://api.open-meteo.com/v1/forecast ，根据核实的地点填写latitude和longitude，并完整加入以下参数：
hourly=temperature_2m,weather_code,uv_index,wind_direction_10m,wind_speed_10m,relative_humidity_2m,apparent_temperature,precipitation_probability&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset&temperature_unit=celsius&wind_speed_unit=ms&timezone=auto&timeformat=unixtime
请输出可直接复制的完整URL、地点名称、纬度、经度及来源链接。不要编造坐标；无法确认时说明不确定，不要把GCJ-02或BD-09当作WGS84。
```

### B：获取WGS84坐标

```text
请联网查询【中国广东省惠州市惠城区】的代表地点WGS84经纬度，使用十进制度，分别标明纬度和经度，并提供可核查的来源链接。说明该坐标代表城市或区县中的哪个位置，不要声称代表整个行政区每一点。不要提供GCJ-02或BD-09；无法确认坐标系时明确说明，不要猜测。
```
