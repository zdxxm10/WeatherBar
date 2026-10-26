#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windows.h>
#include <string>
#include <vector>
#include "../src/ImportConfig.h"
#include "../src/LegacyNames.h"

HINSTANCE g_hinstDll = nullptr;
Language g_language = Language::System;

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")

const CLSID CLSID_WeatherBar = { 0x112143a6, 0x62c1, 0x4478, { 0x9e, 0x8f, 0x87, 0x26, 0x99, 0x25, 0x5e, 0x2e } };
const wchar_t kClsid[] = L"{112143A6-62C1-4478-9E8F-872699255E2E}";
const wchar_t kToolbarCache[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Discardable\\PostSetup\\Component Categories\\{00021492-0000-0000-C000-000000000046}";

const IID IID_ITrayDeskBand = { 0x6D67E846, 0x5B9C, 0x4db8, { 0x9C, 0xBC, 0xDD, 0xE1, 0x2F, 0x42, 0x54, 0xF1 } };
const CLSID CLSID_TrayDeskBandLocal = { 0xE6442437, 0x6C68, 0x4f52, { 0x94, 0xDD, 0x2C, 0xFE, 0xD2, 0x67, 0xEF, 0xB9 } };

struct ITrayDeskBandLocal : IUnknown {
	virtual HRESULT STDMETHODCALLTYPE ShowDeskBand(REFCLSID clsid) = 0;
	virtual HRESULT STDMETHODCALLTYPE HideDeskBand(REFCLSID clsid) = 0;
	virtual HRESULT STDMETHODCALLTYPE IsDeskBandShown(REFCLSID clsid) = 0;
	virtual HRESULT STDMETHODCALLTYPE DeskBandRegistrationChanged() = 0;
};

bool WriteStringValue(HKEY root, const wchar_t* key, const wchar_t* name, const wchar_t* value) {
	HKEY opened = nullptr;
	if (RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr) != ERROR_SUCCESS)
		return false;
	DWORD bytes = (DWORD)((wcslen(value) + 1) * sizeof(wchar_t));
	LONG result = RegSetValueExW(opened, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes);
	RegCloseKey(opened);
	return result == ERROR_SUCCESS;
}

std::wstring ExePath() {
	wchar_t path[MAX_PATH];
	DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return L"";
	return path;
}

std::wstring DirectoryOf(const std::wstring& path) {
	size_t slash = path.find_last_of(L'\\');
	if (slash == std::wstring::npos)
		return L"";
	return path.substr(0, slash);
}

bool RegisterBand(const std::wstring& dllPath) {
	HMODULE module = LoadLibraryW(dllPath.c_str());
	if (!module)
		return false;
	using RegisterFn = HRESULT(STDAPICALLTYPE*)();
	auto registerServer = reinterpret_cast<RegisterFn>(GetProcAddress(module, "DllRegisterServer"));
	HRESULT result = registerServer ? registerServer() : E_FAIL;
	FreeLibrary(module);
	return SUCCEEDED(result);
}

bool RemoveStartupValue(const wchar_t* name) {
	HKEY run = nullptr;
	LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &run);
	if (result != ERROR_SUCCESS) return result == ERROR_FILE_NOT_FOUND;
	result = RegDeleteValueW(run, name);
	RegCloseKey(run);
	return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool EnableStartup(const std::wstring& exePath) {
	std::wstring quoted = L"\"" + exePath + L"\" /silent";
	if (!WriteStringValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"WeatherBar", quoted.c_str())) return false;
	return RemoveStartupValue(kLegacyStartupName);
}

bool HasArgument(const wchar_t* argument) {
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv)
		return false;
	bool found = false;
	for (int i = 1; i < argc; ++i) {
		if (lstrcmpiW(argv[i], argument) == 0)
			found = true;
	}
	LocalFree(argv);
	return found;
}

struct TrayBand {
	HRESULT init = E_FAIL;
	ITrayDeskBandLocal* band = nullptr;

