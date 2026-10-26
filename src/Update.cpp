#include "pch.h"
#include "Update.h"
#include "AppText.h"
#include "ModuleReference.h"
#include <winhttp.h>
#include <thread>
#include <stdexcept>
#include <cwctype>
#include <system_error>

#pragma comment(lib, "winhttp.lib")

bool ParseReleaseVersion(const std::wstring& text, int (&parts)[3]) {
	size_t index = !text.empty() && text[0] == L'v' ? 1 : 0;
	for (int part = 0; part < 3; ++part) {
		if (index >= text.size() || text[index] < L'0' || text[index] > L'9')
			return false;
		int value = 0;
		while (index < text.size() && text[index] >= L'0' && text[index] <= L'9') {
			if (value > 100000)
				return false;
			value = value * 10 + (text[index++] - L'0');
		}
		parts[part] = value;
		// 1.0 与 1.0.0 表示同一正式版本；仅省略补丁号，主次版本必须齐全。
		if (part == 1 && index == text.size()) {
			parts[2] = 0;
			return true;
		}
		if (part < 2 && (index >= text.size() || text[index++] != L'.'))
			return false;
	}
	return index == text.size();
}

UpdateStatus ClassifyRelease(unsigned statusCode, const std::string& body, std::wstring& latest) {
	if (statusCode == 404)
		return UpdateStatus::NoRelease;
	if (statusCode != 200)
		return UpdateStatus::Failed;
	// 仅接受正式发布使用的 vX.Y 或 vX.Y.Z 标签；不执行响应中的 URL 或其他内容。
	size_t key = body.find("\"tag_name\"");
	size_t colon = key == std::string::npos ? key : body.find(':', key + 10);
	if (colon == std::string::npos)
		return UpdateStatus::Failed;
	size_t start = body.find_first_not_of(" \t\r\n", colon + 1);
	if (start == std::string::npos || body[start] != '"')
		return UpdateStatus::Failed;
	size_t end = body.find('"', ++start);
	if (end == std::string::npos || end - start > 32)
		return UpdateStatus::Failed;
	std::string tag = body.substr(start, end - start);
	latest.assign(tag.begin(), tag.end());
	int remote[3]{}, local[3]{};
	if (!ParseReleaseVersion(latest, remote) || !ParseReleaseVersion(kAppVersion, local))
		return UpdateStatus::Failed;
	for (int i = 0; i < 3; ++i) {
		if (remote[i] != local[i])
			return remote[i] > local[i] ? UpdateStatus::Available : UpdateStatus::Current;
	}
	return UpdateStatus::Current;
}

namespace {
struct InternetHandle {
	HINTERNET value = nullptr;
	~InternetHandle() { if (value) WinHttpCloseHandle(value); }
	InternetHandle() = default;
	InternetHandle(const InternetHandle&) = delete;
	InternetHandle& operator=(const InternetHandle&) = delete;
};

void FetchReleasePage(UpdateCheck& result) {
	InternetHandle session, connection, request;
	std::wstring userAgent = std::wstring(L"WeatherBar/") + kAppVersion;
	session.value = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
	if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000))
		throw std::system_error(GetLastError(), std::system_category());
	connection.value = WinHttpConnect(session.value, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (connection.value) request.value = WinHttpOpenRequest(connection.value, L"HEAD", L"/zdxxm10/WeatherBar/releases/latest", nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
	DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
	// 只读取固定官方仓库的重定向，不跟随响应中的任意地址，也不下载程序。
	if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))
		|| !WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
		throw std::system_error(GetLastError(), std::system_category());
	DWORD status = 0, size = sizeof(status);
	if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr))
		throw std::system_error(GetLastError(), std::system_category());
	if (status != 302 && status != 301 && status != 307 && status != 308) {
		result.httpStatus = status;
		return;
	}
	wchar_t location[512]{};
	size = sizeof(location);
	if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, nullptr, location, &size, nullptr))
		throw std::system_error(GetLastError(), std::system_category());
	std::wstring latest;
	UpdateStatus classified = ClassifyReleaseRedirect(location, latest);
	if (classified != UpdateStatus::Failed) {
		result.latestVersion = latest;
		result.status = classified;
		result.usedReleasePage = true;
		result.errorCode = 0;
	}
}

void FetchRelease(UpdateCheck& result) {
	InternetHandle session, connection, request;
	std::wstring userAgent = std::wstring(L"WeatherBar/") + kAppVersion;
	session.value = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
	if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000))
		throw std::system_error(GetLastError(), std::system_category(), "update session");
	connection.value = WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (connection.value)
		request.value = WinHttpOpenRequest(connection.value, L"GET", L"/repos/zdxxm10/WeatherBar/releases/latest", nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
	constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\n";
	if (!request.value || !WinHttpSendRequest(request.value, headers, static_cast<DWORD>(_countof(headers) - 1), nullptr, 0, 0, 0)
		|| !WinHttpReceiveResponse(request.value, nullptr))
		throw std::system_error(GetLastError(), std::system_category(), "update request");
	DWORD status = 0, bytes = sizeof(status);
	if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &bytes, nullptr))
		throw std::system_error(GetLastError(), std::system_category(), "update status");
	result.httpStatus = status;
	std::string body;
	if (status == 200) {
		char buffer[4096];
		DWORD read = 0;
		ULONGLONG deadline = GetTickCount64() + 15000;
		for (;;) {
			if (GetTickCount64() > deadline)
				throw std::system_error(ERROR_WINHTTP_TIMEOUT, std::system_category(), "update deadline");
			if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read))
				throw std::system_error(GetLastError(), std::system_category(), "update read");
			if (!read) break;
			if (body.size() + read > 256 * 1024)
				throw std::runtime_error("update response too large");
			body.append(buffer, read);
		}
	}
	result.status = ClassifyRelease(status, body, result.latestVersion);
}
}

UpdateStatus ClassifyReleaseRedirect(const std::wstring& location, std::wstring& latest) {
	const std::wstring prefix = L"https://github.com/zdxxm10/WeatherBar/releases/tag/";
	if (location.rfind(prefix, 0) != 0) return UpdateStatus::Failed;
	std::wstring tag = location.substr(prefix.size());
	int remote[3]{}, local[3]{};
	if (!ParseReleaseVersion(tag, remote) || !ParseReleaseVersion(kAppVersion, local)) return UpdateStatus::Failed;
	latest = tag;
	for (int part = 0; part < 3; ++part)
		if (remote[part] != local[part]) return remote[part] > local[part] ? UpdateStatus::Available : UpdateStatus::Current;
	return UpdateStatus::Current;
}

std::shared_ptr<UpdateCheck> StartUpdateCheck() {
	auto result = std::make_shared<UpdateCheck>();
	auto module = std::make_unique<ModuleReference>();
	std::thread([module = std::move(module), result] {
		try { FetchRelease(*result); }
		catch (const std::system_error& error) { result->errorCode = static_cast<unsigned long>(error.code().value()); result->status = UpdateStatus::Failed; }
		catch (...) { result->status = UpdateStatus::Failed; }
		if (result->status == UpdateStatus::Failed) {
			try { FetchReleasePage(*result); }
			catch (const std::system_error& error) { result->errorCode = static_cast<unsigned long>(error.code().value()); }
			catch (...) { result->status = UpdateStatus::Failed; }
		}
		// UI 只在 done 的 acquire 读取之后访问结果；窗口关闭无需等待网络线程。
		result->done.store(true, std::memory_order_release);
	}).detach();
	return result;
}
