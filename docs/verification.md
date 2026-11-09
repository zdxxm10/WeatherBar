# 验证记录

## 1.1 重新发布验证

按用户授权，将累计完成的地点搜索、WGS84坐标、自动时区、首次无地点、悬停独立选项、原生响应式设置及滚动/国家列表修复，作为1.1重新发布。应用版本仍为1.1，配置格式独立为5。发布说明参考1.0，只保留中文；不删除英文/日文界面或文档。之前各节的“未上传/未发布”是当时的历史记录。

远程默认分支codex/weatherbar与旧v1.1在准备时均为4981468d90645b47700826576d0d1341001dfb3c。仅替换旧v1.1发布、附件及标签，保留v1.0，不强推分支。先提交并从该提交重新构建、检查，再推送和替换发布；最终源码提交、附件大小、SHA-256及实际验证结果记录在GitHub发布说明。旧版1.1版本号相同，更新检查不会提示重新发布，需手动重新下载。

发布前已完成Release x64、Win32各25组回归、x64主程序构建、隔离资源提取配置保留检查与三语言原生布局截图。重新构建命令与本轮最终结果将随发布说明记录，不声称执行了安装、真实任务栏、屏幕阅读器或跨屏DPI验收。旧发布附件为576512字节、Release x64；最终产物与之对比时说明旧提交未重建，增长来自新增地点/JSON解析、时区和设置代码，无新运行依赖、地图组件或城市数据库。测试程序与截图不嵌入发布附件。

配置仍在DLL/EXE同目录，沿用原子保存及损坏/高版本写入保护，不自动生成格式升级bak。安装/更新保留已有配置与同名图标的隔离提取检查不等同于安装验收。保留此前记录的原子文件替换1175偶发失败历史，不宣称已消除该系统文件操作问题。

## 历史：悬停标题开关、两行卡片与国家列表（发布前本地实现）

本轮仅修改src/Settings.cpp、tests/SettingsTests.cpp、三语言README、docs/architecture.md和本验证记录，保留工作区其余已有修改。仍为WeatherBar/codex/weatherbar，远程zdxxm10/WeatherBar；应用1.1、配置格式5不变。只做本地实现和验证，没有提交/推送、发布、上传、安装、替换运行组件、重启Explorer或改真实配置。

### 实现

- 主开关保留有完整文字的原生复选框及Tab/键盘语义，位于悬停标题同行最右侧。标题用标题字体，开关用正文实际字体测量，均纳入三语言共同宽度要求。删除原独立开关行。
- 双列基础整行，下方按外观/悬停、间距/关于两个卡片行排列，行高取左右实际所需高度的较大值，上下边缘一致；单列保持自然高度。固定底栏、按需滚动及ScrollWindowEx路径保持。
- 国家/语言框共用三语言完整国家表和语言选项的最大实测文字宽度，加一次DPI原生竖条/箭头宽度、左右边框和4 DIP最小文字留白。国家框本机从160减至155像素，语言框同步155；语言不改变宽度，地点输入获得节省空间。
- 国家列表目标高度为全部实际选项行高及原生边框，以内容区与显示器工作区交集约束；优先向下完整展开，向上能完整容纳时向上，否则选择空间较大方向并按完整行限制。原生ComboLBox回调只修正最终窗口矩形，不再次SetWindowPos，不自绘/改选。测试发现仅裁剪外框时原生内部仍按完整列表/最少行数处理，补条和范围不能恢复导航，因此删除这些过时分支。最终使用CBS_NOINTEGRALHEIGHT并在展开前设置整行像素高度，让系统自行正确维护列表视口和滚动范围。实际受限列表28项、page21，滚到底/滑块到尾得到top7，CB_SETTOPINDEX(4)返回top4；原先失败分支均为top0。必要条保留系统样式/箭头。
- 展开前复用EnsureSettingsControlVisible将国家框滚入可视区（含已有焦点但后来滚出视口的情况）。窗口移动、布局变化或内容滚动时收起已展开国家列表；重新展开重新定位，避免旧位置覆盖底栏。不是将展开列表持续绑定窗口拖动；这是有意采用的轻量行为。关闭状态不自动展开，无网络请求。

### 实际检查

- git diff --check通过。Release x64主程序构建通过，LauncherTests --retention-only隔离提取保留配置/图标通过，不安装。最终Release x64、Win32各25组回归全部通过（Failures: 0），包含最后补充的页面先滚到底再展开检查。
- 已导出并目视检查三语言默认双列、中文完整展开、中文/英文受限展开：默认835×887，窄窗645×639。两行卡片对齐，标题与开关无截断，国家/语言等宽左齐。完整展开28项、无无用条；受限列表未越过底栏，保留原生纵向条。实际结果仅代表本机字体/DPI，不是硬编码默认尺寸。
- 24张实际原生窗口PNG位于artifacts/settings-hover-layout，包括zh/en/ja的default、countries、countries-limited、narrow、hint-resize、wide、advanced、url。弹出列表与主窗口分别通过PrintWindow输出并按其实际屏幕位置合成，不使用设计图。静态导出不证明连续滚动体验。
- 六次WM_MOUSEWHEEL逐次泵消息，三语言均为content erase=8（处理、不默认擦白）、paint=6、main paint=0、child moves=0，与上一轮滚动修复一致。未改变滚动路径的刷新方式。
- 回归补充页面先滚到底后重新展开的实际弹出矩形/内容区边界、完整/受限条显隐、原生条范围与滚到底、滚轮浏览不改选择、向上展开、移动后收起、三语言正文完整和关闭状态文本宽度。既有96/120/144/192合成DPI、单双列临界、预览保存取消关闭、异步保护及下拉框键盘/滚轮检查继续运行。
- 本轮未做实际跨显示器DPI、小物理工作区、屏幕阅读器实测、安装后任务栏验收或长时间人工滚动/拖动验收；合成DPI与原生消息检查不能替代这些实际使用测试。没有修改系统设置或真实配置。

### 复现与产物

在对应Visual Studio开发者命令环境中执行：

```powershell
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 "/p:OutDir=C:/Users/zdxxm/Documents/ChatGPT/windows任务栏天气插件/WeatherBar/tests/x64/HoverVerify/" /v:minimal
.\tests\x64\HoverVerify\RegressionTests.exe
.\tests\x64\HoverVerify\RegressionTests.exe --capture-settings artifacts\settings-hover-layout
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=Win32 /v:minimal
.\tests\Win32\Release\RegressionTests.exe
msbuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe --retention-only
.\tests\x64\HoverVerify\RegressionTests.exe --preview-settings zh-CN
```

最终EXE在x64/Release/WeatherBar.exe；同Release x64配置下，本轮前WeatherBar-before-hover-layout.exe为679936字节，当前682496字节，增加2560字节（0.38%），无新依赖。SHA256：59EF3D36C92E792C515EA799E84637F86538C1CC9DFA198464EC1E823CEA314A。

