#include "pch.h"
#include "Settings.h"
#include "DeskBand.h"
#include "Update.h"
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
enum ControlId {
	IDC_URL = 100, IDC_TZ, IDC_LANGUAGE,
	IDC_SHOW_ICON, IDC_ICON_SIZE, IDC_GAP_ICON, IDC_GAP_EXT,
	IDC_CUR_SIZE, IDC_CUR_COLOR, IDC_SHOW_MAX, IDC_MAX_SIZE, IDC_MAX_COLOR,
	IDC_SHOW_MIN, IDC_MIN_SIZE, IDC_MIN_COLOR,
	IDC_SAVE, IDC_CANCEL, IDC_RESET, IDC_CHECK_UPDATE, IDC_RELEASES, IDC_UPDATE_STATUS,
	IDC_TOOLTIP = 140, IDC_TOOLTIP_FIRST, IDC_TOOLTIP_LAST = IDC_TOOLTIP_FIRST + 6
};
constexpr UINT_PTR kUpdateTimer = 1;
constexpr UINT_PTR kPreviewTimer = 2;
constexpr UINT kColorChanged = WM_APP + 20;
constexpr int kClientWidth = 520;
constexpr int kContentHeight = 822;

struct SettingsState {
	CDeskBand* band = nullptr;
	BandConfig draft;
	Language language = Language::English;
	HFONT font = nullptr;
	HFONT headingFont = nullptr;
	HFONT addressFont = nullptr;
	HWND palette = nullptr;
	bool initializing = true;
	struct LocalText { const wchar_t* zh; const wchar_t* en; const wchar_t* ja; };
	struct Label { HWND control; LocalText text; };
	std::vector<Label> labels;
	HBRUSH panelBrush = nullptr;
	SIZE minimumSize{};
	struct Placement { HWND control; RECT rect; bool stretch; bool right; bool bottom; };
	std::vector<Placement> placements;
	int dpi = 96;
	int scroll = 0;
	std::shared_ptr<UpdateCheck> update;
};

int Scale(const SettingsState* state, int value) {
	return MulDiv(value, state->dpi, 96);
}

std::wstring ControlText(HWND parent, int id) {
	HWND control = GetDlgItem(parent, id);
	int length = GetWindowTextLengthW(control);
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	int copied = GetWindowTextW(control, text.data(), length + 1);
	text.resize(copied > 0 ? copied : 0);
	return text;
}

void SetControlText(HWND parent, int id, const std::wstring& text) {
	SetWindowTextW(GetDlgItem(parent, id), text.c_str());
}

LRESULT CALLBACK SettingsChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == WM_SETFOCUS) {
		HWND parent = GetParent(hwnd);
		RECT field{}, client{};
		GetWindowRect(hwnd, &field);
		MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&field), 2);
		GetClientRect(parent, &client);
		int offset = field.top < 0 ? field.top - 8 : field.bottom > client.bottom ? field.bottom - client.bottom + 8 : 0;
		if (offset) {
			int position = GetScrollPos(parent, SB_VERT) + offset;
			SendMessageW(parent, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, position > 0 ? position : 0), 0);
		}
	}
	if (msg == WM_GETDLGCODE)
		return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTTAB;
	if (msg == WM_KEYDOWN && (wp == VK_ESCAPE || wp == VK_RETURN)) {
		if (wp == VK_RETURN && GetDlgCtrlID(hwnd) == IDC_URL)
			return DefSubclassProc(hwnd, msg, wp, lp);
		wchar_t klass[32]{};
		GetClassNameW(hwnd, klass, 32);
		if (wp == VK_RETURN && lstrcmpiW(klass, L"Button") == 0) {
			SendMessageW(hwnd, BM_CLICK, 0, 0);
			return 0;
		}
		if (lstrcmpiW(klass, L"ComboBox") == 0 && SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0))
			return DefSubclassProc(hwnd, msg, wp, lp);
		SendMessageW(GetParent(hwnd), wp == VK_ESCAPE ? WM_CLOSE : WM_COMMAND, IDC_SAVE, 0);
		return 0;
	}
	if (msg == WM_KEYDOWN && wp == VK_TAB) {
		HWND next = GetNextDlgTabItem(GetParent(hwnd), hwnd, GetKeyState(VK_SHIFT) < 0);
		if (next) SetFocus(next);
		return 0;
	}
	return DefSubclassProc(hwnd, msg, wp, lp);
}

HWND AddControl(HWND parent, const wchar_t* klass, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height, HFONT font) {
	HWND control = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent, (HMENU)(INT_PTR)id, g_hinstDll, nullptr);
	if (control) {
		SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
		if (style & WS_TABSTOP) SetWindowSubclass(control, SettingsChildProc, 1, 0);
	}
	return control;
}

SizeLevel ReadLevel(HWND hwnd, int id) {
	LRESULT selected = SendDlgItemMessageW(hwnd, id, CB_GETCURSEL, 0, 0);
	return selected == 0 ? SizeLevel::Small : selected == 2 ? SizeLevel::Large : SizeLevel::Medium;
}

