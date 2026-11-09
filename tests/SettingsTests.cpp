#include "TestSupport.h"
#include "../src/Settings.cpp"
#include <filesystem>
#include <iostream>
#include <wincodec.h>
#include <atlbase.h>
#pragma comment(lib,"windowscodecs.lib")

std::string ReadConfigBytes();

void VerifySettingsLayout(HWND hwnd) {
 auto* state=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
 RECT client{},frame{};GetClientRect(state->content,&client);GetClientRect(hwnd,&frame);
 Require(!(GetWindowLongW(hwnd,GWL_STYLE)&(WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_VSCROLL)) && (GetWindowLongW(hwnd,GWL_STYLE)&WS_THICKFRAME),"resizable close-only frame without a second scrollbar");
 SCROLLINFO scroll{sizeof(scroll),SIF_ALL};GetScrollInfo(state->content,SB_VERT,&scroll);
 Require(scroll.nPage==static_cast<UINT>((std::min)(state->contentHeight,static_cast<int>(client.bottom))) && client.bottom==state->viewportHeight,"scroll page excludes fixed footer");
 Require(!!(GetWindowLongW(state->content,GWL_STYLE)&WS_VSCROLL)==(state->contentHeight>state->viewportHeight),"main scrollbar is present exactly when needed");
 for(int id:{IDC_RESET,IDC_SAVE,IDC_CANCEL}){RECT r{};GetWindowRect(SettingsControl(hwnd,id),&r);MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&r),2);Require(r.top>=client.bottom && r.bottom<=frame.bottom && r.left>=0 && r.right<=frame.right,"footer controls always fit outside content");}
 for(HWND child=GetWindow(state->content,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
  if(!IsWindowVisible(child))continue;
  RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,state->content,reinterpret_cast<POINT*>(&r),2);
  Require(r.left>=0 && r.right<=client.right && r.bottom+state->scroll<=state->contentHeight,"controls fit scrollable content");
  for(HWND other=GetWindow(child,GW_HWNDNEXT);other;other=GetWindow(other,GW_HWNDNEXT)) {
   if(!IsWindowVisible(other))continue;RECT b{},overlap{};GetWindowRect(other,&b);MapWindowPoints(nullptr,state->content,reinterpret_cast<POINT*>(&b),2);
   if(IntersectRect(&overlap,&r,&b))throw std::runtime_error("overlap "+std::to_string(GetDlgCtrlID(child))+" / "+std::to_string(GetDlgCtrlID(other)));
  }
  wchar_t klass[32]{},text[512]{};GetClassNameW(child,klass,32);int length=GetWindowTextW(child,text,512);int id=GetDlgCtrlID(child);
  if((lstrcmpiW(klass,L"STATIC")==0 || lstrcmpiW(klass,L"BUTTON")==0) && length && id!=IDC_UPDATE_STATUS) {
   HDC dc=GetDC(child);auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0)));SIZE size{};GetTextExtentPoint32W(dc,text,length,&size);SelectObject(dc,old);ReleaseDC(child,dc);
   int extra=id==IDC_TOOLTIP || (id>=IDC_TOOLTIP_FIRST && id<=IDC_TOOLTIP_LAST)?Scale(state,24):0;
   if(size.cx+extra>r.right-r.left || size.cy>r.bottom-r.top)throw std::runtime_error("clipped control "+std::to_string(id));
  }
 }
 if(state->twoColumns){
  Require(state->cards[1].top==state->cards[3].top && state->cards[1].bottom==state->cards[3].bottom,"first card row shares top and bottom");
  Require(state->cards[2].top==state->cards[4].top && state->cards[2].bottom==state->cards[4].bottom && state->cards[4].left==state->cards[3].left,"second card row shares top and bottom");
 }
 RECT heading{},master{},option{};GetWindowRect(SettingsControl(hwnd,IDC_TOOLTIP_TITLE),&heading);GetWindowRect(SettingsControl(hwnd,IDC_TOOLTIP),&master);GetWindowRect(SettingsControl(hwnd,IDC_TOOLTIP_FIRST),&option);
 Require(heading.top==master.top && heading.bottom==master.bottom && heading.right<master.left && option.top>=master.bottom,"labeled tooltip master shares heading row without overlap");
 for(size_t i=0;i<state->cards.size();++i)for(size_t j=i+1;j<state->cards.size();++j){RECT intersection{};Require(!IntersectRect(&intersection,&state->cards[i],&state->cards[j]),"cards keep their natural height without overlap");}
 RECT language{},country{},query{},search{};GetWindowRect(SettingsControl(hwnd,IDC_LANGUAGE),&language);GetWindowRect(SettingsControl(hwnd,IDC_COUNTRY),&country);GetWindowRect(SettingsControl(hwnd,IDC_QUERY),&query);GetWindowRect(SettingsControl(hwnd,IDC_SEARCH),&search);
 Require(language.left==country.left && language.right==country.right && country.right<query.left && query.right<search.left,"language/country alignment and compact search columns");
 Require(abs((country.top+country.bottom)-(query.top+query.bottom))<=Scale(state,8),"search controls share vertical center");
 RECT hint{},apply{};int status=IDC_LOCATION_STATUS;GetWindowRect(SettingsControl(hwnd,status),&hint);GetWindowRect(SettingsControl(hwnd,IDC_APPLY_LOCATION),&apply);
 Require(hint.top==apply.top && hint.right<apply.left && ((GetWindowLongW(SettingsControl(hwnd,status),GWL_STYLE)&SS_TYPEMASK)==SS_OWNERDRAW),"one centered plain status shares the action row");
 RECT normal{};GetWindowRect(SettingsControl(hwnd,IDC_RESET),&normal);
 for(int id:{IDC_APPLY_LOCATION,IDC_APPLY_COORDINATES,IDC_APPLY_URL,IDC_CHECK_UPDATE,IDC_RELEASES}){if(!IsWindowVisible(SettingsControl(hwnd,id)))continue;RECT r{};GetWindowRect(SettingsControl(hwnd,id),&r);Require(r.right-r.left==normal.right-normal.left && r.bottom-r.top==normal.bottom-normal.top,"normal buttons have one measured size");}
}