x64/Release/PreviewSettings.cmd指向HoverVerify的隔离预览，不安装。关闭旧预览后启动，切换三语言检查开关/等宽框；展开国家表，缩窄及降低窗口高度后重开检查受限列表，滚轮浏览并键盘/点击选择；移动、缩放、滚动确认旧弹层收起；展开自定义输入后检查固定底栏。最后取消/关闭；用--preview-settings en或ja也可直接打开对应语言。等待用户实际测试，不发布。

## 历史：共同三语言宽度、滚动绘制与按需滚动条

本轮仅修改src/Settings.cpp、tests/SettingsTests.cpp、三语言README、本架构与验证文档；保留工作区其余已有改动。仓库WeatherBar，分支codex/weatherbar，远程zdxxm10/WeatherBar。应用1.1、配置格式5、天气与默认值不变。没有提交、推送、发布、上传、安装或替换运行组件，没有改真实配置、系统时区或系统DPI。

### 原因与修改

旧滚轮路径每次调用LayoutSettings：整页重新测量、逐控件批量定位、内容与主窗口再次擦除/失效，WM_ERASEBKGND的白色背景与灰色卡片后续绘制交替。实际构建中，六次WM_MOUSEWHEEL并逐次泵消息记录：三语言均为内容erase=105、paint=6，主窗paint=6，子控件WM_WINDOWPOSCHANGING=480。修改后同样过程为erase=8（已处理，不执行默认擦白）、paint=6、主窗paint=0、显式子控件移动消息=0。ScrollWindowEx移动已有内容与子窗口，只重画暴露区域；布局/折叠才重新测量，背景与卡片在一次WM_PAINT完成。没有添加缓冲样式或反复重建控件。

MeasureSettingsLanguage使用实际字体/DPI分别测量中、英、日。共同doubleColumn取三者最大值（各自覆盖基础搜索/提示行、左右卡片及底栏），再加一条原生滚动条与窗口边框。默认宽度及单双列阈值共同使用此值；隐藏滚动条后仍完整利用可用客户区，但阈值预算其可能出现的宽度，避免显隐导致列数振荡。单列最小值分别计算后也取三语言最大值；工作区不足才回退。

主区域不使用SIF_DISABLENOSCROLL，内容不足时不留灰色占位。显隐后最多两次有界布局，夹取滚动位置。候选列表清空时同步清零水平extent。URL按既有逐字符断行方式、实际字体及无条宽度测量，避免通过临时修改EDIT格式矩形探测而引起临界显隐振荡。保留必要方向的标准滚动条及系统箭头，没有自绘滚动系统；状态/提示仍是普通静态显示，没有滚动条。

### 实际验证与限制

- Release x64回归25组通过，x64解决方案构建通过。Win32首轮布局等检查通过，但两项保存用例出现既有文件替换错误1175；独立重跑25组全部通过。没有扩大修改保存实现。
- 布局断言覆盖三语言默认宽度相同、语言切换仍双列、临界前后像素、单列、96/120/144/192合成DPI、滚动范围与底栏隔离、内容短时无条、候选纵横条清理、URL短/长/临界重复测量无振荡及隐藏时无溢出。既有保存/取消/关闭恢复、预览、键盘及下拉框滚轮保护一并运行。
- 当前产物的LauncherTests.exe --retention-only通过，隔离重复提取保留XML和自定义图标；不安装、不注册、不重启Explorer。
- 实际预览中展开高级设置并执行鼠标滚轮，操作后未见文字或卡片残影，底部栏不移动。尝试拖动滚动条两次均被工具的用户输入保护中断，之后停止操作，未算作通过。不能用以下静态截图证明瞬时闪烁完全消失；连续长时间滚动/拖动体验仍请用户试用。
- 实际跨屏DPI、小物理工作区、独立32位Windows及安装后任务栏未验证。合成DPI测试不等于真实跨屏。没有增加网络请求验证。
- 18张PNG来自当前构建托管的实际生产原生设置窗口（PrintWindow/WM_PRINTCLIENT导出），不是设计图或已安装任务栏。artifacts/settings-scroll包含zh/en/ja-default、narrow、hint-resize、wide、advanced、url。三个默认均835×909，窄窗均650×639，宽/展开915×939；只是本机字体/DPI的测量结果。已目视检查三个默认、中文窄窗及URL展开，无文字截断或多余禁用滚动条。

### 构建、产物与复现

```powershell
# 在对应VS开发者命令环境执行；独立输出避免覆盖已运行的旧预览
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 "/p:OutDir=C:/Users/zdxxm/Documents/ChatGPT/windows任务栏天气插件/WeatherBar/tests/x64/ScrollVerify/" /v:minimal
.\tests\x64\ScrollVerify\RegressionTests.exe
msbuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=Win32 /v:minimal
.\tests\Win32\Release\RegressionTests.exe
.\tests\x64\ScrollVerify\RegressionTests.exe --capture-settings artifacts\settings-scroll
.\tests\x64\LauncherRelease\LauncherTests.exe --retention-only
.\tests\x64\ScrollVerify\RegressionTests.exe --preview-settings zh-CN
```

x64/Release/PreviewSettings.cmd已指向ScrollVerify的最终测试程序；预览模式不会安装组件，只使用测试程序目录。用en或ja参数可直接预览其他语言。测试取消/关闭后恢复，勿将测试目录配置当成真实配置。

同Release x64配置：修改前WeatherBar-before-scroll.exe为672768字节，当前WeatherBar.exe为679936字节，增加7168字节（1.07%），来自新增三语言测量及按需滚动处理代码，无新增依赖。当前EXE SHA256：417685C082C5C7D987B97E55AA4BB4F289D8B246BEF11EFB14315F653F9FB51C。

手动测试：启动预览，反复切换三语言；从默认双列缩窄到单列再放大；展开/收起自定义地点，滚轮并拖动内容条检查卡片/文字和固定底栏；在URL输入长文本再清空检查条显隐；确认候选列表滚轮和下拉框点击/键盘正常且滚轮不改选择。最后取消/关闭。正式安装和真实任务栏验收留给用户决定。

## 历史：最小完整双列、提示重绘与最大档位默认

以下为上一轮记录；当前宽度、滚动与产物以本页首节为准。仓库WeatherBar、codex/weatherbar、远程zdxxm10/WeatherBar；保留此前工作区改动。仍为应用1.1/配置格式5。只本地修改、构建、测试；未提交/推送、创建远程PR/标签/Release、上传、安装/卸载、替换组件、重启Explorer、改系统时区/DPI或删除真实配置。

### 提示异常定位与最小修复

