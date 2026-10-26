# WeatherBar

Lightweight weather for the classic Windows taskbar.

[简体中文](README.md) | **English** | [日本語](README.ja.md)

**Current version: 1.0** · [First stable release](https://github.com/zdxxm10/WeatherBar/releases/tag/v1.0) · [Download WeatherBar.exe](https://github.com/zdxxm10/WeatherBar/releases/download/v1.0/WeatherBar.exe)

## Install

Download `WeatherBar.exe` from [Releases](https://github.com/zdxxm10/WeatherBar/releases/latest), put it in a writable dedicated folder and run it. The EXE contains the DLL and icons. Allow Windows to display the toolbar, or enable WeatherBar in the classic taskbar's Toolbars menu. Installation registers the component and startup entry for the current user.

The release targets x64 Windows 10 classic taskbars. The native Windows 11 taskbar does not support DeskBands. Other classic taskbar implementations and 32-bit installation are not verified.

Only the EXE is needed from the release page; it includes the DLL, icons and licenses. The release notes list its SHA-256 checksum, so no separate checksum or license TXT download is required. Source license links are below.

## Settings

Right-click the bar to open settings. Use a full Open-Meteo `/v1/forecast` API URL, including hourly `temperature_2m,weather_code`, daily `temperature_2m_max,temperature_2m_min,sunrise,sunset`, and `timeformat=unixtime`. Match its timezone with the integer UTC offset; daylight-saving changes require manual adjustment. The default location is Dongcheng, Beijing (UTC+8).

Appearance and language changes preview immediately. Click a color button for swatches and RGB sliders/values; clicking elsewhere hides the palette. The URL field wraps at the available width and shows three lines. The window opens at its minimum size and can grow; small work areas use scrolling.

**Save** writes the configuration; **Cancel** or closing restores the original settings and forecast cache. **Reset appearance** keeps location, timezone and language. Invalid unfinished data input does not interrupt appearance preview. Requests are debounced during typing.

Languages: Simplified Chinese, English and Japanese. System default maps all Chinese regions to Simplified Chinese, Japanese to Japanese, and others to English. Install/uninstall always follows the system independently of the setting.

Forecasts refresh at local 00/06/12/18 hours with a default 15-minute stagger (`StaggerSeconds`, 0–1800). The optional hover tooltip always ends with last refresh and status. Select daily high/low, hourly UV index, wind direction/speed, relative humidity, feels-like temperature and precipitation chance individually (all off by default). Missing values show `--`; up to two items share a row based on measured text width. Tooltip changes preview immediately and roll back on Cancel. Additional weather fields share the existing request and refresh schedule; wind speed uses m/s. Check updates contacts GitHub only when clicked; it never downloads or installs updates automatically. If the API fails, it checks the official latest-release redirect for this repository; failures report an HTTP or WinHTTP code.

## Update / uninstall

Uninstall while keeping settings, wait for Explorer to restart, replace the EXE in the same folder and run again. Existing custom SVG files are preserved; missing icons are added. Old config names migrate with the original retained. Back up XML and icons before the first upgrade from an older release; use the old EXE to uninstall that version first.

Default uninstall keeps configuration, backups, logs, icons and EXE; it removes the inactive DLL, registration and startup entry. Complete removal also deletes app settings, backups and icons, then uses built-in Windows PowerShell to delete the EXE after exit and the folder if empty. Unrelated files and junction targets are preserved. Organization policies blocking PowerShell may require manual EXE deletion. Explorer restarts and open folder windows close: save your work first.

## Build / test

Use Visual Studio C++ desktop tools v145 and Windows SDK. Plugin: C++20; launcher: C++17. No additional UI framework or old package dependencies are required.

```powershell
MSBuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
MSBuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\Release\RegressionTests.exe
MSBuild tests\LauncherTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe
```

Output: `x64/Release/WeatherBar.exe`. Tests use isolated output folders, do not install or restart Explorer. For a settings preview, pass `--preview-settings en` to RegressionTests.exe. Win32 logic tests are supported; real taskbar, startup, network and cross-monitor DPI need separate validation. See [architecture](docs/architecture.md) and [verification](docs/verification.md).

## Credits / license

Based on [Nusudo-junior/WeatherDeskBand](https://github.com/Nusudo-junior/WeatherDeskBand), retaining its [MIT license](LICENSE). Weather: [Open-Meteo](https://open-meteo.com/). Icons: Bas Milius [Meteocons](https://github.com/basmilius/meteocons), [MIT](src/icons/METEOCONS-LICENSE.txt). [Report issues](https://github.com/zdxxm10/WeatherBar/issues).