void VerifyCountryDropdown(HWND hwnd) {
 HWND combo=SettingsControl(hwnd,IDC_COUNTRY);auto* state=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
 int oldScroll=state->scroll;SetFocus(combo);SendMessageW(hwnd,WM_VSCROLL,SB_BOTTOM,0);int selected=static_cast<int>(SendMessageW(combo,CB_GETCURSEL,0,0));SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);
 COMBOBOXINFO info{sizeof(info)};GetComboBoxInfo(combo,&info);RECT actual{},bounds{};GetWindowRect(info.hwndList,&actual);GetClientRect(state->content,&bounds);MapWindowPoints(state->content,nullptr,reinterpret_cast<POINT*>(&bounds),2);
 auto expected=CountryDropdownRect(combo);Require(IsWindowVisible(info.hwndList) && EqualRect(&actual,&expected),"native country dropdown uses bounded measured rectangle");
 Require(actual.top>=bounds.top && actual.bottom<=bounds.bottom,"country dropdown excludes footer and stays in content");
 RECT client{};GetClientRect(info.hwndList,&client);int count=static_cast<int>(SendMessageW(combo,CB_GETCOUNT,0,0)),item=static_cast<int>(SendMessageW(combo,CB_GETITEMHEIGHT,0,0));
 Require(!!(GetWindowLongW(info.hwndList,GWL_STYLE)&WS_VSCROLL)==(count*item>client.bottom),"country list scrollbar appears only for clipped rows");
 if(count*item>client.bottom){SCROLLINFO scroll{sizeof(scroll),SIF_ALL};GetScrollInfo(info.hwndList,SB_VERT,&scroll);Require(scroll.nMax>=count-1 && scroll.nPage>0 && scroll.nPage<static_cast<UINT>(count),"limited native list exposes a usable scrollbar range");SendMessageW(info.hwndList,WM_VSCROLL,SB_BOTTOM,0);Require(SendMessageW(combo,CB_GETTOPINDEX,0,0)>0,"native country scrollbar reaches lower rows");
 SendMessageW(combo,CB_SETTOPINDEX,0,0);SendMessageW(info.hwndList,WM_VSCROLL,SB_LINEDOWN,0);Require(SendMessageW(combo,CB_GETTOPINDEX,0,0)>0,"native country scrollbar line navigation works");
 SendMessageW(combo,CB_SETTOPINDEX,0,0);SendMessageW(info.hwndList,WM_VSCROLL,MAKEWPARAM(SB_THUMBPOSITION,count-1),0);Require(SendMessageW(combo,CB_GETTOPINDEX,0,0)>0,"native country scrollbar thumb navigation works");SendMessageW(combo,CB_SETTOPINDEX,0,0);}
 HDC dc=GetDC(combo);auto font=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(combo,WM_GETFONT,0,0)));
 for(int i=0;i<count;++i){wchar_t name[128]{};SendMessageW(combo,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(name));SIZE size{};GetTextExtentPoint32W(dc,name,lstrlenW(name),&size);Require(size.cx+Scale(state,4)<=client.right && size.cx<=info.rcItem.right-info.rcItem.left,"expanded and closed country names fit text width");}
 SelectObject(dc,font);ReleaseDC(combo,dc);
 SendMessageW(info.hwndList,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);if(count*item>client.bottom)Require(SendMessageW(info.hwndList,LB_GETTOPINDEX,0,0)>0,"limited list wheel scrolls its viewport");Require(SendMessageW(combo,CB_GETCURSEL,0,0)==selected,"country popup wheel scrolls without selecting");
 SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);SendMessageW(hwnd,WM_VSCROLL,MAKEWPARAM(SB_THUMBPOSITION,oldScroll),0);
}

LRESULT CALLBACK CountSourceLink(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR count) {
 if(msg==WM_NOTIFY){auto* notice=reinterpret_cast<NMHDR*>(lp);if(notice->idFrom==IDC_SOURCES && (notice->code==NM_CLICK || notice->code==NM_RETURN)){++*reinterpret_cast<int*>(count);return 0;}}
 return DefSubclassProc(hwnd,msg,wp,lp);
}