	TrayBand() {
		init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		HRESULT created = CoCreateInstance(CLSID_TrayDeskBandLocal, nullptr, CLSCTX_LOCAL_SERVER, IID_ITrayDeskBand, reinterpret_cast<void**>(&band));
		if (FAILED(created))
			created = CoCreateInstance(CLSID_TrayDeskBandLocal, nullptr, CLSCTX_INPROC_SERVER, IID_ITrayDeskBand, reinterpret_cast<void**>(&band));
		if (FAILED(created))
			band = nullptr;
	}

	~TrayBand() {
		if (band)
			band->Release();
		if (SUCCEEDED(init))
			CoUninitialize();
	}

	bool Shown() const {
		if (!band)
			return false;
		return band->IsDeskBandShown(CLSID_WeatherBar) == S_OK;
	}
};

void DeleteTree(HKEY root, const std::wstring& key) {
	SHDeleteKeyW(root, key.c_str());
}

std::wstring MachineClsidKey() {
	return std::wstring(L"Software\\Classes\\CLSID\\") + kClsid;
}

bool MachineRegistrationExists() {
	HKEY key = nullptr;
	LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, MachineClsidKey().c_str(), 0, KEY_READ, &key);
	if (result == ERROR_SUCCESS)
		RegCloseKey(key);
	return result == ERROR_SUCCESS;
}

bool IsElevated() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return false;
	TOKEN_ELEVATION elevation{};
	DWORD size = 0;
	BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
	CloseHandle(token);
	return ok && elevation.TokenIsElevated;
}

