#include "TestSupport.h"
#include "../src/Settings.cpp"

std::string ReadConfigBytes();

void TestSettingsBehavior() {
	for (Language language : { Language::Chinese, Language::English, Language::Japanese }) {
		BandConfig original = DefaultBandConfig();
		original.language = language;
		original.delaySeconds = 73;
		original.currentColor = RGB(1, 2, 3);
		Require(SaveBandConfig(original), "prepare settings fixture");
		std::string before = ReadConfigBytes();
		CDeskBand band;
		HWND hwnd = ShowBandSettings(nullptr, &band, original);
		Require(hwnd != nullptr, "settings window in each supported language");
		wchar_t title[128]{};
		Require(GetWindowTextW(hwnd, title, 128) > 0, "localized window title must be initialized");
		Require(SendDlgItemMessageW(hwnd, IDC_LANGUAGE, CB_GETCOUNT, 0, 0) == 4, "system plus three languages");
		Require(band.CurrentConfig().language == language, "preview fixture starts with saved language");
		Require(!FindWindowExW(hwnd, nullptr, L"STATIC", L"WeatherBar"), "branding removed");
		Require((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0, "settings supports resizing");
		HWND url = GetDlgItem(hwnd, IDC_URL);
		Require((GetWindowLongPtrW(url, GWL_STYLE) & ES_MULTILINE) != 0, "API address is multiline");
		RECT client{}, initial{}, address{};
		GetClientRect(hwnd, &client); GetWindowRect(hwnd, &initial); GetWindowRect(url, &address);
		Require(client.right == MulDiv(520, GetDpiForWindow(hwnd), 96), "compact page width");
		RECT sizeControl{}; GetWindowRect(GetDlgItem(hwnd, IDC_CUR_SIZE), &sizeControl);
		Require(sizeControl.right - sizeControl.left == MulDiv(84, GetDpiForWindow(hwnd), 96), "compact size dropdown");
		Require(SendMessageW(url, EM_LINEINDEX, 1, 0) > 60, "URL fills the row instead of breaking at punctuation");
		Require(SendMessageW(url, EM_GETLINECOUNT, 0, 0) <= 3, "default API address fits three lines at compact width");
		HDC addressDC = GetDC(url); HGDIOBJ previous = SelectObject(addressDC, reinterpret_cast<HFONT>(SendMessageW(url, WM_GETFONT, 0, 0)));
		TEXTMETRICW metrics{}; GetTextMetricsW(addressDC, &metrics); SelectObject(addressDC, previous); ReleaseDC(url, addressDC);
		Require(address.bottom - address.top == 3 * metrics.tmHeight + 2 * GetSystemMetricsForDpi(SM_CYBORDER, GetDpiForWindow(hwnd)), "API field uses three actual font lines");
		MINMAXINFO bounds{}; SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&bounds));
		Require(bounds.ptMinTrackSize.x == initial.right - initial.left && bounds.ptMinTrackSize.y == initial.bottom - initial.top, "opens at minimum size");
		for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
			RECT r{}; GetWindowRect(child, &r); MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
			Require(r.left >= 0 && r.top >= 0 && r.right <= client.right && r.bottom <= client.bottom, "all controls fit at initial size");
			for (HWND other = GetWindow(child, GW_HWNDNEXT); other; other = GetWindow(other, GW_HWNDNEXT)) {
				RECT otherRect{}, overlap{}; GetWindowRect(other, &otherRect);
				MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&otherRect), 2);
				Require(!IntersectRect(&overlap, &r, &otherRect), "compact controls must not overlap");
			}
			wchar_t klass[32]{}, text[256]{}; GetClassNameW(child, klass, 32);
			int length = GetWindowTextW(child, text, 256);
			if (lstrcmpiW(klass, L"STATIC") == 0 && length > 0) {
				HDC dc = GetDC(child);
				HGDIOBJ font = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0)));
				SIZE extent{}; GetTextExtentPoint32W(dc, text, length, &extent);
				SelectObject(dc, font); ReleaseDC(child, dc);
				if (extent.cx > r.right - r.left || extent.cy > r.bottom - r.top) {
					char description[1024]{};
					WideCharToMultiByte(CP_UTF8, 0, text, -1, description, 1024, nullptr, nullptr);
					throw std::runtime_error(std::string("clipped localized label: ") + description + " measured " + std::to_string(extent.cx) + "x" + std::to_string(extent.cy) + " available " + std::to_string(r.right-r.left) + "x" + std::to_string(r.bottom-r.top));
				}
			}
		}
		RECT dropdownBefore{}, dropdownAfter{};
		SendDlgItemMessageW(hwnd, IDC_LANGUAGE, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropdownBefore));
		SetWindowPos(hwnd, nullptr, 0, 0, initial.right - initial.left + 100, initial.bottom - initial.top + 80, SWP_NOMOVE | SWP_NOZORDER);
		SendDlgItemMessageW(hwnd, IDC_LANGUAGE, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropdownAfter));
		Require(dropdownAfter.bottom - dropdownAfter.top == dropdownBefore.bottom - dropdownBefore.top, "resizing preserves dropdown list height");
		RECT larger{}; GetWindowRect(url, &larger);
		Require(larger.right - larger.left == address.right - address.left + 100, "API field expands with window");
		SetWindowPos(hwnd, nullptr, 0, 0, initial.right - initial.left, initial.bottom - initial.top, SWP_NOMOVE | SWP_NOZORDER);
		RECT reset{}, save{}, cancel{};
		GetWindowRect(GetDlgItem(hwnd, IDC_RESET), &reset);
		GetWindowRect(GetDlgItem(hwnd, IDC_SAVE), &save);
		GetWindowRect(GetDlgItem(hwnd, IDC_CANCEL), &cancel);
		Require(reset.right < save.left && save.right < cancel.left, "reset left; save and cancel right without overlap");
		Button_SetCheck(GetDlgItem(hwnd, IDC_SHOW_ICON), BST_UNCHECKED);
		SendMessageW(hwnd, WM_COMMAND, IDC_SHOW_ICON, 0);
		Require(!band.CurrentConfig().showIcon && ReadConfigBytes() == before, "appearance previews without saving");
		SendDlgItemMessageW(hwnd, IDC_LANGUAGE, CB_SETCURSEL, static_cast<int>(Language::English), 0);
		SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_LANGUAGE, CBN_SELCHANGE), 0);
		GetWindowTextW(hwnd, title, 128);
		Require(std::wstring(title) == L"Weather settings" && band.CurrentConfig().language == Language::English, "language immediately changes the same window and band");
		Require(ReadConfigBytes() == before, "language preview is not persisted");
		SendDlgItemMessageW(hwnd, IDC_LANGUAGE, CB_SETCURSEL, static_cast<int>(language), 0);
		SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_LANGUAGE, CBN_SELCHANGE), 0);
		SendMessageW(hwnd, WM_COMMAND, IDC_CUR_COLOR, 0);
		auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		HWND palette = state->palette;
		Require(palette && IsWindow(palette), "color button opens nonmodal palette");
		SendDlgItemMessageW(palette, 300, TBM_SETPOS, TRUE, 12);
		SendDlgItemMessageW(palette, 301, TBM_SETPOS, TRUE, 34);
		SendDlgItemMessageW(palette, 302, TBM_SETPOS, TRUE, 56);
		SendMessageW(palette, WM_HSCROLL, 0, 0);
		Require(band.CurrentConfig().currentColor == RGB(12,34,56) && ReadConfigBytes() == before, "arbitrary RGB palette changes preview only");
		SendMessageW(palette, WM_ACTIVATE, WA_INACTIVE, 0);
		Require(!IsWindow(palette) && !state->palette, "palette hides on outside activation");
		SetControlText(hwnd, IDC_TZ, L"8junk");
		BandConfig draft = original;
		std::wstring error;
		Require(!ReadDraft(hwnd, draft, language, error), "invalid timezone rejected without saving");
		SetControlText(hwnd, IDC_TZ, L"8");
		std::wstring wrapped = original.url;
		size_t ampersand = wrapped.find(L'&');
		Require(ampersand != std::wstring::npos, "URL fixture contains query parameters");
		wrapped.insert(ampersand + 1, L"\r\n");
		SetControlText(hwnd, IDC_URL, wrapped);
		Require(ReadDraft(hwnd, draft, language, error) && draft.url == original.url, "pasted URL line breaks normalized");
		SendMessageW(url, WM_KEYDOWN, VK_RETURN, 0);
		Require(IsWindow(hwnd), "Enter in API field does not save or close");
		SetControlText(hwnd, IDC_URL, original.url);
		SendMessageW(hwnd, WM_COMMAND, IDC_RESET, 0);
		Require(ReadDraft(hwnd, draft, language, error), "read reset appearance");
		Require(draft.language == language && draft.url == original.url && draft.delaySeconds == 73, "reset preserves language, URL and stagger");
		Require(ReadConfigBytes() == before, "reset is not persisted until save");
		SendMessageW(hwnd, WM_COMMAND, IDC_CANCEL, 0);
		Require(!IsWindow(hwnd) && ReadConfigBytes() == before, "cancel discards draft");
		Require(band.CurrentConfig().language == language && band.CurrentConfig().showIcon == original.showIcon && band.CurrentConfig().currentColor == original.currentColor, "cancel restores original in-memory display");
		hwnd = ShowBandSettings(nullptr, &band, original);
		SendDlgItemMessageW(hwnd, IDC_LANGUAGE, CB_SETCURSEL, static_cast<int>(Language::Japanese), 0);
		SendMessageW(hwnd, WM_COMMAND, IDC_SAVE, 0);
		Require(!IsWindow(hwnd) && LoadBandConfig().language == Language::Japanese, "save persists language and closes window");
		Require(band.CurrentConfig().language == Language::Japanese, "closing after save retains committed preview");
		hwnd = ShowBandSettings(nullptr, &band, band.CurrentConfig());
		Button_SetCheck(GetDlgItem(hwnd, IDC_SHOW_ICON), BST_UNCHECKED);
		SendMessageW(hwnd, WM_COMMAND, IDC_SHOW_ICON, 0);
		SendMessageW(hwnd, WM_CLOSE, 0, 0);
		Require(band.CurrentConfig().showIcon, "title-bar close also restores preview");
	}
}
