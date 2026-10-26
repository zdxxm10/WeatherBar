#pragma once
#include <atomic>
#include <memory>
#include <string>

enum class UpdateStatus { Checking, Available, Current, NoRelease, Failed };

struct UpdateCheck {
	std::atomic<bool> done{ false };
	UpdateStatus status = UpdateStatus::Checking;
	std::wstring latestVersion;
	unsigned long errorCode = 0;
	unsigned httpStatus = 0;
	bool usedReleasePage = false;
};

bool ParseReleaseVersion(const std::wstring& text, int (&parts)[3]);
UpdateStatus ClassifyRelease(unsigned statusCode, const std::string& body, std::wstring& latest);
UpdateStatus ClassifyReleaseRedirect(const std::wstring& location, std::wstring& latest);
std::shared_ptr<UpdateCheck> StartUpdateCheck();