编辑生产代码前，在已编译原生设置窗口测量连续缩窄的实际失效区域。中文提示由709像素缩至343像素，旧文本范围256–452，新窗口内仍存在256–343的旧字区域，但待重绘区只有69–229。英文旧文本274–435、缩后宽411、待重绘102–262；日文旧文本270–438、缩后宽338、待重绘64–224。这证明缩窄路径只使局部失效，不能覆盖居中位置变化前的旧文本。原两个提示控件互斥显示，未发现同时可见，不能把问题单纯归因于重复创建。父内容区WS_CLIPCHILDREN又使父失效无法清除子控件像素。

现仅创建IDC_LOCATION_STATUS一个SS_OWNERDRAW静态控件，边界/文字/语言变化完整失效该控件自身，WM_DRAWITEM填满自身背景后DT_SINGLELINE|DT_CENTER|DT_VCENTER绘制。没有频繁重建控件或新增全窗口强刷。实际修复后待重绘区中文0–343、英文0–411、日文0–338；提示文字也参与单列最小宽度约束。基线诊断保存于artifacts/settings-refine-before/repaint-diagnostics.txt。

注意：完整PrintWindow导出会重新绘制窗口，不保留旧缓存像素残影。因此没有把基线全量截图当成“拍到了重复文字”；实际复现依据是缩放路径的原生失效区域与旧文字范围，最终截图展示修复后的对应位置。

### 布局与默认值

- 基础占整行，左列外观→间距，右列悬停→关于；自然高度不强制对齐。单列为基础、外观、间距、悬停、关于。固定底栏不变。
- single分别覆盖搜索/状态行、各卡片和底栏。doubleColumn=max(single, display+max(hover,about)+48 DIP)，卡片宽已含内边距，再加DPI滚动条与原生窗口边框。默认严格采用该最小双列宽，只有工作区不足才夹取并回退，单列跟踪最小宽度独立。字体不缩小，不使用截图固定尺寸。
- 关于保留版本、检查更新、发布页面和反馈；不足则分行。数据来源改为SysLink紧凑文字链接，NM_CLICK/NM_RETURN调用原TaskDialog。通用Enter处理排除SysLink，不能意外保存。普通/短按钮按三语言最长实际文字和统一20 DIP水平留白测量，删除原数据来源宽按钮专属命令与分组。
- 仅无文件的新配置六项全部Large：当前温度、图标、高/低温、图标间距、温度/高低温间距。只使用原最大档位，不添加档位。已有文件先沿用旧缺项默认，再解析显式值及旧像素配置；保存值不重置。恢复默认六项Large，原颜色/显示开关作用范围保留，不重置地点、语言、悬停。

### 实际验证

- git diff --check通过；Release x64、Win32各25组回归通过；x64发布解决方案构建通过。覆盖默认双列精确宽度、临界前后1像素、单列、96/120/144/192合成DPI、卡片位置、固定底栏、提示单一HWND及连续12次缩放/状态切换/滚动后的重绘范围、链接键盘激活、六项新默认/旧值保留/旧文件缺项/恢复默认范围，以及全部现有数据/异步/保存/取消回归。
- 临界测试修正了合成DPI夹具的边框/滚动条假设：合成消息只改变字体和布局，操作系统边框并未真的换DPI，故按窗口真实像素计算测试目标。可见提示检查完整失效；滚出视口的提示不强求Windows绘制不可见区域，滚回再验证。
- 中间两次旧高频原子配置往返用例出现1175（ERROR_UNABLE_TO_REMOVE_REPLACED）；生产保存机制未改，最终两架构通过，不声称根因已解决。
- 当前产物LauncherTests.exe --retention-only通过，隔离重复资源释放保留XML与自定义图标；未调用安装/注册/Explorer。完整安装器退出清理用例本轮未重跑，之前限制保留。
- 本地导出18张编译原生窗口PNG。默认中文728×909、英文835×909、日文756×909，均完整双列；窄窗中文582×639、英文650×639、日文577×639。已目视检查中文默认/窄窗/连续缩放提示区域和英日代表图。默认尺寸只是本机测量结果，未写成固定窗口值。
- 未做任务栏实装、实际跨屏DPI、极小物理屏幕或独立32位系统验收；DPI范围为字体与布局的合成消息测试。没有增加真实网络验证。本地截图来自生产设置代码托管的原生测试窗口，不是安装后的任务栏截图。

### 产物、截图与复现

x64/Release/WeatherBar.exe：672768字节；同配置修改前WeatherBar-before-refine.exe也是672768字节，变化0。新SHA256：46A11A7B391C6F6DB66A285AECF6198E78AC8F2030895E8A969E50A6A5D8F6DB；旧SHA256：6276A9508A486F6F3F53376F748F8DBC21127815EAF916ED458FAF7C8BCBB139。没有新增发布依赖。

截图artifacts/settings-refine/：zh-default.png、zh-narrow.png、zh-hint-resize.png、zh-wide.png、zh-advanced.png、zh-url.png及en/ja对应系列。README列出实际尺寸与源码/产物哈希。

```powershell
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\Release\RegressionTests.exe
.\tests\x64\Release\RegressionTests.exe --capture-settings artifacts\settings-refine
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=Win32 /v:minimal
.\tests\Win32\Release\RegressionTests.exe
msbuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe --retention-only
```

试用入口x64/Release/PreviewSettings.cmd（默认从新配置展示，仅用于布局和新默认）。拖窄/放宽、滚动返回、切换语言/状态，确认单行提示和固定底栏；检查右下关于和来源文字链接；改小档位后恢复默认再取消，确认范围及回退。已有配置恢复已用隔离配置验证，真实组件未替换。

本轮修改src/Settings.cpp、src/ImportConfig.h/.cpp；tests/SettingsTests.cpp、tests/CoreTests.cpp；三语言README、docs/architecture.md、docs/verification.md；新增本地settings-refine与settings-refine-before截图/诊断。其余已有改动保留。未上传GitHub、未发布，等待用户本地确认。

## 原生动态卡片布局与固定底栏（此前本地实现）

本段为此前一轮记录，当前行为以上方最新记录为准。只读参考本地JDM的MainWindow.Settings.cs、ResponsivePanel.cs、MainWindow.xaml与light-settings.png；未修改JDM。WeatherBar仍为应用1.1、配置格式5，未提交/推送、创建远程PR/标签/Release或上传；未安装/卸载、替换运行组件、改系统时区/启动项、重启Explorer或删除真实配置。

### 布局与保留行为

