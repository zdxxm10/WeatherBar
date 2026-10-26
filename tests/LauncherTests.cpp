#include "TestSupport.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>

// 不调用真正的安装入口；只验证文件策略和预览确认窗口。
#define wWinMain LauncherEntryForTests
#include "../launcher/Launcher.cpp"
#undef wWinMain

std::string ReadBytes(const std::wstring& path) {
	std::ifstream input(std::filesystem::path(path), std::ios::binary);
	return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

bool CreateTestJunction(const std::wstring& link, const std::wstring& target) {
	wchar_t systemDirectory[MAX_PATH]{};
	if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) return false;
	std::wstring shell = std::wstring(systemDirectory) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
	std::wstring command = L"\"" + shell + L"\" -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path "
		+ PowerShellLiteral(link) + L" -Target " + PowerShellLiteral(target) + L" | Out-Null\"";
	STARTUPINFOW startup{ sizeof(startup) }; PROCESS_INFORMATION process{};
	if (!CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
	CloseHandle(process.hThread);
	bool finished = WaitForSingleObject(process.hProcess, 20000) == WAIT_OBJECT_0;
	DWORD result = 1; GetExitCodeProcess(process.hProcess, &result); CloseHandle(process.hProcess);
	return finished && result == 0;
}

int wmain(int argc, wchar_t** argv) {
	g_hinstDll = GetModuleHandleW(nullptr);
	if (argc > 1 && std::wstring(argv[1]) == L"--cleanup-parent") { Sleep(1500); return 0; }
	g_language = argc > 2 ? ParseLanguage(argv[2]) : Language::Chinese;
	if (argc > 1 && std::wstring(argv[1]) == L"--preview-uninstall") {
		bool remove = false;
		ConfirmUninstall(g_language, remove);
		return 0;
	}
	wchar_t temporary[MAX_PATH]{};
	if (!GetTempFileNameW(ModuleDirectory().c_str(), L"wtt", 0, temporary)) return 2;
	DeleteFileW(temporary);
	if (!CreateDirectoryW(temporary, nullptr)) return 2;
	std::wstring directory = temporary;
	int failures = 0;
	try {
		std::wstring icon = directory + L"\\custom.svg";
		Require(WriteIfChanged(icon, "custom", 6, true), "create missing icon");
		Require(WriteIfChanged(icon, "default", 7, true) && ReadBytes(icon) == "custom", "existing custom icon must be preserved");
		Require(!WriteIfChanged(directory, "x", 1, true), "directories cannot be treated as icons");
		std::wstring dll = directory + L"\\test.dll";
		Require(WriteIfChanged(dll, "old DLL", 7), "prepare DLL fixture");
		HANDLE lock = CreateFileW(dll.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		Require(lock != INVALID_HANDLE_VALUE, "lock DLL fixture");
		bool replaced = WriteIfChanged(dll, "new DLL", 7);
		CloseHandle(lock);
		Require(!replaced && ReadBytes(dll) == "old DLL", "locked DLL must be kept intact");
		Require(WriteIfChanged(dll, "new DLL", 7) && ReadBytes(dll) == "new DLL", "DLL replaces after lock is released");
		std::filesystem::path repository = std::filesystem::path(ModuleDirectory()).parent_path().parent_path().parent_path();
		HMODULE package = LoadLibraryExW((repository / L"x64" / L"Release" / L"WeatherBar.exe").c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
		Require(package != nullptr, "load real package resources");
		std::wstring loadedPath = directory + L"\\loaded.dll";
		bool extracted = WriteResource(package, RT_RCDATA, MAKEINTRESOURCEW(101), loadedPath);
		FreeLibrary(package);
		Require(extracted, "extract real DLL into isolated fixture");
		#ifdef _WIN64
		HMODULE loaded = LoadLibraryW(loadedPath.c_str());
		Require(loaded != nullptr, "load plugin without registering it");
		bool replacedLoaded = WriteIfChanged(loadedPath, "new DLL", 7);
		FreeLibrary(loaded);
		Require(!replacedLoaded, "mapped DLL must not be replaced while loaded");
		Require(WriteIfChanged(loadedPath, "new DLL", 7), "mapped DLL replaces after unloading");
		#endif
		for (const wchar_t* name : { L"WeatherBar.xml", L"WeatherBar.xml.before-v2.bak", L"log.txt" })
			Require(WriteIfChanged(directory + L"\\" + name, "fixture", 7), "prepare disposable config fixture");
		Require(WriteIfChanged(directory + L"\\WeatherBar.xml.before-v1.bak", "old", 3), "prepare older migration backup");
		Require(WriteIfChanged(directory + L"\\WeatherBar.xml.before-vpersonal.bak", "keep", 4), "prepare unrelated backup");
		Require(WriteIfChanged(directory + L"\\" + kLegacyConfigFile, "old", 3), "prepare old-brand configuration");
		Require(DeleteUserFiles(directory), "explicit configuration removal");
		Require(GetFileAttributesW((directory + L"\\" + kLegacyConfigFile).c_str()) == INVALID_FILE_ATTRIBUTES, "full uninstall also removes legacy configuration");
		Require(GetFileAttributesW((directory + L"\\WeatherBar.xml.before-v1.bak").c_str()) == INVALID_FILE_ATTRIBUTES, "all numbered migration backups removed");
		Require(ReadBytes(directory + L"\\WeatherBar.xml.before-vpersonal.bak") == "keep", "unrelated backup retained");
		Require(ReadBytes(icon) == "custom", "configuration removal must not delete custom icons");
		std::wstring icons = directory + L"\\icons";
		Require(CreateDirectoryW(icons.c_str(), nullptr) != FALSE, "create owned icon directory");
		Require(WriteIfChanged(icons + L"\\custom.svg", "custom", 6), "prepare custom icon for full uninstall");
		Require(DeleteIconDirectory(icons) && GetFileAttributesW(icons.c_str()) == INVALID_FILE_ATTRIBUTES, "full uninstall removes icon folder");
		Require(ReadBytes(icon) == "custom", "full uninstall does not delete unrelated sibling files");
		std::wstring outside = directory + L"\\outside";
		Require(CreateDirectoryW(outside.c_str(), nullptr) != FALSE, "create junction target");
		Require(WriteIfChanged(outside + L"\\personal.txt", "keep", 4), "create file outside icons");
		Require(CreateTestJunction(icons, outside), "create root icons junction");
		Require(DeleteIconDirectory(icons) && ReadBytes(outside + L"\\personal.txt") == "keep", "root junction removal leaves target intact");
		Require(CreateDirectoryW(icons.c_str(), nullptr) != FALSE, "create nested junction fixture");
		Require(CreateTestJunction(icons + L"\\linked", outside), "create nested icons junction");
		Require(DeleteIconDirectory(icons) && ReadBytes(outside + L"\\personal.txt") == "keep", "nested junction removal leaves target intact");
		DeleteFileW((outside + L"\\personal.txt").c_str()); RemoveDirectoryW(outside.c_str());
		Require(!StartExecutableCleanup(L"relative.exe", 0), "cleanup rejects relative paths");
		Require(PowerShellLiteral(L"a'b$()[]") == L"'a''b$()[]'", "cleanup path quoted as a literal");
		std::wstring cleanupDir = directory + L"\\清理 ' $()[]";
		Require(CreateDirectoryW(cleanupDir.c_str(), nullptr) != FALSE, "create Unicode cleanup fixture");
		std::wstring executable = cleanupDir + L"\\WeatherBar.exe";
		Require(CopyFileW(ExePath().c_str(), executable.c_str(), TRUE) != FALSE, "copy disposable executable");
		std::wstring command = L"\"" + executable + L"\" --cleanup-parent";
		STARTUPINFOW startup{ sizeof(startup) }; PROCESS_INFORMATION parent{};
		Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &parent) != FALSE, "start disposable parent");
		CloseHandle(parent.hThread);
		Require(WriteIfChanged(cleanupDir + L"\\personal.txt", "keep", 4), "prepare unrelated file");
		HANDLE cleanup = nullptr;
		Require(StartExecutableCleanup(executable, parent.dwProcessId, &cleanup), "start after-exit cleanup");
		Require(GetFileAttributesW(executable.c_str()) != INVALID_FILE_ATTRIBUTES, "EXE remains while parent is running");
		Require(WaitForSingleObject(cleanup, 60000) == WAIT_OBJECT_0, "cleanup process finishes within one minute");
		DWORD result = 1, parentResult = 1; GetExitCodeProcess(cleanup, &result); GetExitCodeProcess(parent.hProcess, &parentResult); CloseHandle(cleanup); CloseHandle(parent.hProcess);
		if (result != 0) {
			throw std::runtime_error("cleanup exit=" + std::to_string(result) + " parent exit=" + std::to_string(parentResult));
		}
		Require(result == 0 && GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES, "EXE deleted after parent exit");
		Require(ReadBytes(cleanupDir + L"\\personal.txt") == "keep", "unrelated file survives EXE cleanup");
		DeleteFileW((cleanupDir + L"\\personal.txt").c_str());
		Require(WriteIfChanged(executable, "fixture", 7), "prepare empty-folder cleanup fixture");
		Require(StartExecutableCleanup(executable, 0, &cleanup), "start empty-folder cleanup");
		Require(WaitForSingleObject(cleanup, 60000) == WAIT_OBJECT_0, "empty-folder cleanup finishes within one minute");
		GetExitCodeProcess(cleanup, &result); CloseHandle(cleanup);
		Require(result == 0 && GetFileAttributesW(cleanupDir.c_str()) == INVALID_FILE_ATTRIBUTES, "empty install folder removed");
		std::cout << "PASS icon preservation, locked DLL, backup cleanup, full icon removal, after-exit EXE removal and unrelated-file protection\n";
	}
	catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << error.what() << '\n'; }
	// 只清理本测试通过 GetTempFileName 创建的独立目录。
	for (const wchar_t* name : { L"custom.svg", L"test.dll", L"loaded.dll", L"WeatherBar.xml", L"WeatherBar.xml.before-v1.bak", L"WeatherBar.xml.before-v2.bak", L"WeatherBar.xml.before-vpersonal.bak", L"log.txt" })
		DeleteFileW((directory + L"\\" + name).c_str());
	RemoveDirectoryW(directory.c_str());
	return failures ? 1 : 0;
}