void TestSettingsBehavior() {
 for(int dpi:{96,120,144,192}) {
  RECT work{10,20,650,500},requested{-100,-200,MulDiv(940,dpi,96),MulDiv(900,dpi,96)};
  auto fit=FitSettingsWindow(requested,work);Require(fit.left>=work.left && fit.top>=work.top && fit.right<=work.right && fit.bottom<=work.bottom,"small-work-area bounds include frame without changing system settings");
 }

	for (Language language : { Language::Chinese, Language::English, Language::Japanese }) {
		BandConfig original = DefaultBandConfig(language);
		original.locationMode = LocationMode::Custom;
		original.url = BuildForecastUrl(L"35.6895", L"139.69171", L"Asia/Tokyo");
		original.language = language;
		original.delaySeconds = 73;
		original.tooltipFields = 0;
		original.currentColor = RGB(1, 2, 3);
		Require(SaveBandConfig(original), "prepare settings fixture");
		std::string before = ReadConfigBytes();
		CDeskBand band;
		HWND hwnd = ShowBandSettings(nullptr, &band, original);
		Require(hwnd != nullptr, "settings window in each supported language");
		wchar_t title[128]{};
		Require(GetWindowTextW(hwnd, title, 128) > 0, "localized window title must be initialized");
		Require(SettingsMessage(hwnd, IDC_LANGUAGE, CB_GETCOUNT, 0, 0) == 4, "system plus three languages");
		Require(band.CurrentConfig().language == language, "preview fixture starts with saved language");
		Require(!FindWindowExW(hwnd, nullptr, L"STATIC", L"WeatherBar"), "branding removed");
		Require((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0, "settings supports resizing");
		const int expectedTooltipOrder[] = { 0, 7, 1, 2, 3, 4, 5, 6, 8, 9 };
		RECT previousTooltip{};
		HWND previousCheck = nullptr;
		for (int position = 0; position < 10; ++position) {
			HWND check = SettingsControl(hwnd, IDC_TOOLTIP_FIRST + expectedTooltipOrder[position]);
			RECT rect{};
			Require(check && GetWindowRect(check, &rect), "tooltip checkbox exists");
			if (position % 2) Require(rect.top == previousTooltip.top && rect.left > previousTooltip.left, "tooltip order reads left to right");
			else if (position) Require(rect.top > previousTooltip.top && rect.left < previousTooltip.left, "tooltip order continues on next row");
			if (previousCheck) Require(GetNextDlgTabItem(hwnd, previousCheck, FALSE) == check, "tooltip tab order matches display order");
			previousTooltip = rect;
			previousCheck = check;
		}
        int activations=0;SetWindowSubclass(hwnd,CountSourceLink,99,reinterpret_cast<DWORD_PTR>(&activations));
        SetFocus(SettingsControl(hwnd,IDC_SOURCES));SendMessageW(SettingsControl(hwnd,IDC_SOURCES),WM_KEYDOWN,VK_RETURN,0);SendMessageW(SettingsControl(hwnd,IDC_SOURCES),WM_CHAR,VK_RETURN,0);SendMessageW(SettingsControl(hwnd,IDC_SOURCES),WM_KEYUP,VK_RETURN,0);
        Require(IsWindow(hwnd) && activations==1,"Enter activates attribution link rather than saving settings");RemoveWindowSubclass(hwnd,CountSourceLink,99);
		HWND url = SettingsControl(hwnd, IDC_URL);
		Require((GetWindowLongPtrW(url, GWL_STYLE) & ES_MULTILINE) != 0, "API address is multiline");
        RECT initial{},address{};GetWindowRect(hwnd,&initial);GetWindowRect(url,&address);
        VerifySettingsLayout(hwnd);
        auto* initialState=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));auto measured=MeasureSettings(hwnd,initialState);
        RECT defaultClient{};GetClientRect(hwnd,&defaultClient);auto scrollbar=GetSystemMetricsForDpi(SM_CXVSCROLL,initialState->dpi);
        Require(defaultClient.right==measured.doubleColumn+scrollbar && initialState->twoColumns,"default is the smallest measured complete two-column width");
        for(Language translated:{Language::Chinese,Language::English,Language::Japanese,language}) {
         SettingsMessage(hwnd,IDC_LANGUAGE,CB_SETCURSEL,static_cast<int>(translated),0);SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_LANGUAGE,CBN_SELCHANGE),0);
         auto width=MeasureSettings(hwnd,initialState);RECT current{};GetClientRect(hwnd,&current);
         Require(width.doubleColumn==measured.doubleColumn && current.right==defaultClient.right && initialState->twoColumns,"language switching preserves shared default width and two columns");VerifySettingsLayout(hwnd);
        }
        Require(!(GetWindowLongW(initialState->content,GWL_STYLE)&WS_VSCROLL),"default fitting content has no disabled scrollbar");
        Require(!(GetWindowLongW(SettingsControl(hwnd,IDC_RESULTS),GWL_STYLE)&(WS_VSCROLL|WS_HSCROLL)),"empty candidate list has no scrollbars");

        wchar_t linkClass[32]{};GetClassNameW(SettingsControl(hwnd,IDC_SOURCES),linkClass,32);Require(lstrcmpiW(linkClass,WC_LINK)==0,"data sources remains a keyboard accessible native text link");

        VerifyCountryDropdown(hwnd);
        RECT viewport{};GetClientRect(initialState->content,&viewport);
        HWND countryCombo=SettingsControl(hwnd,IDC_COUNTRY);RECT countryRect{};GetWindowRect(countryCombo,&countryRect);MapWindowPoints(nullptr,initialState->content,reinterpret_cast<POINT*>(&countryRect),2);
        SetWindowPos(countryCombo,nullptr,countryRect.left,viewport.bottom-(countryRect.bottom-countryRect.top)-8,0,0,SWP_NOSIZE|SWP_NOZORDER);
        VerifyCountryDropdown(hwnd);auto upward=CountryDropdownRect(countryCombo);GetWindowRect(countryCombo,&countryRect);Require(upward.bottom==countryRect.top,"country popup can use space above the control");LayoutSettings(hwnd,initialState);
        SettingsMessage(hwnd,IDC_COUNTRY,CB_SHOWDROPDOWN,TRUE,0);SetWindowPos(hwnd,nullptr,initial.left+1,initial.top+1,0,0,SWP_NOSIZE|SWP_NOZORDER);
        Require(!SettingsMessage(hwnd,IDC_COUNTRY,CB_GETDROPPEDSTATE,0,0),"moving window closes detached country popup");SetWindowPos(hwnd,nullptr,initial.left,initial.top,0,0,SWP_NOSIZE|SWP_NOZORDER);

        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
        Require(initial.left>=monitor.rcWork.left && initial.top>=monitor.rcWork.top && initial.right<=monitor.rcWork.right && initial.bottom<=monitor.rcWork.bottom,"default frame stays inside monitor work area");

        MINMAXINFO bounds{};SendMessageW(hwnd,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&bounds));
        Require(bounds.ptMinTrackSize.y<initial.bottom-initial.top,"minimum height allows content scrolling");
		RECT dropdownBefore{}, dropdownAfter{};
		SettingsMessage(hwnd, IDC_LANGUAGE, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropdownBefore));
		SetWindowPos(hwnd, nullptr, 0, 0, initial.right - initial.left + 100, initial.bottom - initial.top + 80, SWP_NOMOVE | SWP_NOZORDER);
		SettingsMessage(hwnd, IDC_LANGUAGE, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropdownAfter));
		Require(dropdownAfter.bottom - dropdownAfter.top == dropdownBefore.bottom - dropdownBefore.top, "resizing preserves dropdown list height");
		RECT larger{}; GetWindowRect(url, &larger);
		Require(larger.right - larger.left == address.right - address.left + 100, "API field expands with window");
		SetWindowPos(hwnd, nullptr, 0, 0, initial.right - initial.left, initial.bottom - initial.top, SWP_NOMOVE | SWP_NOZORDER);
		RECT reset{}, save{}, cancel{};
		GetWindowRect(SettingsControl(hwnd, IDC_RESET), &reset);
		GetWindowRect(SettingsControl(hwnd, IDC_SAVE), &save);
		GetWindowRect(SettingsControl(hwnd, IDC_CANCEL), &cancel);
		Require(reset.right < save.left && save.right < cancel.left, "reset left; save and cancel right without overlap");
		Button_SetCheck(SettingsControl(hwnd, IDC_SHOW_ICON), BST_UNCHECKED);
		SendMessageW(hwnd, WM_COMMAND, IDC_SHOW_ICON, 0);
		Require(!band.CurrentConfig().showIcon && ReadConfigBytes() == before, "appearance previews without saving");
		SettingsMessage(hwnd, IDC_LANGUAGE, CB_SETCURSEL, static_cast<int>(Language::English), 0);
		SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_LANGUAGE, CBN_SELCHANGE), 0);
		GetWindowTextW(hwnd, title, 128);
		Require(std::wstring(title) == L"Weather settings" && band.CurrentConfig().language == Language::English, "language immediately changes the same window and band");
		Require(ReadConfigBytes() == before, "language preview is not persisted");
		Button_SetCheck(SettingsControl(hwnd, IDC_TOOLTIP_FIRST + 7), BST_CHECKED);
		SendMessageW(hwnd, WM_COMMAND, IDC_TOOLTIP_FIRST + 7, 0);
		Require(band.CurrentConfig().tooltipFields == 128 && ReadConfigBytes() == before, "tooltip selection previews without writing");

  for (bool refresh : {false,true}) for (bool status : {false,true}) {
   Button_SetCheck(SettingsControl(hwnd,IDC_TOOLTIP_FIRST+8),refresh?BST_CHECKED:BST_UNCHECKED);
   Button_SetCheck(SettingsControl(hwnd,IDC_TOOLTIP_FIRST+9),status?BST_CHECKED:BST_UNCHECKED);
   SendMessageW(hwnd,WM_COMMAND,IDC_TOOLTIP_FIRST+8,0);
   Require(band.CurrentConfig().tooltipRefresh==refresh && band.CurrentConfig().tooltipStatus==status && ReadConfigBytes()==before,"footer changes preview only in each language");
  }
		Button_SetCheck(SettingsControl(hwnd, IDC_TOOLTIP), BST_UNCHECKED);
		SendMessageW(hwnd, WM_COMMAND, IDC_TOOLTIP, 0);
		Require(!band.CurrentConfig().tooltipEnabled && !IsWindowEnabled(SettingsControl(hwnd, IDC_TOOLTIP_FIRST)), "disabled tooltip disables optional controls");
		SettingsMessage(hwnd, IDC_LANGUAGE, CB_SETCURSEL, static_cast<int>(language), 0);
		SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_LANGUAGE, CBN_SELCHANGE), 0);
		SendMessageW(hwnd, WM_COMMAND, IDC_CUR_COLOR, 0);
		auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		HWND palette = state->palette;
		Require(palette && IsWindow(palette), "color button opens nonmodal palette");
		SettingsMessage(palette, 300, TBM_SETPOS, TRUE, 12);
		SettingsMessage(palette, 301, TBM_SETPOS, TRUE, 34);
		SettingsMessage(palette, 302, TBM_SETPOS, TRUE, 56);
		SendMessageW(palette, WM_HSCROLL, 0, 0);
		Require(band.CurrentConfig().currentColor == RGB(12,34,56) && ReadConfigBytes() == before, "arbitrary RGB palette changes preview only");
		SendMessageW(palette, WM_ACTIVATE, WA_INACTIVE, 0);
		Require(!IsWindow(palette) && !state->palette, "palette hides on outside activation");
		BandConfig draft = original;
		std::wstring error;
		std::wstring wrapped = original.url;
		size_t ampersand = wrapped.find(L'&');
		Require(ampersand != std::wstring::npos, "URL fixture contains query parameters");
		wrapped.insert(ampersand + 1, L"\r\n");
		SetControlText(hwnd, IDC_URL, wrapped);
		Require(ReadCustomLocation(hwnd, original, draft) && draft.url == original.url, "pasted URL line breaks normalized");
		SendMessageW(url, WM_KEYDOWN, VK_RETURN, 0);
		Require(IsWindow(hwnd), "Enter in API field does not save or close");
		SetControlText(hwnd, IDC_URL, original.url);
		SendMessageW(hwnd, WM_COMMAND, IDC_RESET, 0);
		Require(ReadDraft(hwnd, draft, language, error), "read reset appearance");
		Require(draft.language == language && draft.url == original.url && draft.delaySeconds == 73, "reset preserves language, URL and stagger");
        Require(draft.currentLevel==SizeLevel::Large && draft.iconLevel==SizeLevel::Large && draft.maxLevel==SizeLevel::Large && draft.minLevel==SizeLevel::Large && draft.gapIconLevel==SizeLevel::Large && draft.gapExtremeLevel==SizeLevel::Large,"appearance reset uses all six maximum levels");

		Require(ReadConfigBytes() == before, "reset is not persisted until save");
		SendMessageW(hwnd, WM_COMMAND, IDC_CANCEL, 0);
		Require(!IsWindow(hwnd) && ReadConfigBytes() == before, "cancel discards draft");
		Require(band.CurrentConfig().tooltipEnabled == original.tooltipEnabled && band.CurrentConfig().tooltipFields == original.tooltipFields && band.CurrentConfig().tooltipRefresh == original.tooltipRefresh && band.CurrentConfig().tooltipStatus == original.tooltipStatus, "cancel restores tooltip settings");
		Require(band.CurrentConfig().language == language && band.CurrentConfig().showIcon == original.showIcon && band.CurrentConfig().currentColor == original.currentColor, "cancel restores original in-memory display");
		hwnd = ShowBandSettings(nullptr, &band, original);
		Button_SetCheck(SettingsControl(hwnd, IDC_TOOLTIP_FIRST), BST_CHECKED);
		SettingsMessage(hwnd, IDC_LANGUAGE, CB_SETCURSEL, static_cast<int>(Language::Japanese), 0);
		SendMessageW(hwnd, WM_COMMAND, IDC_SAVE, 0);
		Require(!IsWindow(hwnd) && LoadBandConfig().language == Language::Japanese, "save persists language and closes window");
		Require(LoadBandConfig().tooltipEnabled && LoadBandConfig().tooltipFields == 1, "save persists tooltip selection");
		Require(band.CurrentConfig().language == Language::Japanese, "closing after save retains committed preview");
		hwnd = ShowBandSettings(nullptr, &band, band.CurrentConfig());
		Button_SetCheck(SettingsControl(hwnd, IDC_TOOLTIP_FIRST + 7), BST_CHECKED);
		SendMessageW(hwnd, WM_COMMAND, IDC_TOOLTIP_FIRST + 7, 0);
		Button_SetCheck(SettingsControl(hwnd, IDC_SHOW_ICON), BST_UNCHECKED);
		SendMessageW(hwnd, WM_COMMAND, IDC_SHOW_ICON, 0);
		SendMessageW(hwnd, WM_CLOSE, 0, 0);
		Require(band.CurrentConfig().showIcon && band.CurrentConfig().tooltipFields == 1, "title-bar close also restores preview and future selection");
		BandConfig saved = band.CurrentConfig();
		auto beforeLocation = ReadConfigBytes();
		BandConfig location = saved; location.locationMode = LocationMode::Coordinates;
		location.latitude = L"27.7172"; location.longitude = L"85.324"; location.locationName = L"27.7172, 85.324"; location.locationTimezone = L"Asia/Kathmandu"; location.searchCountry = L"";
		location.url = BuildForecastUrl(location.latitude, location.longitude, L"auto");
		ForecastCache forecast; forecast.timezoneValid = true; forecast.utcOffsetSeconds = 20700; forecast.timezone = location.locationTimezone;
		const int locationActions[] = { IDC_CANCEL, 0, IDC_SAVE };
		for (int action : locationActions) {
			hwnd = ShowBandSettings(nullptr, &band, saved);
			auto* locationState = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			locationState->draft = location;
			SettingsMessage(hwnd, IDC_COUNTRY, CB_SETCURSEL, 0, 0);
			band.ApplyPreviewConfig(location, &forecast);
			Require(ReadConfigBytes() == beforeLocation, "location preview does not persist configuration");
			if (action) SendMessageW(hwnd, WM_COMMAND, action, 0);
			else SendMessageW(hwnd, WM_CLOSE, 0, 0);
			if (action == IDC_SAVE) {
				auto loaded = LoadBandConfig();
				Require(loaded.locationMode == LocationMode::Coordinates && loaded.url == location.url && loaded.locationName == location.locationName && loaded.searchCountry.empty() && band.UtcOffsetSeconds() == 20700, "save persists complete automatic location and keeps validated timezone");
			} else Require(band.CurrentConfig().locationMode == saved.locationMode && band.CurrentConfig().url == saved.url && ReadConfigBytes() == beforeLocation, "cancel and close restore original mode location and config file");
		}

	}
}