- 五张原生卡片保留基础、显示与外观、间距、悬停、关于。基础占整行；按当前语言字体/DPI测量的卡片最小宽度切换单双列，宽时左侧显示/间距、右侧悬停，各卡片自然高度。删除旧placements/weatherEnd坐标补偿。控件只创建一次并批量移动，没有新UI框架或发布程序依赖。
- 原生标题栏仅关闭，保留拖边缩放。独立内容子窗口拥有页面滚动条，底栏64 DIP与恢复默认/保存/取消不参加滚动；控件由子窗口天然裁剪，不会绘制或接收点击到操作栏。初始窗口和DPI建议矩形按工作区夹取，最小高度允许内容滚动；展开或出现错误不强制增大窗口。
- 搜索列表72 DIP高，提示/状态与应用按钮常驻同排，空列表应用禁用。语言与国家框左对齐，标签间距一致，国家框按三语言最长选项加箭头/边距测量，地点分配剩余宽度；短/普通按钮分别统一测量尺寸。
- 高级方式、未保存草稿、焦点恢复、Tab顺序、轮滚保护、应用预览与保存/取消/关闭继续使用原逻辑。更新状态出现时重算关于卡片。首次无地点、自动时区、未来天气、系统刷新时间、独立悬停开关、数据来源及错误反馈不变。

### 本轮实际结果

- git diff --check通过；Release x64、Win32各25组回归通过，x64解决方案Release构建通过。原有数据、异步、配置原子失败保护和预览回归通过。
- 新增/更新原生布局断言：无最小/最大化样式且可缩放，默认窗口在工作区内，固定底栏在缩放/滚动后始终位于内容之外，滚动nPage不包含底栏，单双列无卡片/控件重叠或文字截断，搜索状态切换不移动地点信息，语言/国家框对齐，按钮组尺寸统一，更新错误可见，折叠草稿与焦点、轮滚与键盘保持。
- 三语言合成WM_DPICHANGED覆盖96/120/144/192 DPI（100%/125%/150%/200%），含窄/宽、坐标/URL、展开/收起；小工作区640×480验证窗口矩形夹取。没有修改系统DPI/分辨率或时区；这些自动测量不等同于实际跨显示器验收。
- 当前打包产物配置保留专项通过：LauncherTests.exe --retention-only在隔离目录重复释放DLL/图标，XML字节及自定义图标保持；不调用安装、卸载、注册或Explorer。完整安装器退出清理用例本轮未重跑，其此前失败仍保留在历史记录。
- 首轮曾遇旧悬停配置往返用例保存错误1175（ERROR_UNABLE_TO_REMOVE_REPLACED）。没有修改生产保存逻辑；为该用例补齐真实旧配置已有的语言字段，避免额外触发无关隐式迁移写入，随后最终x64/Win32通过。错误根因未单独确认，不声称已修复保存机制。
- 已导出并目视检查真实编译原生窗口PNG，中文默认956×1001、窄590×639、宽/高级973×939；另有英文、日文各5种状态，共15张。初次PrintWindow结果全黑，已修正WM_PRINTCLIENT绘制后覆盖重新导出，最终图片已检查有效。截图由测试程序托管生产Settings.cpp并经原生窗口打印导出，不是设计图，也不是任务栏实装或真实跨屏截图。发布程序不新增WIC依赖，WIC仅用于测试导出。

### 截图、体积与复现

截图位于本地artifacts/settings-layout/：zh-default.png、zh-narrow.png、zh-wide.png、zh-advanced.png、zh-url.png；en/ja同名系列。该目录README列出全部图像尺寸及生产Settings.cpp源文件SHA256。截图/测试使用测试程序目录，没有写用户真实配置。

同配置EXE：修改前WeatherBar-before-cards.exe为659456字节，当前WeatherBar.exe为672768字节，增加13312字节（2.02%）。内嵌DLL由507392增至520704字节，增长与EXE一致；没有新增框架或外部依赖。当前EXE SHA256：6276A9508A486F6F3F53376F748F8DBC21127815EAF916ED458FAF7C8BCBB139。

```powershell
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\Release\RegressionTests.exe
.\tests\x64\Release\RegressionTests.exe --capture-settings artifacts\settings-layout
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=Win32 /v:minimal
.\tests\Win32\Release\RegressionTests.exe
msbuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe --retention-only
```

本地产物x64/Release/WeatherBar.exe；免安装查看可用x64/Release/PreviewSettings.cmd或RegressionTests.exe --preview-settings zh-CN（en/ja）。拖动变窄/变宽、滚动内容、展开两种高级输入、切换语言和下拉框，再取消，检查固定按钮及状态恢复。安装与实际任务栏测试由用户决定，当前等待本地确认。

本轮文件：src/Settings.cpp；tests/SettingsTests.cpp、CoreTests.cpp、TestSupport.h；README.md、README.en.md、README.ja.md、docs/architecture.md、docs/verification.md；新增本地artifacts/settings-layout/截图及索引。其余此前未提交改动保留。未新增实际服务联网验证，未进行实际安装/卸载、真实跨屏DPI或独立32位系统验收；极小物理工作区未做真实桌面操作测试。

## 独立悬停条目、系统刷新时间及同排搜索（此前本地实现）

本段为此前一轮记录，布局以上方最新记录为准。应用仍1.1，配置格式仍5；未提交、推送、创建PR/标签/Release或上传。未安装/卸载插件、替换运行组件、改启动项、改系统时区、重启Explorer或删除用户真实配置。

### 本轮行为与兼容

- 新配置默认悬停开启，Fields=129（明天、后天），独立Refresh=true、Status=false；其他天气字段关闭。旧Fields（含0）保留，只为缺失字段填默认；XML新增可选键不递增格式。主开关不清空子项，全空不启用原生提示，也不留下空白/末尾分隔行。旧“今日高低温”迁移仍只对旧格式执行。
- 成功时间保存为time_t，只有已接受的有效天气更新；失败、过期回复、重绘和语言切换不改。显示通过Windows当前动态系统时区（GetDynamicTimeZoneInformation / SystemTimeToTzSpecificLocalTimeEx）转换，没有UTC后缀；无记录“尚未刷新”。日期、当前小时、未来两天、日出日落及刷新调度继续用天气地点秒偏移。状态优先级：配置保护、无地点、更新中、无数据、过期、失败用缓存、正常。关闭状态不改变既有操作错误反馈。
- 语言在第一行，国家/地区、地点、搜索同排；最小客户区640 DIP并限制到显示器工作区，地点编辑使用剩余宽度。居中单行状态与有限候选列表保留，高级折叠、各自错误区域和下拉滚轮保护继续使用已有实现。
- 固定28项国家表（含不限），顺序及代码见三语言README。不按语言排序，持久化代码而非索引，不限不传过滤。TW/HK/MO显示采用指定三语言名称，请求仍用原代码。候选及新配置按名称、国家代码和行政区分别保存，恢复及语言变化重新组合；旧标签只精确迁移可识别的国家组件，其他自由文本原样保留。
- 首次None、旧地点恢复、配置保护、未来每日weather_code及低/高温顺序沿用，不新增天气摘要算法、网络服务、依赖或备份。

### 实际检查