bool DeleteMachineRegistration() {
	LONG result = SHDeleteKeyW(HKEY_LOCAL_MACHINE, MachineClsidKey().c_str());
	return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool RemoveMachineRegistration() {
	if (!MachineRegistrationExists())
		return true;
	if (IsElevated())
		return DeleteMachineRegistration();

	std::wstring exe = ExePath();
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"runas";
	info.lpFile = exe.c_str();
	info.lpParameters = L"/uninstall-machine";
	info.nShow = SW_HIDE;
	if (!ShellExecuteExW(&info))
		return false;
	if (info.hProcess) {
		WaitForSingleObject(info.hProcess, 30000);
		CloseHandle(info.hProcess);
	}
	return !MachineRegistrationExists();
}

bool RemoveStartup() {
	bool current = RemoveStartupValue(L"WeatherBar");
	bool legacy = RemoveStartupValue(kLegacyStartupName);
	return current && legacy;
}

void UnregisterDll(const std::wstring& dllPath) {
	HMODULE module = LoadLibraryW(dllPath.c_str());
	if (!module)
		return;
	using UnregisterFn = HRESULT(STDAPICALLTYPE*)();
	auto unregister = reinterpret_cast<UnregisterFn>(GetProcAddress(module, "DllUnregisterServer"));
	if (unregister)
		unregister();
	FreeLibrary(module);
}

bool DeleteConfigFiles(const std::wstring& directory, const std::wstring& filename) {
	bool ok = true;
	std::wstring backup = filename + L".before-v" + std::to_wstring(kConfigSchemaVersion) + L".bak";
	for (const std::wstring& name : { filename, backup, std::wstring(L"log.txt") }) {
		if (!DeleteFileW((directory + L"\\" + name).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
			ok = false;
	}
	// 清理各代迁移备份，仅匹配本程序的数字版本命名，避免误删用户的其他备份。
	WIN32_FIND_DATAW entry{};
	HANDLE search = FindFirstFileW((directory + L"\\" + filename + L".before-v*.bak").c_str(), &entry);
	if (search != INVALID_HANDLE_VALUE) {
		const std::wstring prefix = filename + L".before-v";
		do {
			std::wstring name = entry.cFileName;
			std::wstring version = name.substr(prefix.size(), name.size() - prefix.size() - 4);
			if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !version.empty()
				&& version.find_first_not_of(L"0123456789") == std::wstring::npos
				&& !DeleteFileW((directory + L"\\" + name).c_str())) ok = false;
		} while (FindNextFileW(search, &entry));
		if (GetLastError() != ERROR_NO_MORE_FILES) ok = false;
		FindClose(search);
	}
	else if (GetLastError() != ERROR_FILE_NOT_FOUND) ok = false;
	return ok;
}
bool DeleteUserFiles(const std::wstring& directory) {
	bool current = DeleteConfigFiles(directory, L"WeatherBar.xml");
	bool legacy = DeleteConfigFiles(directory, kLegacyConfigFile);
	return current && legacy;
}

bool DeleteIconDirectory(const std::wstring& directory) {
	DWORD attributes = GetFileAttributesW(directory.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND;
	if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
	// 只进入 icons 目录，目录联接和符号链接只删除链接，不进入其目标。
	if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return RemoveDirectoryW(directory.c_str()) != FALSE;
	WIN32_FIND_DATAW entry{};
	HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &entry);
	if (search == INVALID_HANDLE_VALUE) return false;
	bool ok = true;
	do {
		if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0) continue;
		std::wstring path = directory + L"\\" + entry.cFileName;
		bool removed = entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
			? DeleteIconDirectory(path) : DeleteFileW(path.c_str()) != FALSE;
		if (!removed) ok = false;
	} while (FindNextFileW(search, &entry));
	if (GetLastError() != ERROR_NO_MORE_FILES) ok = false;
	FindClose(search);
	return ok && RemoveDirectoryW(directory.c_str()) != FALSE;
}

std::wstring PowerShellLiteral(const std::wstring& value) {
	std::wstring escaped = L"'";
	for (wchar_t ch : value) { escaped += ch; if (ch == L'\'') escaped += ch; }
	return escaped + L"'";
}

bool StartExecutableCleanup(const std::wstring& executable, DWORD parentId, HANDLE* cleanupProcess = nullptr) {
	std::wstring directory = DirectoryOf(executable);
	wchar_t absolute[MAX_PATH]{};
	DWORD length = GetFullPathNameW(executable.c_str(), MAX_PATH, absolute, nullptr);
	// 拒绝相对路径、根目录和目录联接；根目录永远不做递归删除。
	DWORD attributes = GetFileAttributesW(directory.c_str());
	if (!length || length >= MAX_PATH || executable != absolute || directory.size() <= 3
		|| attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
	wchar_t systemDirectory[MAX_PATH]{};
	if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) return false;
	std::wstring powershell = std::wstring(systemDirectory) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
	std::wstring script = L"$ErrorActionPreference='Stop'; try { ";
	if (parentId) {
		HANDLE parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentId);
		if (parent) {
			FILETIME created{}, exited{}, kernel{}, user{};
			bool queried = GetProcessTimes(parent, &created, &exited, &kernel, &user) != FALSE;
			CloseHandle(parent);
			if (!queried) return false;
			ULARGE_INTEGER started{}; started.LowPart = created.dwLowDateTime; started.HighPart = created.dwHighDateTime;
			// PowerShell 启动期间原进程可能已经退出；核对创建时间，避免 PID 被复用后等待其他程序。
			script += L"$p=Get-Process -Id " + std::to_wstring(parentId)
				+ L" -ErrorAction SilentlyContinue; if($p -and $p.StartTime.ToUniversalTime().ToFileTimeUtc() -eq "
				+ std::to_wstring(started.QuadPart) + L"){$p.WaitForExit()}; ";
		}
		else if (GetLastError() != ERROR_INVALID_PARAMETER) return false;
	}
	// 运行中的 EXE 不能删除，交给系统自带 PowerShell 在进程退出后清理指定文件。
	// 不留下临时 EXE 或脚本；混有用户资料的安装目录只在为空时删除。
	script += L"Remove-Item -LiteralPath " + PowerShellLiteral(executable) + L" -Force; "
		L"if(-not(Get-ChildItem -LiteralPath " + PowerShellLiteral(directory) + L" -Force | Select-Object -First 1)){Remove-Item -LiteralPath "
		+ PowerShellLiteral(directory) + L" -Force} } catch { Add-Type -AssemblyName System.Windows.Forms; "
		L"[System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'WeatherBar') | Out-Null; exit 1 }";
	std::wstring command = L"\"" + powershell + L"\" -NoProfile -NonInteractive -WindowStyle Hidden -Command \"& { " + script + L" }\"";
	STARTUPINFOW startup{ sizeof(startup) };
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
	CloseHandle(process.hThread);
	if (cleanupProcess) *cleanupProcess = process.hProcess;
	else CloseHandle(process.hProcess);
	return true;
}