void TestInlineLocationSettings() {
 for (Language language : {Language::Chinese, Language::English, Language::Japanese}) {
  BandConfig config = DefaultBandConfig(); config.language = language;
  config.locationMode = LocationMode::Search; config.locationName = L"杭州";
  config.url = BuildForecastUrl(L"30.27",L"120.15",L"Asia/Shanghai"); config.searchCountry = L"FR";
  Require(SaveBandConfig(config), "prepare inline settings"); CDeskBand band;
  ForecastCache cache; cache.timezoneValid = true; cache.timezone = L"Asia/Shanghai"; cache.utcOffsetSeconds = 28800;
  band.ApplyPreviewConfig(config,&cache);
  HWND hwnd = ShowBandSettings(nullptr,&band,config);
  auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  Require(!FindWindowW(L"WeatherBarLocation",nullptr),"no independent location window");
  Require(!state->advanced && IsWindowVisible(SettingsControl(hwnd,IDC_RESULTS)) && !IsWindowVisible(SettingsControl(hwnd,IDC_URL)),"default search layout keeps compact result list");
  Require(SelectedCountry(hwnd)==L"FR","country preference retained independently from language");
  auto defaultHint=ControlText(hwnd,IDC_LOCATION_STATUS);
  int collapsedEnd=state->basicHeight; Require(collapsedEnd>0 && collapsedEnd<=Scale(state,440),"default weather section has no large empty list");
  auto before=ReadConfigBytes(); auto originalUrl=band.CurrentConfig().url;
  SendMessageW(hwnd,WM_COMMAND,IDC_ADVANCED,0);
  Require(state->advanced && IsWindowVisible(SettingsControl(hwnd,IDC_LATITUDE)) && !IsWindowVisible(SettingsControl(hwnd,IDC_URL)) && state->basicHeight>collapsedEnd,"expand coordinates and move lower sections");
  SetControlText(hwnd,IDC_LATITUDE,L"-33.123456789"); SetControlText(hwnd,IDC_LONGITUDE,L"151.2");
  SettingsMessage(hwnd,IDC_ADVANCED_MODE,CB_SETCURSEL,1,0); SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_ADVANCED_MODE,CBN_SELCHANGE),0);
  Require(IsWindowVisible(SettingsControl(hwnd,IDC_URL)) && !IsWindowVisible(SettingsControl(hwnd,IDC_LATITUDE)) && band.CurrentConfig().url==originalUrl,"switching editor does not apply source");
  SetControlText(hwnd,IDC_URL,L"https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&forecast_days=2");

  BandConfig custom; Require(ReadCustomLocation(hwnd,state->draft,custom) && custom.url.find(L"forecast_days=2")!=std::wstring::npos,"advanced custom URL retains unrelated parameters");
  SetFocus(SettingsControl(hwnd,IDC_URL)); SendMessageW(hwnd,WM_COMMAND,IDC_ADVANCED,0);
  Require(!IsWindowVisible(SettingsControl(hwnd,IDC_URL)) && GetFocus()!=SettingsControl(hwnd,IDC_URL) && state->basicHeight==collapsedEnd,"collapse reclaims space and moves hidden focus");
  HWND first=SettingsControl(hwnd,IDC_ADVANCED), next=first;
  for(int i=0;i<80;++i) { next=GetNextDlgTabItem(hwnd,next,FALSE); if(next==first) break; Require(next!=SettingsControl(hwnd,IDC_URL) && next!=SettingsControl(hwnd,IDC_LATITUDE),"hidden editors excluded from tab order"); }
  SendMessageW(SettingsControl(hwnd,IDC_ADVANCED),WM_KEYDOWN,VK_RETURN,0);
  Require(state->advanced && ControlText(hwnd,IDC_URL)==custom.url,"Enter expands and retains custom input");
  SetFocus(SettingsControl(hwnd,IDC_ADVANCED));
  SendMessageW(SettingsControl(hwnd,IDC_ADVANCED),WM_KEYDOWN,VK_SPACE,0); SendMessageW(SettingsControl(hwnd,IDC_ADVANCED),WM_KEYUP,VK_SPACE,0);
  Require(!state->advanced,"Space toggles the advanced header");
  SendMessageW(hwnd,WM_COMMAND,IDC_ADVANCED,0);
  SettingsMessage(hwnd,IDC_ADVANCED_MODE,CB_SETCURSEL,0,0); SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_ADVANCED_MODE,CBN_SELCHANGE),0);
  Require(ControlText(hwnd,IDC_LATITUDE)==L"-33.123456789" && ControlText(hwnd,IDC_LONGITUDE)==L"151.2","switching preserves coordinate input precision");
  SendMessageW(SettingsControl(hwnd,IDC_LATITUDE),WM_KEYDOWN,VK_RETURN,0);
  Require(IsWindow(hwnd) && ReadConfigBytes()==before,"Enter in an unapplied editor cannot save settings");
  SetControlText(hwnd,IDC_LATITUDE,L"NaN"); SendMessageW(hwnd,WM_COMMAND,IDC_APPLY_COORDINATES,0);
  Require(!state->locationWeather && band.CurrentConfig().url==originalUrl,"invalid coordinates do not request or replace location");
  Require(!ControlText(hwnd,IDC_COORD_ERROR).empty() && ControlText(hwnd,IDC_LOCATION_STATUS)==defaultHint,"coordinate errors stay next to coordinate inputs");
  state->search=std::make_shared<LocationRequest>(); auto old=state->search;
  SetControlText(hwnd,IDC_QUERY,L"杭州"); Require(!state->search,"editing query rejects old response");
  old->status=LocationStatus::Ready; old->done=true; PollLocation(hwnd,state); Require(state->candidates.empty(),"obsolete query cannot overwrite candidates");
  auto candidate=ParseLocations(R"({"results":[{"name":"杭州","country":"中国","admin1":"浙江","timezone":"Asia/Shanghai","latitude":30.27,"longitude":120.15}]})");
  state->search=std::make_shared<LocationRequest>(); state->search->candidates=candidate; state->search->status=LocationStatus::Ready; state->search->done=true; PollLocation(hwnd,state);
  Require(IsWindowVisible(SettingsControl(hwnd,IDC_RESULTS)) && SettingsMessage(hwnd,IDC_RESULTS,LB_GETCURSEL,0,0)==LB_ERR && !IsWindowEnabled(SettingsControl(hwnd,IDC_APPLY_LOCATION)),"results shown compactly without automatic selection");
  state->locationWeather=std::make_shared<LocationRequest>(); state->locationWeather->status=LocationStatus::NetworkError; state->locationWeather->done=true; PollLocation(hwnd,state);
  Require(band.CurrentConfig().url==originalUrl,"failed weather preserves effective source");
  RECT locationBefore{};GetWindowRect(SettingsControl(hwnd,IDC_CURRENT_LOCATION),&locationBefore);
  for(auto status : {LocationStatus::Ready,LocationStatus::NetworkError,LocationStatus::ParseError}) {
   ClearLocationSearch(hwnd,state); state->search=std::make_shared<LocationRequest>();state->search->status=status;state->search->done=true;PollLocation(hwnd,state);
   Require(IsWindowVisible(SettingsControl(hwnd,IDC_RESULTS)) && !ControlText(hwnd,IDC_LOCATION_STATUS).empty(),"empty and error results use compact status only");
   RECT locationAfter{};GetWindowRect(SettingsControl(hwnd,IDC_CURRENT_LOCATION),&locationAfter);Require(EqualRect(&locationBefore,&locationAfter),"search status never shifts location information");
  }

  for (HWND child=GetWindow(reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA))->content,GW_CHILD); child; child=GetWindow(child,GW_HWNDNEXT)) {
   wchar_t klass[32]{}; GetClassNameW(child,klass,32); if(lstrcmpiW(klass,L"COMBOBOX")!=0) continue;
   LRESULT selected=SendMessageW(child,CB_GETCURSEL,0,0); auto location=band.CurrentConfig().url; auto fields=band.CurrentConfig().tooltipFields;
   SendMessageW(child,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
   SetFocus(child); SendMessageW(child,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);
   SendMessageW(child,CB_SHOWDROPDOWN,TRUE,0); COMBOBOXINFO info{sizeof(info)}; GetComboBoxInfo(child,&info);
   SendMessageW(child,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0); SendMessageW(info.hwndList,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);
   Require(SendMessageW(child,CB_GETCURSEL,0,0)==selected && band.CurrentConfig().url==location && band.CurrentConfig().tooltipFields==fields,"wheel on combo and dropdown cannot select or preview");
   SendMessageW(child,CB_SHOWDROPDOWN,FALSE,0);
   SendMessageW(child,WM_KEYDOWN,VK_DOWN,0); SendMessageW(child,WM_KEYUP,VK_DOWN,0);
   Require(SendMessageW(child,CB_GETCURSEL,0,0)!=selected || selected==SendMessageW(child,CB_GETCOUNT,0,0)-1,"keyboard choice still works");
   SendMessageW(child,CB_SETCURSEL,selected,0);
  }
  state->scroll=0; ReadAppearance(hwnd,state->draft); band.ApplyPreviewConfig(state->draft); RefreshLanguage(hwnd,state); LayoutSettings(hwnd,state);
  for(int dpi:{96,120,144,192}) {
   RECT suggested{0,0,MulDiv(960,dpi,96),MulDiv(780,dpi,96)};
   SendMessageW(hwnd,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
   VerifyCountryDropdown(hwnd);
   auto m=MeasureSettings(hwnd,state);
   for(int delta:{-1,0,1}) {
    // Synthetic WM_DPICHANGED changes fonts, not the OS scrollbar/frame DPI. Measure those actual pixels.
    RECT outer{},frame{},viewport{};GetWindowRect(hwnd,&outer);GetClientRect(hwnd,&frame);GetClientRect(state->content,&viewport);
    int chrome=outer.right-outer.left-frame.right+SettingsScrollbarWidth(state);
    SetWindowPos(hwnd,nullptr,0,0,m.doubleColumn+chrome+delta,Scale(state,500)+outer.bottom-outer.top-frame.bottom,SWP_NOMOVE|SWP_NOZORDER);
    Require(state->twoColumns==(delta>=0),"column breakpoint has no unexplained extra default width");VerifySettingsLayout(hwnd);
   }

   for(bool wide:{false,true}) {
    int contentWidth=wide?m.doubleColumn:m.single;
    RECT size{0,0,contentWidth+GetSystemMetricsForDpi(SM_CXVSCROLL,dpi),Scale(state,500)};AdjustWindowRectExForDpi(&size,GetWindowLongW(hwnd,GWL_STYLE),FALSE,0,dpi);
    SetWindowPos(hwnd,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER);
    Require(state->twoColumns==wide,"measured width changes card column count");
    for(int mode:{0,1})for(bool expanded:{false,true}) {
     SettingsMessage(hwnd,IDC_ADVANCED_MODE,CB_SETCURSEL,mode,0);state->advanced=expanded;LayoutSettings(hwnd,state);VerifySettingsLayout(hwnd);
     RECT footerBefore{},footerAfter{};GetWindowRect(SettingsControl(hwnd,IDC_SAVE),&footerBefore);
     RECT beforeLast{},afterLast{};GetWindowRect(SettingsControl(hwnd,IDC_RELEASES),&beforeLast);int oldScroll=state->scroll;
     SendMessageW(hwnd,WM_VSCROLL,SB_BOTTOM,0);GetWindowRect(SettingsControl(hwnd,IDC_SAVE),&footerAfter);
     GetWindowRect(SettingsControl(hwnd,IDC_RELEASES),&afterLast);Require(afterLast.top-beforeLast.top==oldScroll-state->scroll,"scroll moves even controls originally outside the viewport");
     VerifySettingsLayout(hwnd);
     Require(EqualRect(&footerBefore,&footerAfter),"scrolling never moves footer");
     SendMessageW(hwnd,WM_VSCROLL,SB_TOP,0);
    }
   }
  }
  HWND originalPrompt=SettingsControl(hwnd,IDC_LOCATION_STATUS);int prompts=0;
  for(HWND child=GetWindow(state->content,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(GetDlgCtrlID(child)==IDC_LOCATION_STATUS)++prompts;
  Require(prompts==1,"exactly one prompt control exists");
  for(int i=0;i<12;++i){
   SendMessageW(hwnd,WM_VSCROLL,SB_TOP,0);UpdateWindow(originalPrompt);
   auto measured=MeasureSettings(hwnd,state);RECT size{0,0,measured.single+GetSystemMetricsForDpi(SM_CXVSCROLL,state->dpi)+(i%2?0:Scale(state,100)),Scale(state,500)};AdjustWindowRectExForDpi(&size,GetWindowLongW(hwnd,GWL_STYLE),FALSE,0,state->dpi);
   SetWindowPos(hwnd,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER);
   RECT dirty{},area{};GetClientRect(originalPrompt,&area);Require(GetUpdateRect(originalPrompt,&dirty,FALSE) && dirty.left==0 && dirty.right==area.right,"every resize repaints the entire prompt child");
   UpdateWindow(originalPrompt);SetControlText(hwnd,IDC_LOCATION_STATUS,i%2?L"Preview applied; Save to keep.":defaultHint);LayoutSettings(hwnd,state);
   Require(SettingsControl(hwnd,IDC_LOCATION_STATUS)==originalPrompt,"status and resize reuse one HWND");
   SendMessageW(hwnd,WM_VSCROLL,i%2?SB_TOP:SB_BOTTOM,0);VerifySettingsLayout(hwnd);
  }
  state->advanced=true;SettingsMessage(hwnd,IDC_ADVANCED_MODE,CB_SETCURSEL,1,0);LayoutSettings(hwnd,state);
  HWND list=SettingsControl(hwnd,IDC_RESULTS);bool columnsBefore=state->twoColumns;
  SettingsMessage(hwnd,IDC_RESULTS,LB_RESETCONTENT,0,0);
  for(int i=0;i<10;++i)SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Fixed candidate"));
  SendMessageW(list,LB_SETHORIZONTALEXTENT,Scale(state,4000),0);
  Require((GetWindowLongW(list,GWL_STYLE)&(WS_VSCROLL|WS_HSCROLL))==(WS_VSCROLL|WS_HSCROLL),"overflowing candidates expose only their needed scrollbars");
  SendMessageW(list,LB_SETCURSEL,9,0);SendMessageW(list,WM_KEYDOWN,VK_UP,0);Require(SendMessageW(list,LB_GETCURSEL,0,0)==8,"candidate list keyboard scrolling is retained");
  ClearLocationSearch(hwnd,state);
  Require(!(GetWindowLongW(list,GWL_STYLE)&(WS_VSCROLL|WS_HSCROLL)) && state->twoColumns==columnsBefore,"cleared candidates hide both bars without changing page columns");
  auto urlDraft=ControlText(hwnd,IDC_URL);SetControlText(hwnd,IDC_URL,L"https://api.open-meteo.com/");UpdateUrlScrollbar(hwnd);
  Require(!(GetWindowLongW(SettingsControl(hwnd,IDC_URL),GWL_STYLE)&WS_VSCROLL),"short URL needs no vertical scrollbar");
  SetControlText(hwnd,IDC_URL,std::wstring(4000,L'x'));UpdateUrlScrollbar(hwnd);
  Require(GetWindowLongW(SettingsControl(hwnd,IDC_URL),GWL_STYLE)&WS_VSCROLL,"long wrapped URL can scroll");
  SetControlText(hwnd,IDC_URL,L"https://api.open-meteo.com/");UpdateUrlScrollbar(hwnd);Require(!(GetWindowLongW(SettingsControl(hwnd,IDC_URL),GWL_STYLE)&WS_VSCROLL),"shortening URL hides its scrollbar");
  for(size_t length=150;length<500;length+=17){
   SetControlText(hwnd,IDC_URL,std::wstring(length,L'x'));UpdateUrlScrollbar(hwnd);bool visible=(GetWindowLongW(SettingsControl(hwnd,IDC_URL),GWL_STYLE)&WS_VSCROLL)!=0;
   HDC dc=GetDC(SettingsControl(hwnd,IDC_URL));auto previousUrlFont=SelectObject(dc,reinterpret_cast<HFONT>(SettingsMessage(hwnd,IDC_URL,WM_GETFONT,0,0)));TEXTMETRICW font{};GetTextMetricsW(dc,&font);SelectObject(dc,previousUrlFont);ReleaseDC(SettingsControl(hwnd,IDC_URL),dc);RECT format{};SettingsMessage(hwnd,IDC_URL,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
   Require(visible || SettingsMessage(hwnd,IDC_URL,EM_GETLINECOUNT,0,0)*font.tmHeight<=format.bottom-format.top,"hidden URL scrollbar never leaves wrapped overflow inaccessible");
   for(int repeat=0;repeat<3;++repeat){UpdateUrlScrollbar(hwnd);Require(!!(GetWindowLongW(SettingsControl(hwnd,IDC_URL),GWL_STYLE)&WS_VSCROLL)==visible,"URL wrap boundary never oscillates scrollbar visibility");}
  }
  SetControlText(hwnd,IDC_URL,urlDraft);UpdateUrlScrollbar(hwnd);
  Require(band.CurrentConfig().url==originalUrl && ReadConfigBytes()==before,"folding and editor changes never persist or apply");
  state->pendingLocation=custom;state->locationWeather=std::make_shared<LocationRequest>();state->locationWeather->status=LocationStatus::Ready;state->locationWeather->forecast.timezoneValid=true;state->locationWeather->forecast.timezone=L"Asia/Kathmandu";state->locationWeather->forecast.utcOffsetSeconds=20700;state->locationWeather->done=true;PollLocation(hwnd,state);
  Require(band.CurrentConfig().locationMode==LocationMode::Custom && band.UtcOffsetSeconds()==20700,"custom application uses response timezone");
  state->update=std::make_shared<UpdateCheck>();state->update->status=UpdateStatus::Failed;state->update->errorCode=12029;state->update->done=true;
  SendMessageW(hwnd,WM_TIMER,kUpdateTimer,0);Require(IsWindowVisible(SettingsControl(hwnd,IDC_UPDATE_STATUS)) && ControlText(hwnd,IDC_UPDATE_STATUS).find(L"12029")!=std::wstring::npos,"fixed update error expands about card without network");VerifySettingsLayout(hwnd);
  SendMessageW(hwnd,WM_CLOSE,0,0);Require(band.CurrentConfig().url==originalUrl && band.CurrentConfig().locationMode==LocationMode::Search,"close restores complete previous location");
 }
}

void TestEmptySettings() {
 for (Language language : {Language::Chinese,Language::English,Language::Japanese}) {
  auto config=DefaultBandConfig(language);Require(SaveBandConfig(config),"save isolated empty settings");CDeskBand band;
  HWND hwnd=ShowBandSettings(nullptr,&band,config);
  Require(ControlText(hwnd,IDC_URL).empty() && ControlText(hwnd,IDC_LATITUDE).empty() && ControlText(hwnd,IDC_LONGITUDE).empty() && ControlText(hwnd,IDC_QUERY).empty(),"fresh editors are empty");
  Require(SelectedCountry(hwnd).empty() && IsWindowVisible(SettingsControl(hwnd,IDC_RESULTS)) && !IsWindowEnabled(SettingsControl(hwnd,IDC_APPLY_LOCATION)),"fresh country is Any with disabled persistent action");
  auto* state=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  Require(!state->advanced && !HasConfiguredLocation(state->draft),"fresh mode is unselected and collapsed");
  wchar_t klass[32]{};GetClassNameW(SettingsControl(hwnd,IDC_LOCATION_STATUS),klass,32);
  Require(lstrcmpiW(klass,L"STATIC")==0 && !(GetWindowLongPtrW(SettingsControl(hwnd,IDC_LOCATION_STATUS),GWL_STYLE)&WS_VSCROLL),"all search statuses are plain single-line text");
  SettingsMessage(hwnd,IDC_LANGUAGE,CB_SETCURSEL,static_cast<int>(Language::English),0);SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_LANGUAGE,CBN_SELCHANGE),0);
  Require(!HasConfiguredLocation(band.CurrentConfig()),"language change does not create a location");
  state->search=std::make_shared<LocationRequest>();state->search->status=LocationStatus::Ready;state->search->done=true;PollLocation(hwnd,state);
  Require(!ControlText(hwnd,IDC_LOCATION_STATUS).empty() && IsWindowVisible(SettingsControl(hwnd,IDC_LOCATION_STATUS)),"no results updates the one visible status");
  SendMessageW(hwnd,WM_COMMAND,IDC_SAVE,0);Require(!HasConfiguredLocation(LoadBandConfig()),"save appearance without selecting location");
 }
}

struct ScrollPaintTrace {int erase=0,paint=0,moves=0;};
LRESULT CALLBACK TraceScrollPaint(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
 auto* trace=reinterpret_cast<ScrollPaintTrace*>(data);if(msg==WM_ERASEBKGND)++trace->erase;if(msg==WM_PAINT)++trace->paint;if(msg==WM_WINDOWPOSCHANGING)++trace->moves;
 return DefSubclassProc(hwnd,msg,wp,lp);
}
void PumpSettingsPaint(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}

// Capture the real native window used by the regression fixture; no mock rendering or personal configuration.
void CaptureSettings(const std::wstring& directory) {
 std::filesystem::create_directories(directory);
 auto save=[&](HWND hwnd,const std::wstring& name){
  RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN|RDW_FRAME);
  RECT bounds{};GetWindowRect(hwnd,&bounds);int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top,stride=(width*3+3)&~3;
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=24;info.bmiHeader.biCompression=BI_RGB;
  HDC screen=GetDC(hwnd),dc=CreateCompatibleDC(screen);void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,bitmap);
  MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
  bool captured=PrintWindow(hwnd,dc,0)!=FALSE;
  HWND country=SettingsControl(hwnd,IDC_COUNTRY);
  if(SendMessageW(country,CB_GETDROPPEDSTATE,0,0)){
   COMBOBOXINFO combo{sizeof(combo)};GetComboBoxInfo(country,&combo);RECT popup{};GetWindowRect(combo.hwndList,&popup);
   HDC listDc=CreateCompatibleDC(screen);HBITMAP listBitmap=CreateCompatibleBitmap(screen,popup.right-popup.left,popup.bottom-popup.top);auto previous=SelectObject(listDc,listBitmap);
   captured=PrintWindow(combo.hwndList,listDc,0)!=FALSE && captured;BitBlt(dc,popup.left-bounds.left,popup.top-bounds.top,popup.right-popup.left,popup.bottom-popup.top,listDc,0,0,SRCCOPY);
   SelectObject(listDc,previous);DeleteObject(listBitmap);DeleteDC(listDc);
  }
  GdiFlush();HRESULT hr=E_FAIL;
  if(captured){CComPtr<IWICImagingFactory> factory;CComPtr<IWICStream> stream;CComPtr<IWICBitmapEncoder> encoder;CComPtr<IWICBitmapFrameEncode> frame;
   hr=factory.CoCreateInstance(CLSID_WICImagingFactory);
   auto path=std::filesystem::path(directory)/(name+L".png");
   if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
   if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
   if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
   if(SUCCEEDED(hr))hr=encoder->Initialize(stream,WICBitmapEncoderNoCache);
   if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);
   if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);
   if(SUCCEEDED(hr))hr=frame->SetSize(width,height);
   WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
   if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
   if(SUCCEEDED(hr) && format!=GUID_WICPixelFormat24bppBGR)hr=E_FAIL;
   if(SUCCEEDED(hr))hr=frame->WritePixels(height,stride,stride*height,static_cast<BYTE*>(pixels));
   if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();
  }
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(hwnd,screen);
  Require(captured && SUCCEEDED(hr),"native PNG screenshot export");
  std::wcout<<name<<L" "<<width<<L"x"<<height<<L"\n";
 };
 for(Language language:{Language::Chinese,Language::English,Language::Japanese}) {
  auto config=DefaultBandConfig(language);CDeskBand band;HWND hwnd=ShowBandSettings(nullptr,&band,config);
  auto* state=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  std::wstring prefix=language==Language::Chinese?L"zh":language==Language::English?L"en":L"ja";
  save(hwnd,prefix+L"-default");
  VerifyCountryDropdown(hwnd);SettingsMessage(hwnd,IDC_COUNTRY,CB_SHOWDROPDOWN,TRUE,0);save(hwnd,prefix+L"-countries");SettingsMessage(hwnd,IDC_COUNTRY,CB_SHOWDROPDOWN,FALSE,0);
  RECT oldHintArea{};GetClientRect(SettingsControl(hwnd,IDC_LOCATION_STATUS),&oldHintArea);
  auto m=MeasureSettings(hwnd,state);RECT desired{0,0,m.single+GetSystemMetricsForDpi(SM_CXVSCROLL,state->dpi),Scale(state,600)};
  AdjustWindowRectExForDpi(&desired,GetWindowLongW(hwnd,GWL_STYLE),FALSE,0,state->dpi);
  SetWindowPos(hwnd,nullptr,0,0,desired.right-desired.left,desired.bottom-desired.top,SWP_NOMOVE|SWP_NOZORDER);
  HWND hint=SettingsControl(hwnd,IDC_LOCATION_STATUS);RECT invalid{},area{};GetClientRect(hint,&area);
  Require(GetUpdateRect(hint,&invalid,FALSE) && invalid.left==0 && invalid.right==area.right,"resized prompt invalidates the complete child surface");
  std::wcout<<prefix<<L" hint width="<<area.right<<L" pending paint="<<GetUpdateRect(hint,&invalid,FALSE)<<L" dirty="<<invalid.left<<L","<<invalid.right<<L"\n";
  HDC hintDc=GetDC(hint);auto hintFont=SelectObject(hintDc,reinterpret_cast<HFONT>(SendMessageW(hint,WM_GETFONT,0,0)));auto hintText=ControlText(hwnd,IDC_LOCATION_STATUS);SIZE hintSize{};GetTextExtentPoint32W(hintDc,hintText.c_str(),static_cast<int>(hintText.size()),&hintSize);SelectObject(hintDc,hintFont);ReleaseDC(hint,hintDc);
  std::wcout<<prefix<<L" old width="<<oldHintArea.right<<L" text width="<<hintSize.cx<<L" old text="<<(oldHintArea.right-hintSize.cx)/2<<L","<<(oldHintArea.right+hintSize.cx)/2<<L"\n";
  save(hwnd,prefix+L"-narrow");
  VerifyCountryDropdown(hwnd);SettingsMessage(hwnd,IDC_COUNTRY,CB_SHOWDROPDOWN,TRUE,0);save(hwnd,prefix+L"-countries-limited");SettingsMessage(hwnd,IDC_COUNTRY,CB_SHOWDROPDOWN,FALSE,0);
  ScrollPaintTrace viewportTrace,footerTrace,childTrace;
  RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);PumpSettingsPaint();
  SetWindowSubclass(state->content,TraceScrollPaint,901,reinterpret_cast<DWORD_PTR>(&viewportTrace));SetWindowSubclass(hwnd,TraceScrollPaint,902,reinterpret_cast<DWORD_PTR>(&footerTrace));
  for(HWND child=GetWindow(state->content,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SetWindowSubclass(child,TraceScrollPaint,903,reinterpret_cast<DWORD_PTR>(&childTrace));
  for(int tick=0;tick<6;++tick){SendMessageW(hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);PumpSettingsPaint();}
  std::wcout<<prefix<<L" scroll: content erase="<<viewportTrace.erase<<L" paint="<<viewportTrace.paint<<L" main paint="<<footerTrace.paint<<L" child moves="<<childTrace.moves<<L"\n";
  for(HWND child=GetWindow(state->content,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))RemoveWindowSubclass(child,TraceScrollPaint,903);
  RemoveWindowSubclass(state->content,TraceScrollPaint,901);RemoveWindowSubclass(hwnd,TraceScrollPaint,902);SendMessageW(hwnd,WM_VSCROLL,SB_TOP,0);

  for(int cycle=0;cycle<8;++cycle){SetWindowPos(hwnd,nullptr,0,0,desired.right-desired.left+120,desired.bottom-desired.top,SWP_NOMOVE|SWP_NOZORDER);UpdateWindow(state->content);SetWindowPos(hwnd,nullptr,0,0,desired.right-desired.left,desired.bottom-desired.top,SWP_NOMOVE|SWP_NOZORDER);UpdateWindow(state->content);}
  save(hwnd,prefix+L"-hint-resize");
  desired={0,0,m.doubleColumn+Scale(state,80)+GetSystemMetricsForDpi(SM_CXVSCROLL,state->dpi),Scale(state,900)};
  AdjustWindowRectExForDpi(&desired,GetWindowLongW(hwnd,GWL_STYLE),FALSE,0,state->dpi);
  SetWindowPos(hwnd,nullptr,0,0,desired.right-desired.left,desired.bottom-desired.top,SWP_NOMOVE|SWP_NOZORDER);save(hwnd,prefix+L"-wide");
  SendMessageW(hwnd,WM_COMMAND,IDC_ADVANCED,0);save(hwnd,prefix+L"-advanced");
  SettingsMessage(hwnd,IDC_ADVANCED_MODE,CB_SETCURSEL,1,0);SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_ADVANCED_MODE,CBN_SELCHANGE),0);save(hwnd,prefix+L"-url");
  SendMessageW(hwnd,WM_CLOSE,0,0);
 }
}