- git diff --check通过。Release x64、Win32各25组回归通过，x64发布解决方案构建通过。新增/更新测试覆盖默认与旧配置、时间/状态四组合及持久化、空提示、系统本地时间与地点偏移分离、失败/过期回复不更新成功时间、固定国家顺序和三语言名称/代码、旧标签安全迁移、预览/保存/取消/关闭恢复。
- 当前打包产物的LauncherTests.exe --retention-only通过：隔离目录两次释放真实资源，配置字节与自定义图标保留。没有调用安装/卸载、注册或Explorer；完整安装器退出清理测试本轮未重跑，其此前失败见历史记录。
- 三语言、96/144/192 DPI原生控件/字体测量通过：同排对齐、国家列表最长名称适配收起宽度、提示与按钮同行、文字不重叠、折叠区域与滚轮/键盘处理。它们属于自动布局检查，不等于真实跨屏DPI目视验收。
- 中间修复了LONG/int模板编译类型、旧窗口宽度断言及测试中写死的旧国家索引。另一次独立悬停配置保存测试失败，之后加入错误诊断重跑未复现，未确认根因；最终x64和Win32均通过，不声称已诊断该次失败。
- 免安装预览成功启动；Computer Use截图因应用授权超时未取得，因此没有完成真实窗口截图或目视检查。测试进程已停止，未保存其界面设置。未做任务栏实装、真实系统时区切换、独立32位系统或实际跨屏测试。
- 本轮在线核对Open-Meteo天气、地点接口及GeoNames官方文档/链接；没有新增真实地点搜索/天气数据验证。README已有完整参数URL、可信WGS84步骤、坐标系区别、配置恢复边界及AI提示词A/B继续保留，未把文档示例写入默认配置。
- 同配置x64 EXE：本轮前保留WeatherBar-before-hover.exe为652800字节，新WeatherBar.exe为659456字节，增加6656字节（约1.02%）。PE资源读取确认内嵌DLL从500736增至507392字节；其他PE节区大小不变，增长全部来自DLL载荷，没有新增依赖。
- EXE SHA256：0AB690B360D84D0887D0FDC3CE8C186AEDD7C941EA2888D5171B6698B40EBBC8。

### 复现与本地试用

在VS开发者命令环境运行：

```powershell
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\Release\RegressionTests.exe
msbuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=Win32 /v:minimal
.\tests\Win32\Release\RegressionTests.exe
msbuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
```

双击x64/Release/PreviewSettings.cmd，或运行tests/x64/Release/RegressionTests.exe --preview-settings zh-CN（也可用en/ja）。此入口从全新空配置展示默认选项，保存仅写测试程序目录；取消不写。主构建产物为x64/Release/WeatherBar.exe，本轮未运行其安装流程。用户决定实际安装后，在已有XML的同目录测试更新/重装恢复；不要删除配置、不要将全套回归用于个人配置目录。

本轮文件：src/ImportConfig.h/.cpp、src/DeskBand.cpp、src/Location.h/.cpp、src/Settings.cpp；tests/CoreTests.cpp、DisplayTests.cpp、LocationTests.cpp、SettingsTests.cpp；README.md、README.en.md、README.ja.md、docs/architecture.md及本文。前轮其他工作区改动原样保留。

## 未选择地点、配置保留与使用帮助（此前本地实现）

本段为此前一轮记录，当前行为以上方最新记录为准。应用仍1.1，配置格式5新增None状态，未推送GitHub、创建PR/标签/Release或上传；未安装/卸载、替换运行组件、改启动项、重启Explorer或删除用户真实配置。

### 实现与恢复边界

- 全新配置所有语言均None，名称/URL/经纬度/时区为空，国家不限。空状态可保存外观并正常重载。移除首都、默认URL及默认坐标；旧有效URL（含历史北京默认URL）按旧文件读取，不误判为空。
- HasConfiguredLocation守卫StartFetch、ArmTimer和OnTimer；未选地点不发请求、不建立重复天气调度，立即刷新禁用。取消首次地点预览清空天气/时区缓存并杀定时器，代次拒绝晚到结果。任务栏保留无数据占位、悬停引导设置。
- 配置路径仍是DLL/EXE目录WeatherBar.xml，当前文件优先，仅同目录旧名后备。已有模式、名称、坐标、URL、时区及其他设置保留；有效元数据可重建URL，已有原URL也继续序列化保留无关参数。损坏XML、无效URL、未来版本保护不写原件，并在设置/悬停提示。
- 安装资源释放只处理DLL、图标和许可证，不写XML；默认卸载不删除配置或程序目录。同目录重新安装可以恢复，迁移整个目录需带XML/icons；仅移动EXE、删除XML/目录、彻底卸载、换Windows用户或文件不可读不保证恢复。没有新增磁盘扫描或重构安装位置。
- 状态改为居中单行STATIC，无滚动条；成功、无结果、错误按状态替换，不叠加固定说明。候选存在时与应用按钮同排，输入提示不含具体城市示例；坐标明确WGS84（GPS）。自动时区、未来两天、折叠和下拉滚轮保护继续沿用。

### 实际验证

- Release x64、Win32各24组回归通过。新增覆盖：三语言初始空状态、保存空地点重启、旧有效默认URL、0坐标合法、空地点刷新/调度不请求、首次预览取消停止调度和清缓存、过期响应不复活地点、空状态保存外观。原有配置原子写入失败、损坏/未来版本、地点恢复、自动时区/跨日跨月跨年、悬停、三语言96/144/192 DPI布局和滚轮保护测试通过。
- x64 Release构建与静态分析完成，最终保留10条已有告警（6栈空间、2GetTickCount、2COM批注）。本轮曾出现空缓存整值临时对象的栈空间告警和展开列表句柄可空告警，分别改为逐日清理及显式句柄判断后消除；不是零告警构建。
- 安装器整套测试运行到旧“退出后清理EXE”步骤失败：cleanup exit=4294967295、parent exit=0；未确定根因，不把整套标为通过。新增--retention-only定向模式通过：隔离临时目录两次释放真实DLL/图标资源，XML字节不变、自定义图标保留。它不调用安装、注册、启动项、Explorer或真实配置删除。
- 官方文档/链接已在线核对：Open-Meteo天气、地点接口、GeoNames WGS84说明，高德GCJ-02说明及百度坐标说明。README完整东京天气示例实测成功：Asia/Tokyo、32400秒，168小时、7天，返回全部8个hourly字段及每日高低温/weather_code/日出日落。示例未写入运行配置。
- 未完成真实桌面截图/手动点击目视、任务栏实装、独立32位系统或真实跨屏DPI验收；合成DPI与原生控件测量不等同实机目视。单响应固定偏移的DST边界限制保留。
- 同配置x64 Release启动程序：修改前保留产物WeatherBar-before-empty.exe与本轮WeatherBar.exe均为652800字节，大小变化0字节。

### 文档与复现

三语言README新增“首次运行与地点设置”“搜索和应用”“WGS84坐标/官方页面URL”“配置保存、更新与重装”及可复制AI提示词A/B。参数与PrepareForecastUrl一致，含每日weather_code；惠城区仅为文档示例，不是默认或占位符。AI不能保证联网/准确，需用户核对来源。