void RestartExplorer() {
	HWND shell = FindWindowW(L"Shell_TrayWnd", nullptr);
	DWORD pid = 0;
	if (shell)
		GetWindowThreadProcessId(shell, &pid);
	HANDLE process = pid ? OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid) : nullptr;
	if (process) {
		TerminateProcess(process, 0);
		WaitForSingleObject(process, 8000);
		CloseHandle(process);
	}
	for (int i = 0; i < 40; ++i) {
		if (FindWindowW(L"Shell_TrayWnd", nullptr))
			return;
		Sleep(250);
	}
	ShellExecuteW(nullptr, nullptr, L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
	for (int i = 0; i < 40; ++i) {
		if (FindWindowW(L"Shell_TrayWnd", nullptr))
			return;
		Sleep(250);
	}
}

bool ConfirmUninstall(Language language, bool& removeSettings) {
	auto T = [&](const wchar_t* zh, const wchar_t* en, const wchar_t* ja) { return Text(language, zh, en, ja); };
	TASKDIALOG_BUTTON buttons[] = {
		{ IDOK, T(L"卸载天气条", L"Uninstall", L"アンインストール") },
		{ IDCANCEL, T(L"取消", L"Cancel", L"キャンセル") }
	};
	TASKDIALOGCONFIG dialog{};
	dialog.cbSize = sizeof(dialog);
	dialog.dwFlags = TDF_SIZE_TO_CONTENT | TDF_ALLOW_DIALOG_CANCELLATION;
	dialog.pszWindowTitle = L"WeatherBar";
	dialog.pszMainInstruction = T(L"卸载天气条？", L"Uninstall WeatherBar?", L"WeatherBar を削除しますか？");
	dialog.pszContent = T(
		L"将关闭天气条，移除组件注册和开机启动。\n\n设置与自定义图标默认保留，可用于重新安装或手动更新。\n\n资源管理器将重新启动，任务栏会闪烁，文件夹窗口会关闭。请先保存工作。",
		L"The weather bar, component registration and startup entry will be removed.\n\nSettings and custom icons are kept for reinstalling or updating.\n\nFile Explorer will restart. The taskbar will flash and folder windows will close. Save your work first.",
		L"天気バー、コンポーネント登録、自動起動を削除します。\n\n設定とカスタムアイコンは再インストールや更新のために保持します。\n\nエクスプローラーを再起動します。タスクバーが点滅し、フォルダーのウィンドウが閉じます。作業を保存してください。");
	dialog.pszVerificationText = T(L"彻底卸载：删除设置、备份、日志、自定义图标及程序文件", L"Remove everything: settings, backups, logs, custom icons and program files", L"完全削除：設定・バックアップ・ログ・カスタムアイコン・プログラム");
	dialog.cButtons = 2;
	dialog.pButtons = buttons;
	dialog.nDefaultButton = IDCANCEL;
	int choice = IDCANCEL;
	BOOL checked = FALSE;
	// 无法显示确认界面时直接取消，不能在失败路径继续卸载。
	if (FAILED(TaskDialogIndirect(&dialog, &choice, nullptr, &checked)))
		return false;
	removeSettings = checked != FALSE;
	return choice == IDOK;
}