void ReadAppearance(HWND hwnd, BandConfig& draft) {
	LRESULT selected = SendDlgItemMessageW(hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
	draft.language = selected >= 0 && selected <= 3 ? static_cast<Language>(selected) : Language::System;
	draft.showIcon = Button_GetCheck(GetDlgItem(hwnd, IDC_SHOW_ICON)) == BST_CHECKED;
	draft.showMax = Button_GetCheck(GetDlgItem(hwnd, IDC_SHOW_MAX)) == BST_CHECKED;
	draft.showMin = Button_GetCheck(GetDlgItem(hwnd, IDC_SHOW_MIN)) == BST_CHECKED;
	draft.tooltipEnabled = Button_GetCheck(GetDlgItem(hwnd, IDC_TOOLTIP)) == BST_CHECKED;
	draft.tooltipFields = 0;
	for (int field = 0; field < 7; ++field) {
		HWND check = GetDlgItem(hwnd, IDC_TOOLTIP_FIRST + field);
		if (Button_GetCheck(check) == BST_CHECKED) draft.tooltipFields |= 1u << field;
		EnableWindow(check, draft.tooltipEnabled);
	}
	draft.iconLevel = ReadLevel(hwnd, IDC_ICON_SIZE);
	draft.currentLevel = ReadLevel(hwnd, IDC_CUR_SIZE);
	draft.maxLevel = ReadLevel(hwnd, IDC_MAX_SIZE);
	draft.minLevel = ReadLevel(hwnd, IDC_MIN_SIZE);
	draft.gapIconLevel = ReadLevel(hwnd, IDC_GAP_ICON);
	draft.gapExtremeLevel = ReadLevel(hwnd, IDC_GAP_EXT);
	draft.currentColor = static_cast<COLORREF>(GetWindowLongPtrW(GetDlgItem(hwnd, IDC_CUR_COLOR), GWLP_USERDATA));
	draft.maxColor = static_cast<COLORREF>(GetWindowLongPtrW(GetDlgItem(hwnd, IDC_MAX_COLOR), GWLP_USERDATA));
	draft.minColor = static_cast<COLORREF>(GetWindowLongPtrW(GetDlgItem(hwnd, IDC_MIN_COLOR), GWLP_USERDATA));
}

bool ReadDraft(HWND hwnd, BandConfig& draft, Language language, std::wstring& error) {
	ReadAppearance(hwnd, draft);
	std::wstring url = ControlText(hwnd, IDC_URL);
	url.erase(std::remove(url.begin(), url.end(), L'\r'), url.end());
	url.erase(std::remove(url.begin(), url.end(), L'\n'), url.end());
	if (!CanonicalForecastUrl(url, draft.url, error)) {
		error = Text(language, L"请填写有效的 Open-Meteo API 地址。", L"Enter a valid Open-Meteo API URL.", L"有効な Open-Meteo API URL を入力してください。");
		return false;
	}
	BOOL valid = FALSE;
	int timezone = static_cast<int>(GetDlgItemInt(hwnd, IDC_TZ, &valid, TRUE));
	if (!valid) {
		error = Text(language, L"时区偏移要填整数。", L"The UTC offset must be a whole number.", L"UTC オフセットは整数で入力してください。");
		return false;
	}
	draft.timezoneOffsetHours = ClampInt(timezone, kTimezoneMin, kTimezoneMax);
	return true;
}

void SetColor(HWND hwnd, int id, COLORREF color) {
	HWND button = GetDlgItem(hwnd, id);
	SetWindowLongPtrW(button, GWLP_USERDATA, static_cast<LONG_PTR>(color));
	std::wstring text = L"#" + FormatColorText(color);
	SetWindowTextW(button, text.c_str());
	InvalidateRect(button, nullptr, TRUE);
}

void PreviewSettings(HWND hwnd, SettingsState* state) {
	if (state->initializing || !state->band) return;
	BandConfig next = state->draft;
	std::wstring error;
	if (!ReadDraft(hwnd, next, state->language, error)) {
		// 输入到一半的地址或时区不触发请求，但仍允许预览外观和语言。
		next = state->draft;
		ReadAppearance(hwnd, next);
	}
	state->draft = next;
	state->band->ApplyPreviewConfig(next);
}

void RefreshLanguage(HWND hwnd, SettingsState* state) {
	state->language = ResolveLanguage(state->draft.language);
	auto T = [&](const wchar_t* zh, const wchar_t* en, const wchar_t* ja) { return Text(state->language, zh, en, ja); };
	SetWindowTextW(hwnd, T(L"天气设置", L"Weather settings", L"天気の設定"));
	for (const auto& label : state->labels)
		SetWindowTextW(label.control, T(label.text.zh, label.text.en, label.text.ja));
	for (int id : { IDC_ICON_SIZE, IDC_CUR_SIZE, IDC_MAX_SIZE, IDC_MIN_SIZE, IDC_GAP_ICON, IDC_GAP_EXT, IDC_LANGUAGE }) {
		HWND combo = GetDlgItem(hwnd, id);
		LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
		SendMessageW(combo, CB_RESETCONTENT, 0, 0);
		if (id == IDC_LANGUAGE) {
			for (const wchar_t* text : { T(L"跟随系统", L"System default", L"システムに従う"), L"简体中文", L"English", L"日本語" })
				SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
		}
		else for (const wchar_t* text : { T(L"小", L"Small", L"小"), T(L"中", L"Medium", L"中"), T(L"大", L"Large", L"大") })
			SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
		SendMessageW(combo, CB_SETCURSEL, selected, 0);
	}
	if (state->palette) DestroyWindow(state->palette);
	SetControlText(hwnd, IDC_UPDATE_STATUS, state->update
		? T(L"正在检查 GitHub 正式版本…", L"Checking GitHub releases…", L"GitHub の正式リリースを確認しています…") : L"");
}

// URL 没有自然语言的“单词”；逐字符找可用边界，避免在 ?、% 等标点处提前折行。
int CALLBACK UrlWordBreak(LPWSTR, int position, int length, int action) {
	if (action == WB_ISDELIMITER) return FALSE;
	if (action == WB_LEFT) return (std::max)(0, position - 1);
	if (action == WB_RIGHT) return (std::min)(length, position + 1);
	return position;
}

bool CommitSettings(HWND hwnd, SettingsState* state) {
	if (!state || !state->band) return false;
	std::wstring error;
	BandConfig next = state->draft;
	if (!ReadDraft(hwnd, next, state->language, error)) {
		ShowNotice(hwnd, state->language, Text(state->language, L"请检查输入", L"Check your settings", L"入力を確認してください"), error.c_str());
		return false;
	}
	if (!state->band->ApplySavedConfig(next)) {
		ShowNotice(hwnd, state->language, Text(state->language, L"未能保存设置", L"Settings were not saved", L"設定を保存できませんでした"),
			Text(state->language, L"原配置未被覆盖。请检查文件夹权限；如果配置损坏或来自更高版本，请先备份并检查原文件。", L"The original file was preserved. Check folder permissions. Back up and inspect damaged or newer-version configuration files before retrying.", L"元の設定は保持されています。フォルダーの権限を確認してください。破損した設定や新しいバージョンの設定は、先にバックアップして確認してください。"));
		return false;
	}
	return true;
}

void ResetAppearance(HWND hwnd) {
	for (int id : { IDC_SHOW_ICON, IDC_SHOW_MAX, IDC_SHOW_MIN })
		Button_SetCheck(GetDlgItem(hwnd, id), BST_CHECKED);
	for (int id : { IDC_ICON_SIZE, IDC_MAX_SIZE, IDC_MIN_SIZE, IDC_GAP_ICON, IDC_GAP_EXT })
		SendDlgItemMessageW(hwnd, id, CB_SETCURSEL, 1, 0);
	SendDlgItemMessageW(hwnd, IDC_CUR_SIZE, CB_SETCURSEL, 2, 0);
	SetColor(hwnd, IDC_CUR_COLOR, TaskbarContrastColor());
	SetColor(hwnd, IDC_MAX_COLOR, RGB(255, 0, 0));
	SetColor(hwnd, IDC_MIN_COLOR, RGB(0, 178, 238));
}


constexpr COLORREF kPaletteColors[] = {
	RGB(255,255,255), RGB(224,224,224), RGB(160,160,160), RGB(96,96,96), RGB(32,32,32), RGB(0,0,0), RGB(255,0,0), RGB(255,128,0),
	RGB(255,192,0), RGB(255,255,0), RGB(128,192,0), RGB(0,192,0), RGB(0,128,0), RGB(0,192,128), RGB(0,192,192), RGB(0,178,238),
	RGB(0,128,255), RGB(0,64,192), RGB(0,0,255), RGB(96,0,192), RGB(160,0,224), RGB(224,0,192), RGB(255,0,128), RGB(128,0,0),
	RGB(255,192,192), RGB(255,224,192), RGB(255,255,192), RGB(192,255,192), RGB(192,255,255), RGB(192,224,255), RGB(224,192,255), RGB(255,192,224)
};
struct PaletteState {
	HWND owner = nullptr;
	int target = 0;
	int dpi = 96;
	COLORREF color = 0;
	bool syncing = false;
};

LRESULT CALLBACK PaletteChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == WM_KEYDOWN && (wp == VK_ESCAPE || wp == VK_RETURN)) {
		DestroyWindow(GetParent(hwnd)); return 0;
	}
	if (msg == WM_KEYDOWN && wp == VK_TAB) {
		HWND next = GetNextDlgTabItem(GetParent(hwnd), hwnd, GetKeyState(VK_SHIFT) < 0);
		if (next) SetFocus(next);
		return 0;
	}
	return DefSubclassProc(hwnd, msg, wp, lp);
}