可用已有命令构建RegressionTests（x64/Win32）并运行；安装器定向检查：

```powershell
MSBuild tests\LauncherTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe --retention-only
```

双击x64/Release/PreviewSettings.cmd可免安装查看全新空状态，保存仅写测试目录。设置地点保存后，普通测试程序会以自身目录加载配置；不要运行整套回归来检查个人设置，因为回归使用该测试目录作为隔离夹具。真正安装是否执行由用户决定。

本轮修改：src/ImportConfig.h/.cpp、DeskBand.cpp、Settings.cpp、Weather.cpp；launcher/Launcher.cpp（仅修正保留配置提示）；tests/CoreTests.cpp、DisplayTests.cpp、LocationTests.cpp、SettingsTests.cpp、LauncherTests.cpp、TestSupport.h；三语言README、docs/architecture.md及本文。保留前轮其他未提交修改。


## 基础设置、统一自动时区与默认首都（本轮本地验证）

本段为此前一轮实现的历史记录；默认首都及配置格式4已由本文顶部当前实现替代。该轮应用为1.1、配置格式4；无GitHub推送/PR/标签/Release或上传，未安装、卸载、替换运行组件、修改启动项或重启Explorer。保留此前所有相关未提交改动。

### 当前行为与兼容

- 基础设置依次为语言、国家、城市/区县搜索、按需候选、单行提示与应用地点、当前地点/响应时区、自定义地点设置。固定提示为原生单行STATIC，无滚动条和多行编辑样式；搜索、坐标、URL错误独立归属。
- 自定义地点标题采用轻微不同底色、边框和加粗文字；保留整行/箭头/Enter/空格开关，隐藏焦点与空间回收。应用坐标/地址按钮按客户区水平居中，语言不进入折叠区。
- 删除手动UTC控件、运行配置字段、常量和生产请求的手动偏移分支。三种来源先校验响应timezone与utc_offset_seconds，再归档当地数据；更新调度、最近刷新、日出日落及未来两天沿用秒表示。失败保留有效缓存和对应时区，新地点未验证不应用，首次未知不默认系统或北京时间。
- 自定义原URL不改；实际请求按参数边界识别timezone（支持百分号编码键名），缺失/空值补auto，重复取最后一个非空值并合并。无关参数保留，字段补齐幂等。
- DefaultBandConfig仅全新初始化按最终语言选首都；命名地点保存名称、坐标、IANA时区、国家并重建URL，不写默认自定义地址；Custom仍保存原URL。已有文件先从旧地点语义解析，旧TimezoneOffset仅兼容读取并忽略，不再写出或参与运行；原子保存和损坏/未来配置保护保留，不生成备份。切换语言与恢复外观不换地点。
- 组合框本体及其原生展开列表统一子类化拦截滚轮；收起时转发主内容滚动，展开时仅滚动列表视口，防止选中项/预览变化；键盘与点击选择保留，WM_NCDESTROY移除回调。
- 来源署名移到关于与更新中的数据来源入口，原生TaskDialog提供Open-Meteo、GeoNames及CC BY 4.0来源许可链接和简化说明。基础区域不再有署名静态行。

常规Release x64最终EXE：652800字节；本轮开始时同配置旧产物646144字节，增加6656字节（1.03%），没有明显增长。旧产物保留于x64/Release/WeatherBar-before-basic.exe。坐标模式保存重载后仍重建timezone=auto，避免把已取得的IANA名称固化为坐标请求参数。

### 数据查证和真实请求

同一官方Open-Meteo Geocoding接口(count=1、countryCode限制、language=en)实测：

| 初始语言 | 默认地点 | 纬度 | 经度 | IANA | GeoNames ID |
| --- | --- | --- | --- | --- | --- |
| 简体中文 | 北京/中国 | 39.9075 | 116.39723 | Asia/Shanghai | 1816670 |
| 日文 | 东京/日本 | 35.6895 | 139.69171 | Asia/Tokyo | 1850147 |
| 英文 | 伦敦/英国 | 51.50853 | -0.12574 | Europe/London | 2643743 |

正式WinHTTP天气代码实测上述坐标分别返回Asia/Shanghai/28800、Asia/Tokyo/32400、Europe/London/0，均hasData=1；加德满都返回Asia/Kathmandu/20700、hasData=1。首次默认城市无需在线搜索；本轮搜索只用于查证常量。

来源依据：https://open-meteo.com/en/docs/geocoding-api 、https://open-meteo.com/en/docs 、https://open-meteo.com/en/licence 、https://www.geonames.org/about.html 。官方资料确认坐标为WGS84、API数据CC BY 4.0并需署名。DST边界仍按下方已实测记录限制，未新增完整IANA转换层。

### 测试与限制

- Release x64/Win32各22组固定响应及原生窗口回归通过：首都与国家、默认非Custom、命名地点序列化、旧URL/地点保持、语言不换地点、timezone空/缺/编码/重复/幂等、三来源响应偏移、半小时/四十五分钟、跨日跨月跨年、保存取消关闭和失败保护。
- 组合框逐个检查收起/焦点/展开列表滚轮不改值或预览，键盘方向键仍可改选项；恢复选项和语言后再做布局检查。
- 三语言、96/144/192 DPI原生窗口测量：语言首行、提示与按钮同排、提示无滚动、居中按钮、两种自定义内容、错误归属、折叠/隐藏Tab、候选可见时无重叠、静态文字与局部错误换行高度。原任务栏/未来两天悬停逻辑回归通过。
- 首轮发现timezone位置改变导致补齐不幂等，调整内部合并顺序后通过。移除旧测试的手动偏移预期，改为响应时区断言；命名配置不再保存原ForecastUrl的断言通过。
- 本轮x64 Release构建及静态分析完成，仍有10条已有告警：6条栈空间、2条GetTickCount、2条COM批注；不是零告警构建。
- 未完成真实桌面截图/鼠标点击目视、真正跨屏DPI或真实任务栏安装测试；合成DPI消息及原生控件测量不等同实机目视。数据来源弹窗和浏览器链接由用户实际点击验收；未在自动测试中打开外部浏览器。

本轮修改：src/Settings.cpp、ImportConfig.h/.cpp、Weather.h/.cpp、Location.cpp、DeskBand.h/.cpp；tests/CoreTests.cpp、LocationTests.cpp、SettingsTests.cpp；三语言README、docs/architecture.md和本文。保留其他前轮未提交修改。

免安装测试：双击x64/Release/PreviewSettings.cmd，或tests/x64/Release/RegressionTests.exe --preview-settings zh-CN（另支持en/ja）。先检查基础顺序和候选提示，再测试自定义区域与滚轮，最后保存/取消/关闭。此入口仅保存测试目录配置。正式本地产物为x64/Release/WeatherBar.exe，是否安装由用户决定。


## 主设置整合地点搜索与高级折叠（2026-11-08，本地未发布）