bool Uninstall(const std::wstring& exeDirectory, const std::wstring& dllPath) {
	bool removeSettings = false;
	if (!ConfirmUninstall(g_language, removeSettings))
		return false;
	{
		TrayBand tray;
		if (tray.band)
			tray.band->HideDeskBand(CLSID_WeatherBar);
	}
	std::wstring userClsid = std::wstring(L"Software\\Classes\\CLSID\\") + kClsid;
	bool startupRemoved = RemoveStartup();
	UnregisterDll(dllPath);
	LONG userResult = SHDeleteKeyW(HKEY_CURRENT_USER, userClsid.c_str());
	bool registrationRemoved = userResult == ERROR_SUCCESS || userResult == ERROR_FILE_NOT_FOUND;
	bool machineRemoved = RemoveMachineRegistration();
	if (!startupRemoved || !registrationRemoved || !machineRemoved) {
		ShowNotice(nullptr, g_language,
			Text(g_language, L"卸载尚未全部完成", L"Uninstall needs attention", L"アンインストール未完了"),
			Text(g_language, L"无法清除注册或启动项，文件已保留。请检查权限；若出现管理员确认，请允许后重试。", L"Registration or startup removal failed. Files were kept. Check permissions and allow any administrator prompt before retrying.", L"登録または自動起動を削除できません。ファイルは保持しています。権限と管理者の確認を確認し、再実行してください。"));
		return false;
	}
	DeleteTree(HKEY_CURRENT_USER, kToolbarCache);
	RestartExplorer();
	bool filesRemoved = DeleteFileW(dllPath.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;
	if (!DeleteFileW((exeDirectory + L"\\" + kLegacyDllFile).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) filesRemoved = false;
	if (removeSettings) {
		if (!DeleteUserFiles(exeDirectory)) filesRemoved = false;
		if (!DeleteIconDirectory(exeDirectory + L"\\icons")) filesRemoved = false;
	}
	auto T = [&](const wchar_t* zh, const wchar_t* en, const wchar_t* ja) { return Text(g_language, zh, en, ja); };
	if (!filesRemoved) {
		ShowNotice(nullptr, g_language,
			T(L"卸载尚未全部完成", L"Uninstall needs attention", L"アンインストール未完了"),
			T(L"无法删除部分文件。程序已停用，请检查目录权限或文件占用。", L"Some files could not be deleted. The program is disabled; check folder permissions and files in use.", L"一部のファイルを削除できませんでした。権限とファイルの使用状況を確認してください。"));
		return false;
	}
	if (removeSettings && !StartExecutableCleanup(ExePath(), GetCurrentProcessId())) {
		ShowNotice(nullptr, g_language, T(L"文件清理未完成", L"File cleanup incomplete", L"ファイル削除未完了"),
			T(L"程序已停用。关闭此窗口后，请手动删除 EXE。", L"The program is disabled. Close this window and delete the EXE manually.", L"プログラムは無効です。画面を閉じて EXE を手動で削除してください。"));
		return false;
	}
	ShowNotice(nullptr, g_language,
		T(L"天气条已卸载", L"WeatherBar was uninstalled", L"アンインストールが完了しました"),
		removeSettings
			? T(L"设置、备份、日志和图标已删除。关闭此窗口后清理 EXE，目录为空时一并删除。\n\n同目录中与本软件无关的文件会保留。", L"Settings, backups, logs and icons were deleted. Closing this window removes the EXE and the folder if empty.\n\nUnrelated files in the folder are kept.", L"設定・バックアップ・ログ・アイコンを削除しました。画面を閉じると EXE と空のフォルダーを削除します。無関係なファイルは保持します。")
			: T(L"设置和图标已保留。\n\n手动更新：替换同目录的 EXE，再双击安装。新程序会更新 DLL，并补齐缺少的配置和图标。", L"Settings and icons were kept.\n\nTo update manually, replace the EXE in this folder and run it again. The new program updates the DLL and adds missing settings and icons.", L"設定とアイコンを保持しました。\n\n手動更新するには、このフォルダーの EXE を置き換えて再実行してください。DLL を更新し、不足する設定とアイコンを追加します。"));
	return true;
}
bool WriteIfChanged(const std::wstring& path, const void* data, DWORD size, bool preserveExisting = false) {
	DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes != INVALID_FILE_ATTRIBUTES) {
		if (attributes & FILE_ATTRIBUTE_DIRECTORY)
			return false;
		if (preserveExisting)
			return true;
	}
	HANDLE existing = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (existing != INVALID_HANDLE_VALUE) {
		bool same = GetFileSize(existing, nullptr) == size;
		if (same && size > 0) {
			std::vector<BYTE> current(size);
			DWORD read = 0;
			same = ReadFile(existing, current.data(), size, &read, nullptr) && read == size && memcmp(current.data(), data, size) == 0;
		}
		CloseHandle(existing);
		if (same) return true;
	}
	if (preserveExisting) {
		// CREATE_NEW 避免并发安装在检查文件后覆盖刚出现的用户图标。
		HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (output == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_EXISTS;
		DWORD written = 0;
		bool ok = WriteFile(output, data, size, &written, nullptr) && written == size;
		if (!CloseHandle(output)) ok = false;
		if (!ok) DeleteFileW(path.c_str());
		return ok;
	}
	if (attributes != INVALID_FILE_ATTRIBUTES) {
		// 已加载的 DLL 不能替换；仅提示用户先卸载，绝不强行释放或结束进程。
		HANDLE probe = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (probe == INVALID_HANDLE_VALUE) return false;
		CloseHandle(probe);
	}
	std::wstring directory = DirectoryOf(path);
	wchar_t temporary[MAX_PATH]{};
	if (!GetTempFileNameW(directory.empty() ? L"." : directory.c_str(), L"wdb", 0, temporary))
		return false;
	HANDLE output = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	bool ok = output != INVALID_HANDLE_VALUE;
	if (ok) {
		DWORD written = 0;
		ok = WriteFile(output, data, size, &written, nullptr) && written == size;
		if (ok) ok = FlushFileBuffers(output) != FALSE;
		if (!CloseHandle(output)) ok = false;
	}
	if (ok) ok = MoveFileExW(temporary, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	if (!ok) DeleteFileW(temporary);
	return ok;
}
bool WriteResource(HMODULE module, LPCWSTR type, LPCWSTR name, const std::wstring& path, bool preserveExisting = false) {
	HRSRC found = FindResourceW(module, name, type);
	if (!found)
		return false;
	HGLOBAL loaded = LoadResource(module, found);
	DWORD size = SizeofResource(module, found);
	const void* data = loaded ? LockResource(loaded) : nullptr;
	if (!data && size > 0)
		return false;
	return WriteIfChanged(path, data, size, preserveExisting);
}

struct ExtractState {
	std::wstring directory;
	bool ok = true;
};

BOOL CALLBACK ExtractNamedFile(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param) {
	auto* state = reinterpret_cast<ExtractState*>(param);
	if (IS_INTRESOURCE(name))
		return TRUE;
	std::wstring path = state->directory;
	if (lstrcmpiW(type, L"SVG") != 0)
		return TRUE;
	path += L"\\icons\\";
	path += name;
	path += L".svg";
	if (!WriteResource(module, type, name, path, true))
		state->ok = false;
	return state->ok ? TRUE : FALSE;
}

bool ExtractPayload(const std::wstring& directory, bool silent) {
	std::wstring legacyDll = directory + L"\\" + kLegacyDllFile;
	if (GetFileAttributesW(legacyDll.c_str()) != INVALID_FILE_ATTRIBUTES) {
		HANDLE probe = CreateFileW(legacyDll.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (probe == INVALID_HANDLE_VALUE) {
			if (!silent) ShowNotice(nullptr, g_language,
				Text(g_language, L"请先卸载旧版天气条", L"Uninstall the previous version first", L"以前のバージョンを先に削除してください"),
				Text(g_language, L"旧 DLL 仍在使用。请用旧 EXE 卸载并保留配置，待资源管理器重启后再安装 WeatherBar。", L"The old DLL is in use. Uninstall with the old EXE while keeping settings, wait for Explorer to restart, then install WeatherBar.", L"以前の DLL が使用中です。以前の EXE で設定を保持して削除し、エクスプローラーの再起動後に WeatherBar をインストールしてください。"));
			return false;
		}
		CloseHandle(probe);
	}
	HMODULE module = GetModuleHandleW(nullptr);
	if (!FindResourceW(module, MAKEINTRESOURCEW(101), RT_RCDATA))
		return GetFileAttributesW((directory + L"\\WeatherBar.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
	CreateDirectoryW((directory + L"\\icons").c_str(), nullptr);
	if (!WriteResource(module, RT_RCDATA, MAKEINTRESOURCEW(101), directory + L"\\WeatherBar.dll")) {
		if (!silent)
			ShowNotice(nullptr, g_language, Text(g_language, L"无法更新插件文件", L"Could not update the plugin", L"プラグインを更新できませんでした"), Text(g_language, L"DLL 可能正在使用。请先卸载天气条并保留设置，待资源管理器重启完成后再安装。", L"The DLL may be in use. Uninstall the bar while keeping settings, wait for File Explorer to restart, then install again.", L"DLL が使用中の可能性があります。設定を保持してアンインストールし、エクスプローラーの再起動後に再インストールしてください。"));
		return false;
	}
	ExtractState state{ directory, true };
	if (!EnumResourceNamesW(module, L"SVG", ExtractNamedFile, reinterpret_cast<LONG_PTR>(&state)))
		state.ok = false;
	if (!state.ok && !silent)
		ShowNotice(nullptr, g_language, Text(g_language, L"无法补齐图标", L"Could not add missing icons", L"不足するアイコンを追加できませんでした"), Text(g_language, L"请检查 icons 目录权限。已有图标不会被覆盖。", L"Check permissions on the icons folder. Existing icons were not overwritten.", L"icons フォルダーの権限を確認してください。既存のアイコンは上書きしていません。"));
	return state.ok;
}

bool RequestBandDisplay() {
	TrayBand tray;
	if (!tray.band)
		return false;
	DeleteTree(HKEY_CURRENT_USER, kToolbarCache);
	for (int attempt = 0; attempt < 8; ++attempt) {
		tray.band->DeskBandRegistrationChanged();
		if (SUCCEEDED(tray.band->ShowDeskBand(CLSID_WeatherBar)))
			return true;
		Sleep(200);
	}
	return false;
}

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
	g_hinstDll = GetModuleHandleW(nullptr);
	if (HasArgument(L"/uninstall-machine"))
		return DeleteMachineRegistration() ? 0 : 1;
	// 安装器独立跟随系统，不受设置中的手动语言选择影响。
	g_language = ResolveLanguage(Language::System);
	std::wstring exe = ExePath();
	std::wstring directory = DirectoryOf(exe);
	bool silent = HasArgument(L"/silent");
	std::wstring dll = directory + L"\\WeatherBar.dll";
	bool dllExists = GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES;
	bool uninstalling = HasArgument(L"/uninstall");
	if (!uninstalling && !silent && dllExists) {
		TrayBand tray;
		uninstalling = tray.Shown();
	}
	if (uninstalling)
		return Uninstall(directory, dll) ? 0 : 1;
	if (!ExtractPayload(directory, silent))
		return 1;
	auto T = [&](const wchar_t* zh, const wchar_t* en, const wchar_t* ja) { return Text(g_language, zh, en, ja); };
	if (!RegisterBand(dll)) {
		if (!silent) ShowNotice(nullptr, g_language,
			T(L"无法安装天气条", L"Could not install WeatherBar", L"インストールできませんでした"),
			T(L"无法加载 DLL 或写入组件注册。请确认程序目录可写，并使用 Release x64 版本。", L"The DLL could not be loaded or registered. Check folder permissions and use the Release x64 build.", L"DLL を読み込み、または登録できませんでした。フォルダーの権限と Release x64 版であることを確認してください。"));
		return 1;
	}
	if (!EnableStartup(exe)) {
		if (!silent) ShowNotice(nullptr, g_language,
			T(L"组件已注册，但开机启动未设置", L"Registered, but startup was not enabled", L"登録済みですが、自動起動を設定できませんでした"),
			T(L"无法写入当前用户的启动项。请检查权限后重试。配置和图标已保留。", L"The current user's startup entry could not be written. Check permissions and retry. Settings and icons were kept.", L"現在のユーザーの自動起動項目を書き込めませんでした。権限を確認して再実行してください。設定とアイコンは保持されています。"));
		return 1;
	}
	DeleteTree(HKEY_CURRENT_USER, kToolbarCache);
	bool displayRequested = RequestBandDisplay();
	// Shell 会自行显示工具栏确认框；请求成功不表示用户已经同意，不能提前报成功。
	if (!silent && !displayRequested) ShowNotice(nullptr, g_language,
		T(L"已安装，等待显示到任务栏", L"Installed — taskbar setup needed", L"インストール済み・タスクバーの設定が必要です"),
		T(L"请在传统任务栏右键菜单的“工具栏”中勾选 WeatherBar。", L"Open Toolbars in the classic taskbar context menu and select WeatherBar.", L"従来型タスクバーの右クリックメニューの「ツールバー」で WeatherBar を選択してください。"));
	return 0;
}