void UpdatePalette(HWND hwnd, PaletteState* palette, COLORREF color) {
	palette->color = color;
	palette->syncing = true;
	int channels[] = { GetRValue(color), GetGValue(color), GetBValue(color) };
	for (int i = 0; i < 3; ++i) {
		SendDlgItemMessageW(hwnd, 300 + i, TBM_SETPOS, TRUE, channels[i]);
		SetDlgItemInt(hwnd, 310 + i, channels[i], FALSE);
	}
	palette->syncing = false;
	SendMessageW(palette->owner, kColorChanged, palette->target, color);
	InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK PaletteProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	auto* palette = reinterpret_cast<PaletteState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (msg == WM_NCCREATE) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
	if (!palette) return DefWindowProcW(hwnd, msg, wp, lp);
	auto S = [&](int value) { return MulDiv(value, palette->dpi, 96); };
	switch (msg) {
	case WM_PAINT: {
		PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
		for (int i = 0; i < 32; ++i) {
			RECT r{ S(10 + (i % 8) * 34), S(10 + (i / 8) * 28), S(40 + (i % 8) * 34), S(34 + (i / 8) * 28) };
			HBRUSH brush = CreateSolidBrush(kPaletteColors[i]); FillRect(dc, &r, brush); DeleteObject(brush);
			FrameRect(dc, &r, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
			if (kPaletteColors[i] == palette->color) { InflateRect(&r, S(2), S(2)); FrameRect(dc, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))); }
		}
		EndPaint(hwnd, &paint); return 0;
	}
	case WM_LBUTTONDOWN: {
		int column = (GET_X_LPARAM(lp) - S(10)) / S(34);
		int row = (GET_Y_LPARAM(lp) - S(10)) / S(28);
		if (GET_X_LPARAM(lp) >= S(10) && GET_Y_LPARAM(lp) >= S(10) && column < 8 && row < 4
			&& (GET_X_LPARAM(lp) - S(10)) % S(34) < S(30) && (GET_Y_LPARAM(lp) - S(10)) % S(28) < S(24))
			UpdatePalette(hwnd, palette, kPaletteColors[row * 8 + column]);
		return 0;
	}
	case WM_HSCROLL: {
		int channels[3]{};
		for (int i = 0; i < 3; ++i) channels[i] = static_cast<int>(SendDlgItemMessageW(hwnd, 300 + i, TBM_GETPOS, 0, 0));
		UpdatePalette(hwnd, palette, RGB(channels[0], channels[1], channels[2])); return 0;
	}
	case WM_COMMAND:
		if (!palette->syncing && HIWORD(wp) == EN_CHANGE && LOWORD(wp) >= 310 && LOWORD(wp) <= 312) {
			int channels[3]{};
			for (int i = 0; i < 3; ++i) {
				BOOL valid = FALSE; channels[i] = static_cast<int>(GetDlgItemInt(hwnd, 310 + i, &valid, FALSE));
				if (!valid || channels[i] > 255) return 0;
			}
			UpdatePalette(hwnd, palette, RGB(channels[0], channels[1], channels[2]));
		}
		return 0;
	case WM_ACTIVATE:
		// 非模态调色盘，点击设置页或其他窗口时自动收起，当前颜色保留在预览草稿中。
		if (LOWORD(wp) == WA_INACTIVE) DestroyWindow(hwnd);
		return 0;
	case WM_KEYDOWN: if (wp == VK_ESCAPE || wp == VK_RETURN) { DestroyWindow(hwnd); return 0; } break;
	case WM_CLOSE: DestroyWindow(hwnd); return 0;
	case WM_DESTROY: {
		auto* owner = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(palette->owner, GWLP_USERDATA));
		if (owner && owner->palette == hwnd) owner->palette = nullptr;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); delete palette; return 0;
	}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowPalette(HWND owner, SettingsState* state, int target) {
	if (state->palette) DestroyWindow(state->palette);
	INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_BAR_CLASSES };
	InitCommonControlsEx(&controls);
	WNDCLASSEXW klass{ sizeof(klass) };
	klass.lpfnWndProc = PaletteProc; klass.hInstance = g_hinstDll;
	klass.hCursor = LoadCursorW(nullptr, IDC_ARROW); klass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
	klass.lpszClassName = L"WeatherBarPalette";
	if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
	auto* palette = new PaletteState{ owner, target, state->dpi, static_cast<COLORREF>(GetWindowLongPtrW(GetDlgItem(owner, target), GWLP_USERDATA)) };
	auto S = [&](int value) { return Scale(state, value); };
	RECT button{}; GetWindowRect(GetDlgItem(owner, target), &button);
	RECT bounds{ 0, 0, S(292), S(238) };
	AdjustWindowRectExForDpi(&bounds, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOOLWINDOW, state->dpi);
	MONITORINFO monitor{ sizeof(monitor) }; GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
	int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
	int x = (std::max)(monitor.rcWork.left, (std::min)(button.left, monitor.rcWork.right - width));
	int y = button.bottom + height <= monitor.rcWork.bottom ? button.bottom : button.top - height;
	HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, klass.lpszClassName, Text(state->language, L"选择颜色", L"Choose color", L"色を選択"), WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, x, y, width, height, owner, nullptr, g_hinstDll, palette);
	if (!hwnd) { delete palette; return; }
	state->palette = hwnd;
	const wchar_t* labels[] = { Text(state->language, L"红", L"Red", L"赤"), Text(state->language, L"绿", L"Green", L"緑"), Text(state->language, L"蓝", L"Blue", L"青") };
	for (int i = 0; i < 3; ++i) {
		AddControl(hwnd, L"STATIC", labels[i], SS_CENTERIMAGE, -1, S(10), S(132 + i * 32), S(48), S(24), state->font);
		HWND slider = AddControl(hwnd, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, 300 + i, S(58), S(132 + i * 32), S(168), S(26), state->font);
		// 设置页的 Enter/Tab 子类不适用于独立调色盘，让原生滑块和数值框正常处理按键。
		RemoveWindowSubclass(slider, SettingsChildProc, 1);
		SetWindowSubclass(slider, PaletteChildProc, 1, 0);
		SendMessageW(slider, TBM_SETRANGE, FALSE, MAKELPARAM(0, 255));
		HWND edit = AddControl(hwnd, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_NUMBER, 310 + i, S(232), S(132 + i * 32), S(48), S(24), state->font);
		RemoveWindowSubclass(edit, SettingsChildProc, 1);
		SetWindowSubclass(edit, PaletteChildProc, 1, 0);
		SendMessageW(edit, EM_SETLIMITTEXT, 3, 0);
	}
	UpdatePalette(hwnd, palette, palette->color);
	ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); SetFocus(GetDlgItem(hwnd, 300));
}


LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (msg == WM_NCCREATE) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
	if (!state) return DefWindowProcW(hwnd, msg, wp, lp);
	switch (msg) {
	case WM_GETMINMAXINFO: {
		auto* bounds = reinterpret_cast<MINMAXINFO*>(lp);
		bounds->ptMinTrackSize = { state->minimumSize.cx, state->minimumSize.cy };
		return 0;
	}
	case WM_SIZE: {
		if (state->placements.empty()) break;
		RECT client{}; GetClientRect(hwnd, &client);
		int extraWidth = client.right - Scale(state, kClientWidth);
		int extraHeight = (std::max)(0L, client.bottom - Scale(state, kContentHeight));
		SCROLLINFO info{ sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, 0, Scale(state, kContentHeight) - 1, static_cast<UINT>(client.bottom), state->scroll, 0 };
		SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
		state->scroll = GetScrollPos(hwnd, SB_VERT);
		for (const auto& placement : state->placements) {
			const RECT& r = placement.rect;
			SetWindowPos(placement.control, nullptr, r.left + (placement.right ? extraWidth : 0),
				r.top + (placement.bottom ? extraHeight : 0) - state->scroll,
				r.right - r.left + (placement.stretch ? extraWidth : 0), r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		InvalidateRect(hwnd, nullptr, TRUE);
		return 0;
	}
	case WM_PAINT: {
		PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
		RECT client{}; GetClientRect(hwnd, &client);
		for (auto range : { std::pair<int, int>{10, 160}, {166, 366}, {370, 474}, {478, 674}, {682, 770} }) {
			RECT panel{ Scale(state, 12), Scale(state, range.first) - state->scroll, client.right - Scale(state, 12), Scale(state, range.second) - state->scroll };
			HGDIOBJ oldBrush = SelectObject(dc, state->panelBrush);
			HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
			RoundRect(dc, panel.left, panel.top, panel.right, panel.bottom, Scale(state, 14), Scale(state, 14));
			SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
		}
		EndPaint(hwnd, &paint);
		return 0;
	}
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLOREDIT: {
		HDC dc = reinterpret_cast<HDC>(wp);
		bool panel = msg != WM_CTLCOLOREDIT;
		SetTextColor(dc, RGB(35, 35, 35));
		SetBkColor(dc, panel ? RGB(240, 242, 245) : RGB(255, 255, 255));
		return reinterpret_cast<LRESULT>(panel ? state->panelBrush : GetStockObject(WHITE_BRUSH));
	}
	case kColorChanged:
		SetColor(hwnd, static_cast<int>(wp), static_cast<COLORREF>(lp));
		PreviewSettings(hwnd, state);
		return 0;
	case WM_DRAWITEM: {
		auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
		if (item->CtlID != IDC_CUR_COLOR && item->CtlID != IDC_MAX_COLOR && item->CtlID != IDC_MIN_COLOR) break;
		FillRect(item->hDC, &item->rcItem, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		FrameRect(item->hDC, &item->rcItem, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
		RECT swatch = item->rcItem; InflateRect(&swatch, -Scale(state, 5), -Scale(state, 5)); swatch.right = swatch.left + Scale(state, 18);
		HBRUSH brush = CreateSolidBrush(static_cast<COLORREF>(GetWindowLongPtrW(item->hwndItem, GWLP_USERDATA)));
		FillRect(item->hDC, &swatch, brush); DeleteObject(brush); FrameRect(item->hDC, &swatch, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
		RECT text = item->rcItem; text.left += Scale(state, 28);
		SetBkMode(item->hDC, TRANSPARENT); SetTextColor(item->hDC, RGB(35,35,35));
		std::wstring value = ControlText(hwnd, static_cast<int>(item->CtlID));
		HGDIOBJ previous = SelectObject(item->hDC, state->addressFont);
		DrawTextW(item->hDC, value.c_str(), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		SelectObject(item->hDC, previous);
		if (item->itemState & ODS_FOCUS) { RECT focus = item->rcItem; InflateRect(&focus, -2, -2); DrawFocusRect(item->hDC, &focus); }
		return TRUE;
	}
	case WM_COMMAND:
		if (state->initializing) return 0;
		if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == BN_CLICKED) {
			if (LOWORD(wp) == IDC_LANGUAGE) {
				ReadAppearance(hwnd, state->draft);
				RefreshLanguage(hwnd, state);
			}
			if (LOWORD(wp) == IDC_LANGUAGE || (LOWORD(wp) >= IDC_SHOW_ICON && LOWORD(wp) <= IDC_MIN_COLOR)
				|| (LOWORD(wp) >= IDC_TOOLTIP && LOWORD(wp) <= IDC_TOOLTIP_LAST)) PreviewSettings(hwnd, state);
		}
		if (HIWORD(wp) == EN_CHANGE && (LOWORD(wp) == IDC_URL || LOWORD(wp) == IDC_TZ))
			if (!SetTimer(hwnd, kPreviewTimer, 350, nullptr)) PreviewSettings(hwnd, state);
		switch (LOWORD(wp)) {
		case IDC_CUR_COLOR: case IDC_MAX_COLOR: case IDC_MIN_COLOR: ShowPalette(hwnd, state, LOWORD(wp)); break;
		case IDC_SAVE: if (CommitSettings(hwnd, state)) DestroyWindow(hwnd); break;
		case IDC_CANCEL: DestroyWindow(hwnd); break;
		case IDC_RESET: ResetAppearance(hwnd); PreviewSettings(hwnd, state); break;
		case IDC_RELEASES:
			if (reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", kReleasePage, nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
				SetControlText(hwnd, IDC_UPDATE_STATUS, Text(state->language, L"无法打开浏览器，请手动访问 GitHub 发布页。", L"Could not open your browser. Visit the GitHub release page manually.", L"ブラウザーを開けませんでした。GitHub のリリースページを直接開いてください。"));
			break;
		case IDC_CHECK_UPDATE:
			if (!state->update) {
				try {
					state->update = StartUpdateCheck();
					EnableWindow(GetDlgItem(hwnd, IDC_CHECK_UPDATE), FALSE);
					SetControlText(hwnd, IDC_UPDATE_STATUS, Text(state->language, L"正在检查 GitHub 正式版本…", L"Checking GitHub releases…", L"GitHub の正式リリースを確認しています…"));
					if (!SetTimer(hwnd, kUpdateTimer, 200, nullptr)) {
						state->update.reset();
						EnableWindow(GetDlgItem(hwnd, IDC_CHECK_UPDATE), TRUE);
						SetControlText(hwnd, IDC_UPDATE_STATUS, Text(state->language, L"无法显示检查结果，请稍后重试。", L"Could not display the result. Try again later.", L"確認結果を表示できません。後でもう一度お試しください。"));
					}
				}
				catch (...) { SetControlText(hwnd, IDC_UPDATE_STATUS, Text(state->language, L"无法启动检查，请稍后重试。", L"Could not start the check. Try again later.", L"確認を開始できませんでした。後でもう一度お試しください。")); }
			}
			break;
		}
		return 0;
	case WM_TIMER:
		if (wp == kPreviewTimer) { KillTimer(hwnd, kPreviewTimer); PreviewSettings(hwnd, state); return 0; }
		if (wp == kUpdateTimer && state->update && state->update->done.load(std::memory_order_acquire)) {
			std::wstring message;
			switch (state->update->status) {
			case UpdateStatus::Available: message = Text(state->language, L"发现新版本：", L"New version: ", L"新しいバージョン：") + state->update->latestVersion; break;
			case UpdateStatus::Current: message = Text(state->language, L"当前已是最新正式版本。", L"You have the latest stable version.", L"最新の正式バージョンです。"); break;
			case UpdateStatus::NoRelease: message = Text(state->language, L"尚无正式发布版本。", L"No stable release has been published.", L"正式リリースはまだありません。"); break;
			default:
				message = Text(state->language, L"检查失败，可打开发布页。", L"Check failed. Open the release page.", L"確認失敗。リリースページをご覧ください。");
				if (state->update->errorCode) message += L" (WinHTTP " + std::to_wstring(state->update->errorCode) + L")";
				else if (state->update->httpStatus) message += L" (HTTP " + std::to_wstring(state->update->httpStatus) + L")";
				break;
			}
			SetControlText(hwnd, IDC_UPDATE_STATUS, message);
			state->update.reset();
			EnableWindow(GetDlgItem(hwnd, IDC_CHECK_UPDATE), TRUE);
			KillTimer(hwnd, kUpdateTimer);
		}
		return 0;
	case WM_VSCROLL: {
		SCROLLINFO info{ sizeof(info), SIF_ALL };
		GetScrollInfo(hwnd, SB_VERT, &info);
		int next = info.nPos;
		switch (LOWORD(wp)) {
		case SB_LINEUP: next -= Scale(state, 24); break;
		case SB_LINEDOWN: next += Scale(state, 24); break;
		case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
		case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
		case SB_THUMBTRACK: next = info.nTrackPos; break;
		case SB_THUMBPOSITION: next = HIWORD(wp); break;
		}
		info.fMask = SIF_POS;
		info.nPos = next;
		SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
		GetScrollInfo(hwnd, SB_VERT, &info);
		ScrollWindowEx(hwnd, 0, state->scroll - info.nPos, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
		state->scroll = info.nPos;
		return 0;
	}
	case WM_MOUSEWHEEL:
		SendMessageW(hwnd, WM_VSCROLL, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
		return 0;
	case WM_CLOSE: DestroyWindow(hwnd); return 0;
	case WM_DESTROY:
		KillTimer(hwnd, kUpdateTimer);
		KillTimer(hwnd, kPreviewTimer);
		if (state->palette) DestroyWindow(state->palette);
		if (state->band) { state->band->CancelSettingsPreview(); state->band->OnSettingsClosed(); }
		if (state->font) DeleteObject(state->font);
		if (state->headingFont) DeleteObject(state->headingFont);
		if (state->addressFont) DeleteObject(state->addressFont);
		if (state->panelBrush) DeleteObject(state->panelBrush);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		delete state;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}
}

HWND ShowBandSettings(HWND owner, CDeskBand* band, const BandConfig& current) {
	const wchar_t* className = L"WeatherBarSettings";
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = SettingsProc;
	wc.hInstance = g_hinstDll;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
	wc.lpszClassName = className;
	if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
	auto* state = new SettingsState();
	state->band = band;
	state->draft = current;
	state->language = ResolveLanguage(current.language);
	UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
	state->dpi = dpi ? static_cast<int>(dpi) : 96;
	auto S = [&](int value) { return Scale(state, value); };
	auto T = [&](const wchar_t* zh, const wchar_t* en, const wchar_t* ja) { return Text(state->language, zh, en, ja); };
	state->font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	state->headingFont = CreateFontW(-S(17), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	state->panelBrush = CreateSolidBrush(RGB(240, 242, 245));
	POINT cursor{};
	GetCursorPos(&cursor);
	MONITORINFO monitor{ sizeof(monitor) };
	GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
	int clientHeight = S(kContentHeight);
	int maximumHeight = monitor.rcWork.bottom - monitor.rcWork.top - S(64);
	bool scroll = clientHeight > maximumHeight;
	if (scroll) clientHeight = maximumHeight;
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | (scroll ? WS_VSCROLL : 0);
	RECT rect{ 0, 0, S(kClientWidth), clientHeight };
	AdjustWindowRectExForDpi(&rect, style, FALSE, 0, state->dpi);
	int width = rect.right - rect.left, height = rect.bottom - rect.top;
	state->minimumSize = { width, height };
	int x = monitor.rcWork.left + ((monitor.rcWork.right - monitor.rcWork.left) - width) / 2;
	int y = monitor.rcWork.top + ((monitor.rcWork.bottom - monitor.rcWork.top) - height) / 2;
	HWND hwnd = CreateWindowExW(0, className, T(L"天气设置", L"Weather settings", L"天気の設定"), style, x, y, width, height, owner, nullptr, g_hinstDll, state);
	if (!hwnd) { DeleteObject(state->font); DeleteObject(state->headingFont); DeleteObject(state->panelBrush); delete state; return nullptr; }

	auto control = [&](const wchar_t* klass, SettingsState::LocalText text, DWORD flags, int id, int left, int top, int w, int h) {
		HWND child = AddControl(hwnd, klass, T(text.zh, text.en, text.ja), flags, id, S(left), S(top), S(w), S(h), state->font);
		if (text.zh[0]) state->labels.push_back({ child, text });
		return child;
	};
	auto label = [&](SettingsState::LocalText text, int left, int top, int w, int id = -1) {
		return control(L"STATIC", text, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, id, left, top, w, 24);
	};
	auto edit = [&](int id, int left, int top, int w) {
		return control(L"EDIT", { L"", L"", L"" }, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, id, left, top, w, 26);
	};
	auto section = [&](SettingsState::LocalText text, int top) {
		HWND title = label(text, 20, top, 480);
		SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(state->headingFont), TRUE);
	};
	auto sizeCombo = [&](int id, int top, SizeLevel level) {
		HWND combo = control(L"COMBOBOX", { L"", L"", L"" }, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, id, 296, top, 84, 130);
		for (const wchar_t* text : { T(L"小", L"Small", L"小"), T(L"中", L"Medium", L"中"), T(L"大", L"Large", L"大") }) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
		SendMessageW(combo, CB_SETCURSEL, static_cast<int>(level), 0);
	};
	section({ L"天气数据", L"Weather data", L"天気データ" }, 16);
	label({ L"Open-Meteo API 地址", L"Open-Meteo API URL", L"Open-Meteo API URL" }, 20, 42, 480);
	HWND address = control(L"EDIT", { L"", L"", L"" }, WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, IDC_URL, 20, 68, 480, 68);
	state->addressFont = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	SendMessageW(address, WM_SETFONT, reinterpret_cast<WPARAM>(state->addressFont), TRUE);
	SendMessageW(address, EM_SETWORDBREAKPROC, 0, reinterpret_cast<LPARAM>(UrlWordBreak));
	HDC addressDC = GetDC(address);
	if (addressDC) {
		HGDIOBJ previous = SelectObject(addressDC, state->addressFont);
		TEXTMETRICW metrics{};
		if (GetTextMetricsW(addressDC, &metrics))
			SetWindowPos(address, nullptr, 0, 0, S(480), 3 * metrics.tmHeight + 2 * GetSystemMetricsForDpi(SM_CYBORDER, state->dpi), SWP_NOMOVE | SWP_NOZORDER);
		SelectObject(addressDC, previous); ReleaseDC(address, addressDC);
	}
	label({ L"UTC 时区偏移", L"UTC offset", L"UTC オフセット" }, 20, 128, 108);
	edit(IDC_TZ, 130, 128, 52);
	label({ L"界面语言", L"Language", L"表示言語" }, 238, 128, 108);
	HWND languages = control(L"COMBOBOX", { L"", L"", L"" }, WS_TABSTOP | CBS_DROPDOWNLIST, IDC_LANGUAGE, 350, 128, 150, 150);
	for (const wchar_t* text : { T(L"跟随系统", L"System default", L"システムに従う"), L"简体中文", L"English", L"日本語" }) SendMessageW(languages, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
	SendMessageW(languages, CB_SETCURSEL, static_cast<int>(current.language), 0);
	section({ L"显示与外观", L"Display", L"表示と外観" }, 172);
	label({ L"项目", L"Item", L"項目" }, 20, 200, 188);
	label({ L"显示", L"Show", L"表示" }, 220, 200, 56);
	label({ L"大小", L"Size", L"サイズ" }, 296, 200, 84);
	label({ L"颜色", L"Color", L"色" }, 412, 200, 88);
	auto appearanceRow = [&](SettingsState::LocalText title, int top, int showId, bool show, int sizeId, SizeLevel level, int colorId, COLORREF color) {
		label(title, 20, top, 188);
		if (showId) {
			HWND check = control(L"BUTTON", { L"", L"", L"" }, WS_TABSTOP | BS_AUTOCHECKBOX, showId, 226, top, 24, 26);
			Button_SetCheck(check, show ? BST_CHECKED : BST_UNCHECKED);
		}
		else label({ L"始终", L"Always", L"常に" }, 220, top, 68);
		sizeCombo(sizeId, top, level);
		if (colorId) {
			control(L"BUTTON", { L"", L"", L"" }, WS_TABSTOP | BS_OWNERDRAW, colorId, 412, top, 88, 26);
			SetColor(hwnd, colorId, color);
		}
		else label({ L"—", L"—", L"—" }, 412, top, 88);
	};
	appearanceRow({ L"当前温度", L"Current temperature", L"現在の気温" }, 230, 0, true, IDC_CUR_SIZE, current.currentLevel, IDC_CUR_COLOR, current.currentColor);
	appearanceRow({ L"天气图标", L"Weather icon", L"天気アイコン" }, 264, IDC_SHOW_ICON, current.showIcon, IDC_ICON_SIZE, current.iconLevel, 0, 0);
	appearanceRow({ L"今日最高温", L"Today's high", L"今日の最高気温" }, 298, IDC_SHOW_MAX, current.showMax, IDC_MAX_SIZE, current.maxLevel, IDC_MAX_COLOR, current.maxColor);
	appearanceRow({ L"今日最低温", L"Today's low", L"今日の最低気温" }, 332, IDC_SHOW_MIN, current.showMin, IDC_MIN_SIZE, current.minLevel, IDC_MIN_COLOR, current.minColor);
	section({ L"间距", L"Spacing", L"間隔" }, 376);
	label({ L"图标与当前温度", L"Icon to temperature", L"アイコンと気温" }, 20, 406, 260);
	sizeCombo(IDC_GAP_ICON, 406, current.gapIconLevel);
	label({ L"当前温度与高低温", L"Temperature to high / low", L"気温と最高・最低気温" }, 20, 440, 260);
	sizeCombo(IDC_GAP_EXT, 440, current.gapExtremeLevel);
	section({ L"悬停提示", L"Hover tooltip", L"ツールチップ" }, 484);
	HWND hover = control(L"BUTTON", { L"开启悬停提示", L"Enable tooltip", L"ツールチップを表示" }, WS_TABSTOP | BS_AUTOCHECKBOX, IDC_TOOLTIP, 20, 514, 480, 24);
	Button_SetCheck(hover, current.tooltipEnabled ? BST_CHECKED : BST_UNCHECKED);
	label({ L"底部固定显示：上次刷新、状态", L"Always at bottom: last refresh, status", L"最下部に常に表示：最終更新・状態" }, 20, 542, 480);
	SettingsState::LocalText tooltipLabels[] = {
		{ L"今日最高 / 最低", L"Today's high / low", L"今日の最高・最低" },
		{ L"紫外线指数", L"UV index", L"紫外線指数" },
		{ L"风向", L"Wind direction", L"風向" },
		{ L"风速", L"Wind speed", L"風速" },
		{ L"相对湿度", L"Relative humidity", L"相対湿度" },
		{ L"体感温度", L"Feels like", L"体感温度" },
		{ L"降水概率", L"Precipitation chance", L"降水確率" }
	};
	for (int field = 0; field < 7; ++field) {
		HWND check = control(L"BUTTON", tooltipLabels[field], WS_TABSTOP | BS_AUTOCHECKBOX,
			IDC_TOOLTIP_FIRST + field, field % 2 ? 266 : 20, 572 + field / 2 * 24, 234, 24);
		Button_SetCheck(check, current.tooltipFields & (1u << field) ? BST_CHECKED : BST_UNCHECKED);
		EnableWindow(check, current.tooltipEnabled);
	}
	section({ L"关于与更新", L"About & updates", L"このアプリについて" }, 688);
	std::wstring version = std::wstring(L"WeatherBar  ") + kAppVersion;
	AddControl(hwnd, L"STATIC", version.c_str(), SS_LEFT | SS_CENTERIMAGE, -1, S(20), S(720), S(220), S(24), state->font);
	control(L"BUTTON", { L"检查更新", L"Check updates", L"更新を確認" }, WS_TABSTOP | BS_PUSHBUTTON, IDC_CHECK_UPDATE, 292, 716, 100, 30);
	control(L"BUTTON", { L"发布页面", L"Release page", L"リリースページ" }, WS_TABSTOP | BS_PUSHBUTTON, IDC_RELEASES, 404, 716, 96, 30);
	label({ L"", L"", L"" }, 20, 748, 480, IDC_UPDATE_STATUS);
	control(L"BUTTON", { L"恢复默认", L"Reset appearance", L"外観を初期化" }, WS_TABSTOP | BS_PUSHBUTTON, IDC_RESET, 20, 776, 142, 30);
	control(L"BUTTON", { L"保存", L"Save", L"保存" }, WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_SAVE, 300, 776, 92, 30);
	control(L"BUTTON", { L"取消", L"Cancel", L"キャンセル" }, WS_TABSTOP | BS_PUSHBUTTON, IDC_CANCEL, 408, 776, 92, 30);
	SetControlText(hwnd, IDC_URL, current.url);
	SetControlText(hwnd, IDC_TZ, std::to_wstring(current.timezoneOffsetHours));
	SendDlgItemMessageW(hwnd, IDC_URL, EM_SETLIMITTEXT, 2048, 0);
	SendDlgItemMessageW(hwnd, IDC_URL, EM_SETSEL, 0, 0);
	SendDlgItemMessageW(hwnd, IDC_URL, EM_SCROLLCARET, 0, 0);
	if (scroll) {
		SCROLLINFO info{ sizeof(info), SIF_RANGE | SIF_PAGE, 0, S(kContentHeight) - 1, static_cast<UINT>(clientHeight), 0, 0 };
		SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
	}
	for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
		RECT r{}; GetWindowRect(child, &r);
		MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
		wchar_t klass[32]{}; GetClassNameW(child, klass, 32);
		if (lstrcmpiW(klass, L"ComboBox") == 0) {
			// ComboBox 的窗口矩形只含收起部分；缩放布局必须保留下拉列表的高度。
			RECT dropped{};
			if (SendMessageW(child, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropped)))
				r.bottom = r.top + dropped.bottom - dropped.top;
		}
		int id = GetDlgCtrlID(child);
		bool stretch = r.right - r.left >= S(460);
		state->placements.push_back({ child, r, stretch, !stretch && r.left >= S(292), id == IDC_RESET || id == IDC_SAVE || id == IDC_CANCEL });
	}
	if (band) band->BeginSettingsPreview();
	state->initializing = false;
	ShowWindow(hwnd, SW_SHOW);
	SetForegroundWindow(hwnd);
	return hwnd;
}

void CloseBandSettings(HWND hwnd) {
	if (!hwnd || !IsWindow(hwnd)) return;
	auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (state) state->band = nullptr;
	DestroyWindow(hwnd);
}