本段为当前实现，后面的独立地点窗口记录是历史验证，不再代表当前界面。保留此前未提交修改；应用1.1、配置格式4不变，无GitHub推送/PR/标签/Release，无安装、启动项修改、组件替换或Explorer重启。

### 功能与实现

- 地点搜索直接放在主窗口天气数据区域，删除独立地点窗口、入口及专属消息循环，复用国家表、UTF-8编码、JSON解析、WinHTTP、共享异步对象和秒级时区。
- 未搜索/无结果/失败不显示空列表，仅按需展示紧凑状态；候选列表限高96 DIP并内部滚动，不自动选择第一项。当前有效来源、地点及自动/手动时区在主窗口只读展示。
- 高级设置为整行原生按钮，右侧上下箭头，支持Enter/空格。搜索/坐标模式默认收起，自定义地址模式默认展开。显示一种高级编辑器，保留两种输入草稿；折叠不更改配置或联网，隐藏控件不能Tab聚焦，隐藏原焦点前移至标题。
- 所有来源都明确应用并验证天气后才预览。自定义地址应用保留手动UTC小时偏移，不被自动时区覆盖；未应用编辑不影响外观预览和主保存。保存、取消、关闭和写入失败保护继续沿用原机制。
- 天气区高度按状态动态变化，下方分组和背景统一平移；预留滚动条宽度避免外观列重叠。窗口高度不无限增加，内容可滚动；语言保持常规设置位置。

### 实际验证

- Release x64和Win32各22组回归全部通过。新增主窗口固定响应检查覆盖：默认紧凑、无独立窗口、展开/收起、Enter/空格、隐藏焦点与Tab、两编辑器输入保留、未应用输入不保存、过期搜索、候选不自动选中、网络/无结果/解析失败、手动地址偏移、关闭恢复。
- 三种语言，96/144/192 DPI；包含候选可见、两种高级方式和展开/收起组合的原生控件坐标、重叠及静态文案实际字体测量。继承的天气日期、当前小时、未来两天悬停、旧配置、保存/取消/关闭及失败保护测试通过。
- 曾发现动态滚动条缩小客户区导致外观列重叠；调整滚动条宽度预留后重跑通过。旧测试按“编辑就应用”检查URL，已改为验证明确应用及未应用输入不干扰主保存。
- x64 Release构建完成；本轮MSVC静态分析10条告警（6条栈空间、2条GetTickCount、2条COM批注），不是零告警构建。删去独立窗口后比上轮减少2条栈空间告警。
- 真实生产WinHTTP路径复测：杭州2条，Hangzhou3条，惠城区0条，惠城1条且在揭阳市，惠州市惠城区0条；Paris+FR和Paris+US各10条并对应国家。杭州坐标自动返回Asia/Shanghai、28800秒；加德满都坐标返回Asia/Kathmandu、20700秒，均有天气数据。无结果均是成功响应的0候选，不是网络或解析失败。
- 未执行真实桌面截图目视、实际任务栏安装/跨屏DPI或独立32位系统验收。原生自动化和合成DPI检查不等同真实跨屏验证。单次Open-Meteo响应固定偏移的DST限制继续存在，未声称完整支持夏令时边界。

### 本地测试与修改清单

最终常规Release x64 EXE为646144字节，修改前同配置产物645632字节（保留在x64/Release/WeatherBar-before-inline.exe），差值512字节，变化0.08%。无明显增长。


双击 `x64/Release/PreviewSettings.cmd`，或运行：

```powershell
.\tests\x64\Release\RegressionTests.exe --preview-settings zh-CN
```

此入口不安装插件，保存只写测试程序目录的配置。主窗口选择国家、搜索、明确选择候选、应用；展开高级设置切换坐标/URL，检查草稿保留和应用行为；再测试保存、取消、关闭。en/ja参数可切换预览语言。

本轮修改：`src/Settings.cpp`、`src/Location.h/.cpp`、`src/Weather.h/.cpp`；`tests/CoreTests.cpp`、`tests/LocationTests.cpp`、`tests/SettingsTests.cpp`、`tests/TestSupport.h`；三语言README、`docs/architecture.md`和本文。前轮其他未提交修改保留。未增加新依赖或配置格式。


## 本地地点与自动时区功能（2026-11-08，尚未发布）

范围：WeatherBar 仓库、codex/weatherbar 分支；保留此前取消迁移备份及悬停选项排序的未提交改动。应用版本仍1.1，配置格式4。未推送、未创建PR/标签/Release，未上传产物；未安装/卸载、替换运行组件或重启Explorer。

### 固定响应与原生窗口检查

- x64、Win32 Release 各21组回归通过。新增测试覆盖UTF-8（包括JSON Unicode转义）、countryCode及不限、最多10条候选、缺失行政区、重复名称、无结果/错误JSON、候选不自动选中、输入/国家变化拒绝旧请求、天气失败保留原状态。
- 坐标边界、负数、小数、精度、NaN/无穷/非法文本；生成天气URL的字段、补齐幂等性、高级请求forecast_days/models参数保留。
- 自动时区及UTC+05:30、+05:45、-03:30；当前小时/日期、非整数时区跨年与下一小时；旧配置自定义URL和整数小时偏移保留；配置元数据读写。
- 地点预览、保存、取消、关闭恢复配置及缓存；带地点元数据的短写/关闭/权限失败保护。旧天气代次拒绝测试继续通过。
- 三语言地点原生窗口在96/144/192 DPI的坐标及标签实测宽度检查通过；长候选水平滚动，状态区可滚动，窄屏/高DPI有垂直滚动。该结果来自原生窗口自动化及合成DPI消息，不等同真实跨屏目视验收。
- 桌面工具未能枚举到独立测试窗口，未完成截图目视检查；未操作其他用户窗口。未声称完成真实任务栏、真实跨屏或独立32位系统验证。
- x64 Release DLL和单EXE构建及MSVC静态分析完成：12条告警（8条栈空间、2条GetTickCount、2条COM批注），无未初始化成员告警。新增栈空间提示来自沿用值类型天气缓存的设置/地点验证路径；不是零告警构建。

### 真实官方接口结果

采用少量Python请求核对服务；另以测试程序的生产WinHTTP路径验证杭州及手动坐标。

| 查询（language=zh） | 国家 | 实际结果 |
| --- | --- | --- |
| 杭州 | CN | 2条：浙江杭州、四川同名地点，均Asia/Shanghai |
| Hangzhou | CN | 3条，含杭州及机场 |
| 惠城区 | CN | 无结果，非网络/解析失败 |
| 惠城 | CN | 1条，广东揭阳市；不是惠州市惠城区 |
| 惠州市惠城区 | CN | 无结果，非网络/解析失败 |
| Paris | FR | 10条，国家FR |
| Paris | US | 10条，国家US |

