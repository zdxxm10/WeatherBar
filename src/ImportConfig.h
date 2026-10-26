#pragma once
#include <windows.h>
#include <string>
#include <utility>
#include <vector>
#include "AppText.h"

extern HINSTANCE g_hinstDll;

constexpr int kTimezoneMin = -12;
constexpr int kTimezoneMax = 14;
constexpr int kDelayMin = 0;
constexpr int kDelayMax = 30 * 60;
constexpr int kDefaultDelaySeconds = 15 * 60;
constexpr int kIconSizeMin = 16;
constexpr int kConfigSchemaVersion = 2;

enum class SizeLevel {
	Small,
	Medium,
	Large
};

inline int SizePercent(SizeLevel level) {
	if (level == SizeLevel::Small)
		return 60;
	if (level == SizeLevel::Large)
		return 100;
	return 80;
}

inline int LevelPixels(SizeLevel level, int full) {
	if (full < 1)
		full = 1;
	int px = MulDiv(full, SizePercent(level), 100);
	return px < 1 ? 1 : px;
}

inline int GapPercent(SizeLevel level) {
	if (level == SizeLevel::Small)
		return 8;
	if (level == SizeLevel::Large)
		return 28;
	return 16;
}

struct BandConfig {
	Language language = Language::System;
	int schemaVersion = kConfigSchemaVersion;
	bool canSave = true;
	std::wstring url;
	int timezoneOffsetHours = 8;
	std::vector<int> scheduleHours;
	int delaySeconds = kDefaultDelaySeconds;
	bool showIcon = true;
	bool tooltipEnabled = true;
	// 位 0 为今日高低温，随后依次为紫外线、风向、风速、湿度、体感、降水概率。
	unsigned tooltipFields = 0;
	SizeLevel iconLevel = SizeLevel::Medium;
	SizeLevel currentLevel = SizeLevel::Large;
	COLORREF currentColor = RGB(255, 255, 255);
	bool showMax = true;
	SizeLevel maxLevel = SizeLevel::Medium;
	COLORREF maxColor = RGB(255, 0, 0);
	bool showMin = true;
	SizeLevel minLevel = SizeLevel::Medium;
	COLORREF minColor = RGB(0, 178, 238);
	SizeLevel gapIconLevel = SizeLevel::Medium;
	SizeLevel gapExtremeLevel = SizeLevel::Medium;
	std::vector<std::pair<int, std::wstring>> icons;
};

std::wstring ModuleDirectory();
std::wstring ConfigFilePath();
BandConfig DefaultBandConfig();
void NormalizeBandConfig(BandConfig& config);
bool CanonicalForecastUrl(const std::wstring& input, std::wstring& url, std::wstring& error);
BandConfig LoadBandConfig();
bool SaveBandConfig(const BandConfig& config);

std::wstring FormatScheduleHours(const std::vector<int>& hours);
bool ParseColorText(const std::wstring& text, COLORREF& color);
std::wstring FormatColorText(COLORREF color);
COLORREF TaskbarContrastColor();
int ClampInt(int value, int low, int high);