坐标(30.27,120.15)自动返回Asia/Shanghai、28800秒；(27.7172,85.324)自动返回Asia/Kathmandu、20700秒。生产WinHTTP/正式解析路径实测：杭州2条、加德满都有天气数据且20700秒。没有保证任意区县或完整地址都能匹配。

纽约实际请求（America/New_York，2026-10-31至11-02）返回固定-18000秒，每日UTC时间都为05:00，间隔均86400秒；与官方“响应范围内忽略DST切换”的说明一致。因此本实现仅随刷新更新固定偏移，不声称完整DST支持。Windows提供动态时区转换，但本次未引入IANA映射/逐点转换和每日聚合重构。

官方依据：https://open-meteo.com/en/docs/geocoding-api 、https://open-meteo.com/en/docs 。搜索数据来源GeoNames，三语言README补充署名及能力限制。

### 产物与文件清单

常规 Release x64 EXE：645632字节；本轮开始时的同配置旧产物记录为577024字节，增加68608字节（11.89%）。增加来自地点窗口三语言代码及Windows JSON投影/调用代码；没有增加图片、城市库或打包第三方运行库。旧提交未重新构建。

最终常规构建 SHA-256：EDBBA691720C2719B25F0287E9C9506D6AA224C745B814B6879766CF5AE8EF03。

新增：`src/Location.h`、`src/Location.cpp`、`src/ServiceJson.h`、`tests/LocationTests.cpp`。

修改（包含保留的前两轮本地改动）：`src/Weather.h/.cpp`、`src/DeskBand.h/.cpp`、`src/ImportConfig.h/.cpp`、`src/Settings.cpp`、`src/WeatherBar.vcxproj`；`tests/CoreTests.cpp`、`tests/DisplayTests.cpp`、`tests/SettingsTests.cpp`、`tests/TestSupport.h`、`tests/RegressionTests.vcxproj`；`README.md`、`README.en.md`、`README.ja.md`、`docs/architecture.md`、`docs/verification.md`。本地忽略目录还生成了`x64/Release/PreviewSettings.cmd`，用于免安装预览。

### 本地操作与复现

先通过Visual Studio的VsDevCmd.bat初始化工具链，执行：

```powershell
MSBuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\Release\RegressionTests.exe
MSBuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=Win32 /v:minimal
.\tests\Win32\Release\RegressionTests.exe
MSBuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /v:minimal
.\tests\x64\Release\RegressionTests.exe --check-location 杭州 CN
.\tests\x64\Release\RegressionTests.exe --check-coordinates 27.7172 85.324
.\tests\x64\Release\RegressionTests.exe --preview-settings zh-CN
```

预览也可双击本地产物 `x64/Release/PreviewSettings.cmd`，不会安装插件；其保存行为仅写测试程序目录下XML。使用zh-CN/en/ja切换预览语言。点击选择地点→搜索→明确选择→应用；或填写WGS84经纬度→应用坐标；返回主页面测试保存/取消/关闭。主发布产物路径为 `x64/Release/WeatherBar.exe`，是否安装由用户决定。程序中的GitHub操作未作为本轮测试执行。


## 后续调整：设置选项与悬停顺序一致

悬停选项按从左到右、从上到下排列为：明天、后天、紫外线指数、风向、风速、相对湿度、体感温度、降水概率；Tab 顺序同步，配置位和控件 ID 不变。本轮三语言原生窗口回归验证各项实际坐标和 Tab 顺序，x64 Release 20 组回归全部通过，DLL 和 EXE 构建成功。未重复 Win32 或任务栏目视验证。

## 后续调整：配置迁移不生成备份

配置升级及旧名配置迁移不再生成 `.before-v*.bak`；沿用原子保存，损坏配置和更高格式版本仍禁止覆盖。旧名源文件及已经存在的备份不主动删除。新增回归检查迁移不创建备份、迁移写入失败保留原 XML。本轮 x64 Release 20 组回归全部通过，x64 Release DLL 和 EXE 构建成功；未重复 Win32 或真实任务栏验证。下方 1.1 发布记录中的备份行为为历史记录。

## 1.1 未来两天天气悬停（2026-11-08）

- 应用版本 1.1，配置格式 3；明天、后天独立控制整条天气及最低 / 最高温，新安装均关闭。格式 2 及以前的今日高低温位只迁移一次，开启转两天开启，关闭或缺项转两天关闭；复用迁移前备份及原子保存，格式 3 保留独立选择。
- daily 增加 weather_code，同一请求缓存今天至后天，按配置时区当地日期匹配。每日代码是当天最严重现象的单一摘要，不分析逐小时变化。毛毛雨/阵雨归雨，冻毛毛雨归冻雨，米雪/阵雪归雪，含冰雹雷暴归雷暴。未知代码显示天气未知；缺失温度各自保持 --，顺序为最低 / 最高，不补零或借用其他日期。
- 每天独立条目，原生提示字体测量决定单行或日期/内容两行；沿用刷新时间宽度、标点与状态行，不无限加宽，不额外加空行。任务栏今日高低温及其他悬停项保持原逻辑。
- Release x64、Win32 各 20 组回归全部通过；覆盖跨月跨年当地日期、短数组、空值、天气归类、旧配置及新版独立选择、保存/取消/关闭预览恢复、三语言 256 种悬停组合、单行/分行和真实字体宽度。原生设置控件边界及重叠检查通过；未进行真实任务栏或跨屏 DPI 目视验收。
- x64 Release 单 EXE 和静态分析完成。分析共 9 条告警：GetTickCount 2 条、COM SAL 2 条、栈空间 5 条；三天缓存扩大导致设置应用/还原、解析及工作线程出现栈空间提示。保留现有值类型缓存架构，未扩大重构；这不是零告警构建。
- x64 安装器测试在沙箱内创建目录联接失败；正常权限重跑通过，包括图标保留、占用 DLL、目录联接、备份清理、退出后删除和无关文件保护。未执行真实安装或重启 Explorer。
- 首次直接调用 MSBuild 遇 MSPDB140.DLL 版本错误；通过 VsDevCmd -arch=x64 -host_arch=x64 初始化后成功。首轮回归发现更新检查测试仍基于 1.0 的版本预期，更新为 1.1 后两种架构重跑全部通过。
- 大小：既有 Release x64 1.0 EXE 571904 字节（GitHub v1.0 附件 API 同大小），1.1 同配置 EXE 576512 字节，增加 4608 字节（0.81%），无明显增长；没有重新构建旧提交。附件仅 WeatherBar.exe，校验值写入发行说明。
- 环境仍为本机 Windows x64，Win32 回归不等于独立 32 位系统实装。未完成真实服务故障联调、独立 Windows 10、所有 DPI 和真实任务栏验收。

复现：先调用 Visual Studio Common7/Tools/VsDevCmd.bat；其余命令沿用下方，RegressionTestsV1 改为 RegressionTests。安装器需正常权限创建目录联接。

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
