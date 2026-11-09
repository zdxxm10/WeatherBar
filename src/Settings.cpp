#include "pch.h"
#include "Settings.h"
#include "DeskBand.h"
#include "Update.h"
#include "Location.h"
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
enum ControlId {
	IDC_URL = 100, IDC_LANGUAGE = 102,
	IDC_SHOW_ICON, IDC_ICON_SIZE, IDC_GAP_ICON, IDC_GAP_EXT,
	IDC_CUR_SIZE, IDC_CUR_COLOR, IDC_SHOW_MAX, IDC_MAX_SIZE, IDC_MAX_COLOR,
	IDC_SHOW_MIN, IDC_MIN_SIZE, IDC_MIN_COLOR,
	IDC_SAVE, IDC_CANCEL, IDC_RESET, IDC_CHECK_UPDATE, IDC_RELEASES, IDC_UPDATE_STATUS,
	IDC_COUNTRY = 160, IDC_QUERY, IDC_SEARCH, IDC_RESULTS, IDC_APPLY_LOCATION,
	IDC_LOCATION_STATUS, IDC_CURRENT_LOCATION, IDC_CURRENT_ZONE, IDC_ADVANCED, IDC_ADVANCED_MODE,
	IDC_LATITUDE, IDC_LONGITUDE, IDC_APPLY_COORDINATES, IDC_APPLY_URL, IDC_WEATHER_TITLE,
	IDC_COUNTRY_LABEL, IDC_QUERY_LABEL, IDC_MODE_LABEL, IDC_COORDINATE_LABEL, IDC_LATITUDE_LABEL,
	IDC_LONGITUDE_LABEL, IDC_URL_LABEL, IDC_LANGUAGE_LABEL, IDC_COORD_ERROR, IDC_URL_ERROR, IDC_SOURCES,
	IDC_CONTENT = 220, IDC_DISPLAY_TITLE, IDC_SPACING_TITLE, IDC_TOOLTIP_TITLE, IDC_ABOUT_TITLE,
	IDC_ITEM_LABEL, IDC_SHOW_LABEL, IDC_SIZE_LABEL, IDC_COLOR_LABEL, IDC_CUR_LABEL, IDC_ALWAYS_LABEL,
	IDC_ICON_LABEL, IDC_ICON_COLOR_LABEL, IDC_MAX_LABEL, IDC_MIN_LABEL, IDC_GAP_ICON_LABEL, IDC_GAP_EXT_LABEL, IDC_VERSION,
	IDC_TOOLTIP = 140, IDC_TOOLTIP_FIRST, IDC_TOOLTIP_LAST = IDC_TOOLTIP_FIRST + 9
};
constexpr UINT_PTR kUpdateTimer = 1;
constexpr UINT_PTR kLocationTimer = 3;
constexpr UINT kColorChanged = WM_APP + 20;
constexpr int kFooterHeight = 64;

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
	HWND content = nullptr;
	std::vector<RECT> cards;
	int contentHeight = 0, viewportHeight = 0;
	bool twoColumns = false;
	int dpi = 96;
	int scroll = 0;
	std::shared_ptr<UpdateCheck> update;
	bool advanced = false;
	bool layingOut = false;
	int basicHeight = 0;
	std::shared_ptr<LocationRequest> search, locationWeather;
	std::vector<LocationCandidate> candidates;
	BandConfig pendingLocation;
	int locationMessageId = IDC_LOCATION_STATUS;
};

int Scale(const SettingsState* state, int value) {
	return MulDiv(value, state->dpi, 96);
}

HWND SettingsControl(HWND hwnd, int id) {
 HWND direct = GetDlgItem(hwnd, id);
 return direct ? direct : GetDlgItem(GetDlgItem(hwnd, IDC_CONTENT), id);
}
LRESULT SettingsMessage(HWND hwnd, int id, UINT msg, WPARAM wp, LPARAM lp) {
 return SendMessageW(SettingsControl(hwnd, id), msg, wp, lp);
}

RECT FitSettingsWindow(RECT rect, const RECT& work) {
 int width=(std::min)(rect.right-rect.left,work.right-work.left),height=(std::min)(rect.bottom-rect.top,work.bottom-work.top);
 rect.left=(std::max)(work.left,(std::min)(rect.left,work.right-width));rect.top=(std::max)(work.top,(std::min)(rect.top,work.bottom-height));
 rect.right=rect.left+width;rect.bottom=rect.top+height;return rect;
}

std::wstring ControlText(HWND parent, int id) {
	HWND control = SettingsControl(parent, id);
	int length = GetWindowTextLengthW(control);
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	int copied = GetWindowTextW(control, text.data(), length + 1);
	text.resize(copied > 0 ? copied : 0);
	return text;
}

void SetControlText(HWND parent, int id, const std::wstring& text) {
 HWND control=SettingsControl(parent,id);SetWindowTextW(control,text.c_str());
 if(id==IDC_LOCATION_STATUS)InvalidateRect(control,nullptr,FALSE);
}

void ResetSearchMessage(HWND hwnd,SettingsState* state) {
 SetControlText(hwnd,IDC_LOCATION_STATUS,Text(state->language,L"请选择候选，再应用地点。",L"Select a result, then Apply.",L"候補を選んで適用。"));
}

void EnsureSettingsControlVisible(HWND hwnd) {
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

LRESULT CALLBACK SettingsChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == WM_SETFOCUS) EnsureSettingsControlVisible(hwnd);
	if (msg == WM_GETDLGCODE)
		return DefSubclassProc(hwnd, msg, wp, lp) | DLGC_WANTTAB;
	if (msg == WM_KEYDOWN && (wp == VK_ESCAPE || wp == VK_RETURN)) {
		if (wp == VK_RETURN && GetDlgCtrlID(hwnd) == IDC_QUERY) {
			SendMessageW(GetParent(hwnd), WM_COMMAND, IDC_SEARCH, 0); return 0;
		}
		if (wp == VK_RETURN && (GetDlgCtrlID(hwnd) == IDC_LATITUDE || GetDlgCtrlID(hwnd) == IDC_LONGITUDE || GetDlgCtrlID(hwnd) == IDC_COUNTRY || GetDlgCtrlID(hwnd) == IDC_ADVANCED_MODE))
			return DefSubclassProc(hwnd, msg, wp, lp);
		if (wp == VK_RETURN && GetDlgCtrlID(hwnd) == IDC_URL)
			return DefSubclassProc(hwnd, msg, wp, lp);
		wchar_t klass[32]{};
		GetClassNameW(hwnd, klass, 32);
		if (wp == VK_RETURN && lstrcmpiW(klass, WC_LINK) == 0) return DefSubclassProc(hwnd,msg,wp,lp);
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
		HWND next = GetNextDlgTabItem(GetAncestor(hwnd, GA_ROOT), hwnd, GetKeyState(VK_SHIFT) < 0);
		if (next) SetFocus(next);
		return 0;
	}
	return DefSubclassProc(hwnd, msg, wp, lp);
}

RECT CountryDropdownRect(HWND combo) {
 COMBOBOXINFO info{sizeof(info)};GetComboBoxInfo(combo,&info);
 RECT bounds{},field{},list{},client{};GetClientRect(GetParent(combo),&bounds);MapWindowPoints(GetParent(combo),nullptr,reinterpret_cast<POINT*>(&bounds),2);GetWindowRect(combo,&field);
 MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(combo,MONITOR_DEFAULTTONEAREST),&monitor);IntersectRect(&bounds,&bounds,&monitor.rcWork);
 GetWindowRect(info.hwndList,&list);GetClientRect(info.hwndList,&client);
 int border=(std::max)(2,static_cast<int>(list.bottom-list.top-client.bottom));
 int item=(std::max)(1,static_cast<int>(SendMessageW(combo,CB_GETITEMHEIGHT,0,0)));
 int count=static_cast<int>(SendMessageW(combo,CB_GETCOUNT,0,0)),wanted=count*item+border;
 int below=(std::max)(0,static_cast<int>(bounds.bottom-field.bottom)),above=(std::max)(0,static_cast<int>(field.top-bounds.top));
 bool down=wanted<=below || (wanted>above && below>=above);
 int available=down?below:above;
 int height=(std::min)(wanted,((std::max)(0,available-border)/item)*item+border);
 int width=field.right-field.left;
 int x=(std::max)(bounds.left,(std::min)(field.left,bounds.right-width));
 int y=down?field.bottom:field.top-height;
 return {x,y,x+width,y+height};
}

LRESULT CALLBACK ComboWheelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
 HWND combo=reinterpret_cast<HWND>(data);
 if(hwnd==combo && GetDlgCtrlID(combo)==IDC_COUNTRY
  && ((msg==CB_SHOWDROPDOWN && wp) || msg==WM_LBUTTONDOWN || ((msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN) && (wp==VK_F4 || wp==VK_DOWN || wp==VK_UP || wp==VK_SPACE)))
  && !SendMessageW(combo,CB_GETDROPPEDSTATE,0,0)){
  // A focused selector may have scrolled out of view since it last received focus.
  SetFocus(combo);EnsureSettingsControlVisible(combo);
  auto drop=CountryDropdownRect(combo);RECT field{};GetWindowRect(combo,&field);
  // Set the requested drop height before native ComboBox opens and caches its internal visible row count.
  SetWindowPos(combo,nullptr,0,0,field.right-field.left,field.bottom-field.top+drop.bottom-drop.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
 }

 // Native ComboLBox still owns drawing/selection; constrain its final popup rectangle, not a second layout pass.
 if(msg==WM_WINDOWPOSCHANGING && hwnd!=combo && GetDlgCtrlID(combo)==IDC_COUNTRY){
  auto* position=reinterpret_cast<WINDOWPOS*>(lp);
  if(!(position->flags&SWP_HIDEWINDOW) && ((position->flags&SWP_SHOWWINDOW) || IsWindowVisible(hwnd))){
   auto rect=CountryDropdownRect(combo);position->x=rect.left;position->y=rect.top;position->cx=rect.right-rect.left;position->cy=rect.bottom-rect.top;
   position->flags&=~(SWP_NOMOVE|SWP_NOSIZE);
  }
 }
	if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
		// The dropdown list receives its own wheel messages. Consume both surfaces to prevent selection/preview.
		if (msg == WM_MOUSEWHEEL) {
			if (SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) {
				COMBOBOXINFO info{sizeof(info)};
				if (GetComboBoxInfo(combo, &info) && info.hwndList) {
					int top = static_cast<int>(SendMessageW(info.hwndList, LB_GETTOPINDEX, 0, 0));
					int count = static_cast<int>(SendMessageW(info.hwndList, LB_GETCOUNT, 0, 0));
					if (count > 0) SendMessageW(info.hwndList, LB_SETTOPINDEX, (std::max)(0, (std::min)(count-1, top-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*3)), 0);
				}
			} else if (hwnd == combo) SendMessageW(GetParent(combo), msg, wp, lp);
		}
		return 0;
	}
	if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ComboWheelProc, id);
	return DefSubclassProc(hwnd, msg, wp, lp);
}

HWND AddControl(HWND parent, const wchar_t* klass, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height, HFONT font) {
	HWND control = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent, (HMENU)(INT_PTR)id, g_hinstDll, nullptr);
	if (control) {
		SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
		if (style & WS_TABSTOP) SetWindowSubclass(control, SettingsChildProc, 1, 0);
		if (lstrcmpiW(klass, L"COMBOBOX") == 0) {
			SetWindowSubclass(control, ComboWheelProc, 2, reinterpret_cast<DWORD_PTR>(control));
			COMBOBOXINFO info{sizeof(info)};
			if (GetComboBoxInfo(control, &info) && info.hwndList) SetWindowSubclass(info.hwndList, ComboWheelProc, 2, reinterpret_cast<DWORD_PTR>(control));
		}
	}
	return control;
}

SizeLevel ReadLevel(HWND hwnd, int id) {
	LRESULT selected = SettingsMessage(hwnd, id, CB_GETCURSEL, 0, 0);
	return selected == 0 ? SizeLevel::Small : selected == 2 ? SizeLevel::Large : SizeLevel::Medium;
}

void ReadAppearance(HWND hwnd, BandConfig& draft) {
	LRESULT selected = SettingsMessage(hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
	draft.language = selected >= 0 && selected <= 3 ? static_cast<Language>(selected) : Language::System;
	draft.showIcon = Button_GetCheck(SettingsControl(hwnd, IDC_SHOW_ICON)) == BST_CHECKED;
	draft.showMax = Button_GetCheck(SettingsControl(hwnd, IDC_SHOW_MAX)) == BST_CHECKED;
	draft.showMin = Button_GetCheck(SettingsControl(hwnd, IDC_SHOW_MIN)) == BST_CHECKED;
	draft.tooltipEnabled = Button_GetCheck(SettingsControl(hwnd, IDC_TOOLTIP)) == BST_CHECKED;
	draft.tooltipRefresh = Button_GetCheck(SettingsControl(hwnd, IDC_TOOLTIP_FIRST + 8)) == BST_CHECKED;
	draft.tooltipStatus = Button_GetCheck(SettingsControl(hwnd, IDC_TOOLTIP_FIRST + 9)) == BST_CHECKED;
	draft.tooltipFields = 0;
	for (int field = 0; field < 10; ++field) {
		HWND check = SettingsControl(hwnd, IDC_TOOLTIP_FIRST + field);
		if (field < 8 && Button_GetCheck(check) == BST_CHECKED) draft.tooltipFields |= 1u << field;
		EnableWindow(check, draft.tooltipEnabled);
	}
	draft.iconLevel = ReadLevel(hwnd, IDC_ICON_SIZE);
	draft.currentLevel = ReadLevel(hwnd, IDC_CUR_SIZE);
	draft.maxLevel = ReadLevel(hwnd, IDC_MAX_SIZE);
	draft.minLevel = ReadLevel(hwnd, IDC_MIN_SIZE);
	draft.gapIconLevel = ReadLevel(hwnd, IDC_GAP_ICON);
	draft.gapExtremeLevel = ReadLevel(hwnd, IDC_GAP_EXT);
	draft.currentColor = static_cast<COLORREF>(GetWindowLongPtrW(SettingsControl(hwnd, IDC_CUR_COLOR), GWLP_USERDATA));
	draft.maxColor = static_cast<COLORREF>(GetWindowLongPtrW(SettingsControl(hwnd, IDC_MAX_COLOR), GWLP_USERDATA));
	draft.minColor = static_cast<COLORREF>(GetWindowLongPtrW(SettingsControl(hwnd, IDC_MIN_COLOR), GWLP_USERDATA));
}

std::wstring SelectedCountry(HWND hwnd);

bool ReadDraft(HWND hwnd, BandConfig& draft, Language language, std::wstring& error) {
	ReadAppearance(hwnd, draft);
	draft.searchCountry = SelectedCountry(hwnd);
	(void)language;
	(void)error;
	return true;
}

void SetColor(HWND hwnd, int id, COLORREF color) {
	HWND button = SettingsControl(hwnd, id);
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


struct SettingsMetrics {
 int shortButton, normalButton, country, language, master, mode, label, place, size, color, display, hover, about, sources, version, basic;
 int single, doubleColumn;
};
SettingsMetrics MeasureSettingsLanguage(HWND hwnd, SettingsState* state, Language language) {
 HDC dc=GetDC(hwnd); auto old=SelectObject(dc,state->font);
 auto width=[&](const std::wstring& text) { SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);return static_cast<int>(size.cx); };
 auto text=[&](int id) {
  for(const auto& label:state->labels)if(GetDlgCtrlID(label.control)==id)return width(Text(language,label.text.zh,label.text.en,label.text.ja));
  return width(ControlText(hwnd,id));
 };
 auto S=[&](int n) {return Scale(state,n);};
 SettingsMetrics m{};m.shortButton=S(64);m.normalButton=S(96);
 for(const auto& label:state->labels) {
  int id=GetDlgCtrlID(label.control);
  bool shortAction=id==IDC_SEARCH || id==IDC_SAVE || id==IDC_CANCEL;
  bool normalAction=id==IDC_APPLY_LOCATION || id==IDC_APPLY_COORDINATES || id==IDC_APPLY_URL || id==IDC_RESET || id==IDC_CHECK_UPDATE || id==IDC_RELEASES;
  if(!shortAction && !normalAction)continue;
  int required=(std::max)({width(label.text.zh),width(label.text.en),width(label.text.ja)})+S(20);
  int& group=shortAction?m.shortButton:m.normalButton;group=(std::max)(group,required);
 }
 size_t count=0;auto countries=LocationCountries(count);
 for(size_t i=0;i<count;++i)m.country=(std::max)(m.country,(std::max)({width(countries[i].zh),width(countries[i].en),width(countries[i].ja)}));
 // One shared arrow/list-scrollbar allowance; borders and minimal text padding are counted once.
 int names=(std::max)({m.country,width(L"System default"),width(L"システムに従う"),width(L"跟随系统"),width(L"English"),width(L"日本語"),width(L"简体中文")});
 m.country=m.language=names+GetSystemMetricsForDpi(SM_CXVSCROLL,state->dpi)+2*GetSystemMetricsForDpi(SM_CXEDGE,state->dpi)+S(4);
 m.master=text(IDC_TOOLTIP)+S(26);
 m.mode=(std::max)({width(L"Manual coordinates"),width(L"Custom URL"),width(L"座標を手動入力")})+S(30);
 m.label=(std::max)(text(IDC_COUNTRY_LABEL),text(IDC_LANGUAGE_LABEL));m.place=text(IDC_QUERY_LABEL);
 m.size=(std::max)({width(L"Medium"),width(L"Small"),width(L"サイズ")})+S(30);
 m.color=width(L"#FFFFFF")+S(38);
 int rowLabel=(std::max)({text(IDC_CUR_LABEL),text(IDC_ICON_LABEL),text(IDC_MAX_LABEL),text(IDC_MIN_LABEL)});
 int show=(std::max)(text(IDC_ALWAYS_LABEL),text(IDC_SHOW_LABEL))+S(8);
 m.display=rowLabel+show+m.size+m.color+S(3*12+32);
 m.display=(std::max)(m.display,(std::max)(text(IDC_GAP_ICON_LABEL),text(IDC_GAP_EXT_LABEL))+m.size+S(44));
 int option=0;for(int i=0;i<10;++i)option=(std::max)(option,text(IDC_TOOLTIP_FIRST+i)+S(26));
 auto headingFont=SelectObject(dc,state->headingFont);int hoverHeading=text(IDC_TOOLTIP_TITLE);SelectObject(dc,headingFont);
 m.hover=(std::max)(2*option+S(44),hoverHeading+m.master+S(44));
 int query=width(Text(language,L"输入城市或区县",L"Enter a city or district",L"都市・区を入力"))+S(16);
 m.basic=m.label+m.country+m.place+query+m.shortButton+S(4*8+64);
 // The one-line status and action button also constrain the single-column minimum.
 m.basic=(std::max)(m.basic,text(IDC_LOCATION_STATUS)+m.normalButton+S(12+64));
 m.sources=width(Text(language,L"数据来源",L"Data sources",L"データ出典"))+S(4);m.version=text(IDC_VERSION)+S(4);
 auto previous=SelectObject(dc,state->headingFont);int heading=text(IDC_ABOUT_TITLE);SelectObject(dc,previous);
 m.about=(std::max)({2*m.normalButton+S(12+32),m.version+m.sources+S(12+32),heading+S(32)});
 int right=(std::max)(m.hover,m.about);
 m.single=(std::max)({m.basic,m.display+S(32),right+S(32),m.normalButton+2*m.shortButton+S(44)});
 m.doubleColumn=(std::max)(m.single,m.display+right+S(48));
 SelectObject(dc,old);ReleaseDC(hwnd,dc);return m;
}

SettingsMetrics MeasureSettings(HWND hwnd,SettingsState* state) {
 auto result=MeasureSettingsLanguage(hwnd,state,state->language);
 // Measure translated labels without changing controls, language, draft or preview state.
 for(Language language:{Language::Chinese,Language::English,Language::Japanese}){
  auto other=MeasureSettingsLanguage(hwnd,state,language);
  result.single=(std::max)(result.single,other.single);result.doubleColumn=(std::max)(result.doubleColumn,other.doubleColumn);
 }
 HDC dc=GetDC(hwnd);auto old=SelectObject(dc,state->font);SIZE size{};auto status=ControlText(hwnd,IDC_LOCATION_STATUS);
 GetTextExtentPoint32W(dc,status.c_str(),static_cast<int>(status.size()),&size);SelectObject(dc,old);ReleaseDC(hwnd,dc);
 result.single=(std::max)(result.single,static_cast<int>(size.cx)+result.normalButton+Scale(state,76));
 result.doubleColumn=(std::max)(result.doubleColumn,result.single);return result;
}

int SettingsScrollbarWidth(SettingsState* state) {
 return GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(state->content));
}

void UpdateUrlScrollbar(HWND hwnd) {
 HWND url=SettingsControl(hwnd,IDC_URL);
 if(!(GetWindowLongW(url,GWL_STYLE)&WS_VISIBLE))return;
 RECT format{};SendMessageW(url,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
 int first=static_cast<int>(SendMessageW(url,EM_GETFIRSTVISIBLELINE,0,0));
 bool visible=(GetWindowLongW(url,GWL_STYLE)&WS_VSCROLL)!=0;
 // URLs use the existing per-character word break. Measure the no-bar width without mutating EDIT layout.
 HDC dc=GetDC(url);auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(url,WM_GETFONT,0,0)));TEXTMETRICW font{};GetTextMetricsW(dc,&font);
 int width=format.right-format.left+(visible?GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(url)):0);
 auto text=ControlText(hwnd,IDC_URL);size_t position=0;int lines=0;
 do {
  size_t end=text.find_first_of(L"\r\n",position);if(end==std::wstring::npos)end=text.size();
  if(end==position)++lines;
  while(position<end){int fit=0;SIZE size{};GetTextExtentExPointW(dc,text.data()+position,static_cast<int>(end-position),(std::max)(1,width),&fit,nullptr,&size);position+=(std::max)(1,fit);++lines;}
  if(end==text.size())break;
  position=end+1;if(text[end]==L'\r' && position<text.size() && text[position]==L'\n')++position;
 }while(position<=text.size());
 SelectObject(dc,old);ReleaseDC(url,dc);
 bool needed=lines*font.tmHeight>format.bottom-format.top;
 if(needed!=visible){ShowScrollBar(url,SB_VERT,needed);InvalidateRect(url,nullptr,TRUE);}
 int current=static_cast<int>(SendMessageW(url,EM_GETFIRSTVISIBLELINE,0,0));SendMessageW(url,EM_LINESCROLL,0,first-current);
}

void ScrollSettings(SettingsState* state,int requested) {
 SCROLLINFO info{sizeof(info),SIF_POS};info.nPos=requested;SetScrollInfo(state->content,SB_VERT,&info,TRUE);
 int next=GetScrollPos(state->content,SB_VERT),delta=state->scroll-next;
 if(!delta)return;
 SettingsMessage(GetParent(state->content),IDC_COUNTRY,CB_SHOWDROPDOWN,FALSE,0);
 state->scroll=next;
 // Pure scrolling copies existing cards and moves child windows; it never relayouts the page/footer.
 ScrollWindowEx(state->content,0,delta,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE);
}

void LayoutSettings(HWND hwnd, SettingsState* state) {
 if(state->layingOut || !state->content || !SettingsControl(hwnd,IDC_SAVE))return;
 state->layingOut=true;SettingsMessage(hwnd,IDC_COUNTRY,CB_SHOWDROPDOWN,FALSE,0);
 auto S=[&](int n){return Scale(state,n);};
 RECT frame{};GetClientRect(hwnd,&frame);
 int footer=S(kFooterHeight);
 state->viewportHeight=(std::max)(1,static_cast<int>(frame.bottom)-footer);
 SetWindowPos(state->content,nullptr,0,0,frame.right,state->viewportHeight,SWP_NOZORDER|SWP_NOACTIVATE);
 MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
 for(int pass=0;pass<2;++pass){
 RECT client{};GetClientRect(state->content,&client);
 auto m=MeasureSettings(hwnd,state);
 struct Position {int id,x,y,w,h;bool visible;};std::vector<Position> positions;
 auto place=[&](int id,int x,int y,int w,int h,bool visible=true){positions.push_back({id,x,y,(std::max)(1,w),h,visible});};
 int margin=S(16),pad=S(16),gap=S(12),h=S(30),step=S(38),full=client.right-2*margin;
 auto combo=[&](int id,int x,int y,int w,bool visible=true){
  int height=S(170);
  if(id==IDC_COUNTRY)height=h+static_cast<int>(SettingsMessage(hwnd,id,CB_GETCOUNT,0,0))*static_cast<int>(SettingsMessage(hwnd,id,CB_GETITEMHEIGHT,0,0))+2*GetSystemMetricsForDpi(SM_CYEDGE,state->dpi);
  place(id,x,y+S(2),w,height,visible);
 };
 state->cards.clear();
 auto title=[&](int id,int x,int y,int w){place(id,x+pad,y+pad,w-2*pad,S(24));return y+pad+S(36);};
 auto finish=[&](int x,int y,int w,int end){state->cards.push_back({x,y,x+w,end+pad});return end+pad;};
 int x=margin,y=margin,w=full,inside=w-2*pad,left=x+pad,right=x+w-pad;
 int row=title(IDC_WEATHER_TITLE,x,y,w),editor=left+m.label+S(8);
 place(IDC_LANGUAGE_LABEL,left,row,m.label,h);combo(IDC_LANGUAGE,editor,row,m.language);row+=step;
 place(IDC_COUNTRY_LABEL,left,row,m.label,h);combo(IDC_COUNTRY,editor,row,m.country);
 int next=editor+m.country+S(8);place(IDC_QUERY_LABEL,next,row,m.place,h);next+=m.place+S(8);
 place(IDC_QUERY,next,row,right-m.shortButton-S(8)-next,h);place(IDC_SEARCH,right-m.shortButton,row,m.shortButton,h);row+=step;
 // The list and action row stay present, so search states cannot move the rest of the page.
 place(IDC_RESULTS,left,row,inside,S(72));row+=S(82);
 place(IDC_LOCATION_STATUS,left,row,inside-m.normalButton-gap,h);
 place(IDC_APPLY_LOCATION,right-m.normalButton,row,m.normalButton,h);row+=step;
 place(IDC_CURRENT_LOCATION,left,row,inside,S(22));row+=S(26);
 place(IDC_CURRENT_ZONE,left,row,inside,S(22));row+=S(30);
 place(IDC_ADVANCED,left,row,inside,h);row+=h;
 bool advanced=state->advanced,coordinates=SettingsMessage(hwnd,IDC_ADVANCED_MODE,CB_GETCURSEL,0,0)==0;
 if(advanced)row+=gap;
 place(IDC_MODE_LABEL,left,row,m.label,h,advanced);combo(IDC_ADVANCED_MODE,editor,row,m.mode,advanced);if(advanced)row+=step;
 place(IDC_COORDINATE_LABEL,left,row,inside,S(24),advanced&&coordinates);if(advanced&&coordinates)row+=S(30);
 int half=(inside-gap)/2,coordinateLabel=S(80);
 place(IDC_LATITUDE_LABEL,left,row,coordinateLabel,h,advanced&&coordinates);place(IDC_LATITUDE,left+coordinateLabel,row,half-coordinateLabel,h,advanced&&coordinates);
 place(IDC_LONGITUDE_LABEL,left+half+gap,row,coordinateLabel,h,advanced&&coordinates);place(IDC_LONGITUDE,left+half+gap+coordinateLabel,row,half-coordinateLabel,h,advanced&&coordinates);
 if(advanced&&coordinates)row+=step;
 bool coordError=GetWindowTextLengthW(SettingsControl(hwnd,IDC_COORD_ERROR))!=0;
 place(IDC_COORD_ERROR,left,row,inside,h,advanced&&coordinates&&coordError);if(advanced&&coordinates&&coordError)row+=step;
 place(IDC_APPLY_COORDINATES,left+(inside-m.normalButton)/2,row,m.normalButton,h,advanced&&coordinates);if(advanced&&coordinates)row+=h;
 place(IDC_URL_LABEL,left,row,inside,S(24),advanced&&!coordinates);if(advanced&&!coordinates)row+=S(30);
 place(IDC_URL,left,row,inside,S(68),advanced&&!coordinates);if(advanced&&!coordinates)row+=S(78);
 bool urlError=GetWindowTextLengthW(SettingsControl(hwnd,IDC_URL_ERROR))!=0;
 place(IDC_URL_ERROR,left,row,inside,h,advanced&&!coordinates&&urlError);if(advanced&&!coordinates&&urlError)row+=step;
 place(IDC_APPLY_URL,left+(inside-m.normalButton)/2,row,m.normalButton,h,advanced&&!coordinates);if(advanced&&!coordinates)row+=h;
 int bottom=finish(x,y,w,row);state->basicHeight=bottom-y;
 int middle=bottom+margin;
 // All three languages and the header switch participate in the shared column minimum.
 int rightMinimum=(std::max)(m.hover,m.about);
 // Reserve potential scrollbar room only for the breakpoint; a hidden bar leaves its pixels usable.
 state->twoColumns=frame.right-SettingsScrollbarWidth(state)>=m.doubleColumn;
 int displayWidth=state->twoColumns?(std::max)(m.display,(std::min)((full-margin)/2,full-margin-rightMinimum)):full;
 x=margin;y=middle;w=displayWidth;left=x+pad;right=x+w-pad;
 row=title(IDC_DISPLAY_TITLE,x,y,w);
 int colorX=right-m.color,sizeX=colorX-gap-m.size,showWidth=0;
 // The show column is sized by its actual labels, not by spare card width.
 HDC dc=GetDC(hwnd);auto old=SelectObject(dc,state->font);SIZE always{},show{};
 auto alwaysText=ControlText(hwnd,IDC_ALWAYS_LABEL),showText=ControlText(hwnd,IDC_SHOW_LABEL);
 GetTextExtentPoint32W(dc,alwaysText.c_str(),static_cast<int>(alwaysText.size()),&always);GetTextExtentPoint32W(dc,showText.c_str(),static_cast<int>(showText.size()),&show);
 SelectObject(dc,old);ReleaseDC(hwnd,dc);showWidth=(std::max)(always.cx,show.cx)+S(8);
 int showX=sizeX-gap-showWidth,labelWidth=showX-left-gap;
 place(IDC_ITEM_LABEL,left,row,labelWidth,S(24));place(IDC_SHOW_LABEL,showX,row,showWidth,S(24));place(IDC_SIZE_LABEL,sizeX,row,m.size,S(24));place(IDC_COLOR_LABEL,colorX,row,m.color,S(24));row+=S(30);
 auto appearance=[&](int label,int check,int size,int color,int fallback){
  place(label,left,row,labelWidth,h);
  if(check)place(check,showX+(showWidth-S(24))/2,row,S(24),h);else place(IDC_ALWAYS_LABEL,showX,row,showWidth,h);
  combo(size,sizeX,row,m.size);
  if(color)place(color,colorX,row,m.color,h);else place(fallback,colorX,row,m.color,h);
  row+=S(34);
 };
 appearance(IDC_CUR_LABEL,0,IDC_CUR_SIZE,IDC_CUR_COLOR,0);appearance(IDC_ICON_LABEL,IDC_SHOW_ICON,IDC_ICON_SIZE,0,IDC_ICON_COLOR_LABEL);
 appearance(IDC_MAX_LABEL,IDC_SHOW_MAX,IDC_MAX_SIZE,IDC_MAX_COLOR,0);appearance(IDC_MIN_LABEL,IDC_SHOW_MIN,IDC_MIN_SIZE,IDC_MIN_COLOR,0);
 int displayEnd=finish(x,y,w,row),spacingY=displayEnd+margin;size_t spacingStart=positions.size();
 row=title(IDC_SPACING_TITLE,x,spacingY,w);
 place(IDC_GAP_ICON_LABEL,left,row,w-2*pad-m.size-gap,h);combo(IDC_GAP_ICON,right-m.size,row,m.size);row+=step;
 place(IDC_GAP_EXT_LABEL,left,row,w-2*pad-m.size-gap,h);combo(IDC_GAP_EXT,right-m.size,row,m.size);row+=h;
 int spacingEnd=finish(x,spacingY,w,row);size_t spacingStop=positions.size();
 x=state->twoColumns?margin+displayWidth+margin:margin;y=state->twoColumns?middle:spacingEnd+margin;w=state->twoColumns?full-displayWidth-margin:full;
 left=x+pad;inside=w-2*pad;row=y+pad+S(36);
 place(IDC_TOOLTIP_TITLE,left,y+pad,inside-m.master-gap,h);
 place(IDC_TOOLTIP,x+w-pad-m.master,y+pad,m.master,h);
 const int order[]={0,7,1,2,3,4,5,6,8,9};int optionWidth=(inside-gap)/2;
 for(int i=0;i<10;++i)place(IDC_TOOLTIP_FIRST+order[i],left+(i%2)*(optionWidth+gap),row+(i/2)*S(30),optionWidth,h);
 row+=5*S(30);int hoverEnd=finish(x,y,w,row);
 x=state->twoColumns?margin+displayWidth+margin:margin;
 y=(state->twoColumns?hoverEnd:(std::max)(spacingEnd,hoverEnd))+margin;w=state->twoColumns?full-displayWidth-margin:full;left=x+pad;inside=w-2*pad;
 size_t aboutStart=positions.size();
 row=title(IDC_ABOUT_TITLE,x,y,w);
 int buttons=2*m.normalButton+gap;
 place(IDC_VERSION,left,row,m.version,h);place(IDC_SOURCES,left+m.version+gap,row+S(6),m.sources,h-S(6));
 if(inside<m.version+m.sources+2*gap+buttons)row+=step;
 int buttonX=x+w-pad-buttons;
 place(IDC_CHECK_UPDATE,buttonX,row,m.normalButton,h);place(IDC_RELEASES,buttonX+m.normalButton+gap,row,m.normalButton,h);row+=h;
 bool update=GetWindowTextLengthW(SettingsControl(hwnd,IDC_UPDATE_STATUS))!=0;
 int updateHeight=S(24);
 if(update){HDC measure=GetDC(hwnd);auto previous=SelectObject(measure,state->font);RECT r{0,0,inside,0};auto text=ControlText(hwnd,IDC_UPDATE_STATUS);DrawTextW(measure,text.c_str(),-1,&r,DT_CALCRECT|DT_WORDBREAK);updateHeight=(std::max)(updateHeight,static_cast<int>(r.bottom));SelectObject(measure,previous);ReleaseDC(hwnd,measure);row+=gap;}
 place(IDC_UPDATE_STATUS,left,row,inside,updateHeight,update);if(update)row+=updateHeight;
 int aboutEnd=finish(x,y,w,row);
 if(state->twoColumns){
  // Two card rows share the larger required height; single-column cards retain their natural height.
  int firstBottom=(std::max)(displayEnd,hoverEnd),secondTop=firstBottom+margin;
  int spacingShift=secondTop-state->cards[2].top,aboutShift=secondTop-state->cards[4].top;
  for(size_t i=spacingStart;i<spacingStop;++i)positions[i].y+=spacingShift;
  for(size_t i=aboutStart;i<positions.size();++i)positions[i].y+=aboutShift;
  state->cards[1].bottom=state->cards[3].bottom=firstBottom;
  int secondBottom=(std::max)(spacingEnd+spacingShift,aboutEnd+aboutShift);
  state->cards[2].top=state->cards[4].top=secondTop;state->cards[2].bottom=state->cards[4].bottom=secondBottom;
  spacingEnd=aboutEnd=secondBottom;
 }
 state->contentHeight=(std::max)(spacingEnd,aboutEnd)+margin;
 bool needed=state->contentHeight>state->viewportHeight;
 bool visible=(GetWindowLongW(state->content,GWL_STYLE)&WS_VSCROLL)!=0;
 if(needed!=visible && pass==0){ShowScrollBar(state->content,SB_VERT,needed);continue;}
 // Only the content viewport owns a scrollbar; the footer never enters its range.
 // Move focus before clamping scroll or submitting the batch; focus can itself request scrolling.
 for(const auto& item:positions)if(!item.visible && GetFocus()==SettingsControl(hwnd,item.id))SetFocus(SettingsControl(hwnd,IDC_ADVANCED));
 SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,state->contentHeight-1,static_cast<UINT>(state->viewportHeight),state->scroll,0};
 SetScrollInfo(state->content,SB_VERT,&info,TRUE);state->scroll=GetScrollPos(state->content,SB_VERT);
 RECT oldStatus{};GetWindowRect(SettingsControl(hwnd,IDC_LOCATION_STATUS),&oldStatus);
 HDWP batch=BeginDeferWindowPos(static_cast<int>(positions.size())+3);
 for(const auto& item:positions){HWND control=SettingsControl(hwnd,item.id);if(!control)continue;
  if(batch)batch=DeferWindowPos(batch,control,nullptr,item.x,item.y-state->scroll,item.w,item.h,SWP_NOZORDER|SWP_NOACTIVATE|(item.visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
 }
 if(batch)EndDeferWindowPos(batch);
 else for(const auto& item:positions)SetWindowPos(SettingsControl(hwnd,item.id),nullptr,item.x,item.y-state->scroll,item.w,item.h,SWP_NOZORDER|SWP_NOACTIVATE|(item.visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
 // Centered text moves when resized. Parent invalidation cannot erase a child using copied valid bits.
 RECT newStatus{};GetWindowRect(SettingsControl(hwnd,IDC_LOCATION_STATUS),&newStatus);
 if(!EqualRect(&oldStatus,&newStatus))InvalidateRect(SettingsControl(hwnd,IDC_LOCATION_STATUS),nullptr,FALSE);
 int footerY=state->viewportHeight+(footer-h)/2;
 SetWindowPos(SettingsControl(hwnd,IDC_RESET),nullptr,margin,footerY,m.normalButton,h,SWP_NOZORDER|SWP_NOACTIVATE);
 SetWindowPos(SettingsControl(hwnd,IDC_SAVE),nullptr,frame.right-margin-2*m.shortButton-gap,footerY,m.shortButton,h,SWP_NOZORDER|SWP_NOACTIVATE);
 SetWindowPos(SettingsControl(hwnd,IDC_CANCEL),nullptr,frame.right-margin-m.shortButton,footerY,m.shortButton,h,SWP_NOZORDER|SWP_NOACTIVATE);
 RECT minimum{0,0,m.single+SettingsScrollbarWidth(state),S(360)};
 AdjustWindowRectExForDpi(&minimum,GetWindowLongW(hwnd,GWL_STYLE),FALSE,0,state->dpi);
 state->minimumSize={(std::min)(minimum.right-minimum.left,monitor.rcWork.right-monitor.rcWork.left),(std::min)(minimum.bottom-minimum.top,monitor.rcWork.bottom-monitor.rcWork.top)};
 UpdateUrlScrollbar(hwnd);
 break;
 }
 state->layingOut=false;InvalidateRect(state->content,nullptr,FALSE);
 RECT footerArea{0,state->viewportHeight,frame.right,frame.bottom};InvalidateRect(hwnd,&footerArea,FALSE);
 RECT window{};GetWindowRect(hwnd,&window);
 if(window.right-window.left<state->minimumSize.cx || window.bottom-window.top<state->minimumSize.cy){
  window.right=window.left+(std::max)(static_cast<LONG>(state->minimumSize.cx),window.right-window.left);
  window.bottom=window.top+(std::max)(static_cast<LONG>(state->minimumSize.cy),window.bottom-window.top);
  window=FitSettingsWindow(window,monitor.rcWork);SetWindowPos(hwnd,nullptr,window.left,window.top,window.right-window.left,window.bottom-window.top,SWP_NOZORDER|SWP_NOACTIVATE);
 }
}

LRESULT CALLBACK SettingsContentProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
 auto* state=reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
 if(msg==WM_NCCREATE){state=reinterpret_cast<SettingsState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
 if(msg==WM_ERASEBKGND)return 1;
 if((msg==WM_PAINT || msg==WM_PRINTCLIENT) && state){
  PAINTSTRUCT paint{};HDC dc=msg==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):BeginPaint(hwnd,&paint);
  RECT client{};GetClientRect(hwnd,&client);FillRect(dc,&client,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  auto oldBrush=SelectObject(dc,state->panelBrush),oldPen=SelectObject(dc,GetStockObject(DC_PEN));SetDCPenColor(dc,RGB(220,225,232));
  for(auto panel:state->cards)RoundRect(dc,panel.left,panel.top-state->scroll,panel.right,panel.bottom-state->scroll,Scale(state,10),Scale(state,10));
  SelectObject(dc,oldPen);SelectObject(dc,oldBrush);if(msg==WM_PAINT)EndPaint(hwnd,&paint);return 0;
 }
 switch(msg){
 case WM_COMMAND:case WM_NOTIFY:case WM_DRAWITEM:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_VSCROLL:case WM_MOUSEWHEEL:case WM_CLOSE:
  return SendMessageW(GetParent(hwnd),msg,wp,lp);
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}

void LocationInfo(HWND hwnd, SettingsState* state) {
	auto T = [&](const wchar_t* zh, const wchar_t* en, const wchar_t* ja) { return Text(state->language, zh, en, ja); };
	const auto& config = state->draft;
	const wchar_t* source = !HasConfiguredLocation(config) ? T(L"尚未选择地点", L"No location selected", L"地点未選択") : config.locationMode == LocationMode::Custom ? T(L"自定义地址", L"Custom URL", L"カスタムURL")
		: config.locationMode == LocationMode::Search ? T(L"搜索地点", L"Search", L"検索地点") : T(L"手动坐标", L"Coordinates", L"座標");
	std::wstring name = config.locationName;
	if (!config.locationCountryCode.empty()) {
		LocationCandidate place;
		place.name = config.locationName; place.countryCode = config.locationCountryCode; place.country = config.locationCountry;
		place.admin = config.locationAdmin; place.latitude = config.latitude; place.longitude = config.longitude;
		name = LocationLabel(place, state->language);
	}
	SetControlText(hwnd, IDC_CURRENT_LOCATION, std::wstring(T(L"当前地点：", L"Location: ", L"現在地：")) + source + (name.empty() ? L"" : L" · " + name));
	std::wstring zone;
	if (!HasConfiguredLocation(config)) zone = T(L"时区：--", L"Timezone: --", L"タイムゾーン：--");
	else if (state->band && state->band->CurrentForecast().timezoneValid) {
		const auto& cache = state->band->CurrentForecast();
		zone = std::wstring(T(L"自动识别 · ", L"Automatic · ", L"自動・")) + cache.timezone + L" (" + FormatUtcOffset(cache.utcOffsetSeconds) + L")";
	} else zone = T(L"时区尚未获取", L"Timezone not obtained", L"タイムゾーン未取得");
	SetControlText(hwnd, IDC_CURRENT_ZONE, zone);
}

void LocationMessage(HWND hwnd, SettingsState* state, const wchar_t* zh, const wchar_t* en, const wchar_t* ja) {
	SetControlText(hwnd, state->locationMessageId, Text(state->language, zh, en, ja));
	LayoutSettings(hwnd, state);
}
void ClearLocationSearch(HWND hwnd, SettingsState* state) {
	// UI owns only the current shared request; discarded replies never access a window.
	state->search.reset(); state->candidates.clear();
	SettingsMessage(hwnd, IDC_RESULTS, LB_RESETCONTENT, 0, 0);
	SettingsMessage(hwnd, IDC_RESULTS, LB_SETHORIZONTALEXTENT, 0, 0);
	EnableWindow(SettingsControl(hwnd, IDC_APPLY_LOCATION), FALSE);
}
std::wstring SelectedCountry(HWND hwnd) {
	size_t count = 0; auto countries = LocationCountries(count);
	LRESULT index = SettingsMessage(hwnd, IDC_COUNTRY, CB_GETCURSEL, 0, 0);
	return index >= 0 && static_cast<size_t>(index) < count ? countries[index].code : L"";
}
bool ReadCustomLocation(HWND hwnd, const BandConfig& current, BandConfig& next) {
	next = current; std::wstring url = ControlText(hwnd, IDC_URL), error;
	url.erase(std::remove(url.begin(), url.end(), L'\r'), url.end()); url.erase(std::remove(url.begin(), url.end(), L'\n'), url.end());
	if (!CanonicalForecastUrl(url, next.url, error)) return false;
	next.locationMode = LocationMode::Custom; next.locationName.clear(); next.locationTimezone.clear(); next.latitude.clear(); next.longitude.clear();
	next.locationCountryCode.clear(); next.locationCountry.clear(); next.locationAdmin.clear();
	return true;
}
void ApplyLocation(HWND hwnd, SettingsState* state, BandConfig next) {
	state->search.reset(); next.searchCountry = SelectedCountry(hwnd);
	state->pendingLocation = std::move(next);
	try {
		state->locationWeather = StartLocationWeather(state->pendingLocation);
		LocationMessage(hwnd, state, L"正在验证天气和时区…", L"Checking weather and timezone…", L"天気・タイムゾーンを確認中…");
	} catch (...) { LocationMessage(hwnd, state, L"无法启动请求，请重试", L"Could not start request; retry", L"リクエストを開始できません。再試行してください"); }
}
void PollLocation(HWND hwnd, SettingsState* state) {
	if (state->search && state->search->done.load(std::memory_order_acquire)) {
		auto result = std::move(state->search);
		int messageId = state->locationMessageId; state->locationMessageId = IDC_LOCATION_STATUS;
		if (result->status == LocationStatus::Ready) {
			state->candidates = std::move(result->candidates);
			SettingsMessage(hwnd, IDC_RESULTS, LB_RESETCONTENT, 0, 0);
			HDC dc = GetDC(hwnd); HGDIOBJ old = SelectObject(dc, state->font); int extent = 0;
			for (const auto& item : state->candidates) {
				SettingsMessage(hwnd, IDC_RESULTS, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.label.c_str()));
				SIZE size{}; GetTextExtentPoint32W(dc, item.label.c_str(), static_cast<int>(item.label.size()), &size); extent = (std::max)(extent, static_cast<int>(size.cx));
			}
			SelectObject(dc, old); ReleaseDC(hwnd, dc);
			SettingsMessage(hwnd, IDC_RESULTS, LB_SETHORIZONTALEXTENT, extent + 16, 0);
			if (state->candidates.empty()) LocationMessage(hwnd, state, L"未找到地点，请换个名称或输入坐标。", L"No results; try another name or coordinates.", L"結果なし。別の名前か座標を試してください。");
			else { ResetSearchMessage(hwnd,state); LayoutSettings(hwnd, state); }
		} else if (result->status == LocationStatus::NetworkError) LocationMessage(hwnd, state, L"网络请求失败，请点击搜索重试。", L"Network request failed. Click Search to retry.", L"通信失敗。検索を押して再試行してください。");
		else if (result->status == LocationStatus::ServiceError) LocationMessage(hwnd, state, L"搜索服务请求失败，请重试。", L"Search service request failed; retry.", L"検索サービスのリクエスト失敗。再試行してください。");
		else LocationMessage(hwnd, state, L"搜索响应解析失败，请重试。", L"Could not parse search response; retry.", L"検索結果を解析できません。再試行してください。");
		state->locationMessageId = messageId;
	}
	if (state->locationWeather && state->locationWeather->done.load(std::memory_order_acquire)) {
		auto result = std::move(state->locationWeather);
		if (result->status == LocationStatus::Ready && state->band) {
			// A source change is committed to preview only after the complete response validates.
			ReadAppearance(hwnd, state->pendingLocation);
			state->draft = state->pendingLocation;
			if (state->draft.locationMode != LocationMode::Custom) state->draft.locationTimezone = result->forecast.timezone;
			state->band->ApplyPreviewConfig(state->draft, &result->forecast);
			LocationMessage(hwnd, state, L"已应用预览，保存后保留。", L"Preview applied; Save to keep.", L"プレビュー適用済み。保存で確定。");
		} else if (result->status == LocationStatus::NetworkError) LocationMessage(hwnd, state, L"请求失败，地点未改变。", L"Request failed; location kept.", L"通信失敗。地点は未変更。");
		else LocationMessage(hwnd, state, L"天气或时区无效，地点未改变。", L"Invalid weather/timezone; location kept.", L"天気・時区が無効。地点は未変更。");
	}
	LocationInfo(hwnd, state);
}

void RefreshLanguage(HWND hwnd, SettingsState* state) {
	state->language = ResolveLanguage(state->draft.language);
	auto T = [&](const wchar_t* zh, const wchar_t* en, const wchar_t* ja) { return Text(state->language, zh, en, ja); };
	SetWindowTextW(hwnd, T(L"天气设置", L"Weather settings", L"天気の設定"));
	SettingsMessage(hwnd, IDC_QUERY, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(T(L"输入城市或区县", L"Enter a city or district", L"都市・区を入力")));
	for (const auto& label : state->labels) {
        SetWindowTextW(label.control, T(label.text.zh, label.text.en, label.text.ja));
        if(GetDlgCtrlID(label.control)==IDC_LOCATION_STATUS)InvalidateRect(label.control,nullptr,FALSE);
    }
	for (int id : { IDC_ICON_SIZE, IDC_CUR_SIZE, IDC_MAX_SIZE, IDC_MIN_SIZE, IDC_GAP_ICON, IDC_GAP_EXT, IDC_LANGUAGE }) {
		HWND combo = SettingsControl(hwnd, id);
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
	LRESULT country = SettingsMessage(hwnd, IDC_COUNTRY, CB_GETCURSEL, 0, 0);
	SettingsMessage(hwnd, IDC_COUNTRY, CB_RESETCONTENT, 0, 0);
	size_t count = 0; auto countries = LocationCountries(count);
	for (size_t i = 0; i < count; ++i) SettingsMessage(hwnd, IDC_COUNTRY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(T(countries[i].zh, countries[i].en, countries[i].ja)));
	SettingsMessage(hwnd, IDC_COUNTRY, CB_SETCURSEL, country, 0);
	LRESULT mode = SettingsMessage(hwnd, IDC_ADVANCED_MODE, CB_GETCURSEL, 0, 0);
	SettingsMessage(hwnd, IDC_ADVANCED_MODE, CB_RESETCONTENT, 0, 0);
	for (auto text : {T(L"手动经纬度",L"Manual coordinates",L"座標を手動入力"), T(L"自定义地址",L"Custom URL",L"カスタムURL")}) SettingsMessage(hwnd, IDC_ADVANCED_MODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
	SettingsMessage(hwnd, IDC_ADVANCED_MODE, CB_SETCURSEL, mode, 0);
	LocationInfo(hwnd, state);
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
		Button_SetCheck(SettingsControl(hwnd, id), BST_CHECKED);
	for (int id : { IDC_CUR_SIZE, IDC_ICON_SIZE, IDC_MAX_SIZE, IDC_MIN_SIZE, IDC_GAP_ICON, IDC_GAP_EXT })
		SettingsMessage(hwnd, id, CB_SETCURSEL, static_cast<int>(SizeLevel::Large), 0);
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
		HWND next = GetNextDlgTabItem(GetAncestor(hwnd, GA_ROOT), hwnd, GetKeyState(VK_SHIFT) < 0);
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
		SettingsMessage(hwnd, 300 + i, TBM_SETPOS, TRUE, channels[i]);
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
		for (int i = 0; i < 3; ++i) channels[i] = static_cast<int>(SettingsMessage(hwnd, 300 + i, TBM_GETPOS, 0, 0));
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
	auto* palette = new PaletteState{ owner, target, state->dpi, static_cast<COLORREF>(GetWindowLongPtrW(SettingsControl(owner, target), GWLP_USERDATA)) };
	auto S = [&](int value) { return Scale(state, value); };
	RECT button{}; GetWindowRect(SettingsControl(owner, target), &button);
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
	ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); SetFocus(SettingsControl(hwnd, 300));
}


void ShowDataSources(HWND hwnd, Language language) {
	TASKDIALOGCONFIG dialog{sizeof(dialog)};
	dialog.hwndParent = hwnd; dialog.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT | TDF_POSITION_RELATIVE_TO_WINDOW;
	dialog.dwCommonButtons = TDCBF_OK_BUTTON;
	dialog.pszWindowTitle = Text(language, L"数据来源", L"Data sources", L"データ出典");
	dialog.pszContent = Text(language,
		L"天气与地点搜索：<a href=\"https://open-meteo.com/\">Open-Meteo</a>\n地点数据：<a href=\"https://www.geonames.org/about.html\">GeoNames</a>\n许可：<a href=\"https://open-meteo.com/en/licence\">CC BY 4.0 与来源说明</a>\n程序对数值取舍和天气分类作了简化。",
		L"Weather and geocoding: <a href=\"https://open-meteo.com/\">Open-Meteo</a>\nLocation data: <a href=\"https://www.geonames.org/about.html\">GeoNames</a>\nLicence: <a href=\"https://open-meteo.com/en/licence\">CC BY 4.0 and sources</a>\nValues and weather categories are simplified by this app.",
		L"天気・地点検索：<a href=\"https://open-meteo.com/\">Open-Meteo</a>\n地点データ：<a href=\"https://www.geonames.org/about.html\">GeoNames</a>\nライセンス：<a href=\"https://open-meteo.com/en/licence\">CC BY 4.0・出典</a>\n数値の丸めと天気分類の簡略化を行っています。");
	dialog.pfCallback = [](HWND window, UINT notification, WPARAM, LPARAM parameter, LONG_PTR) -> HRESULT {
		if (notification == TDN_HYPERLINK_CLICKED) {
			const wchar_t* url = reinterpret_cast<const wchar_t*>(parameter);
			if (lstrcmpW(url, L"https://open-meteo.com/") == 0 || lstrcmpW(url, L"https://www.geonames.org/about.html") == 0 || lstrcmpW(url, L"https://open-meteo.com/en/licence") == 0)
				ShellExecuteW(window, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
		}
		return S_OK;
	};
	if (FAILED(TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr)))
		ShowNotice(hwnd, language, dialog.pszWindowTitle, L"Open-Meteo: https://open-meteo.com/\nGeoNames: https://www.geonames.org/about.html\nCC BY 4.0: https://open-meteo.com/en/licence");
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
	case WM_DPICHANGED: {
		int nextDpi = HIWORD(wp);
		if (!nextDpi) return 0;
		HFONT oldFont = state->font, oldHeading = state->headingFont, oldAddress = state->addressFont;
		state->dpi = nextDpi;
		auto font = [&](int size, int weight) { return CreateFontW(-Scale(state, size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); };
		state->font = font(14, FW_NORMAL); state->headingFont = font(17, FW_BOLD); state->addressFont = font(12, FW_NORMAL);
		for (HWND host : {hwnd, state->content}) for (HWND child = GetWindow(host, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
			if(child==state->content)continue;
			HFONT previous = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
			SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(previous == oldAddress ? state->addressFont : previous == oldHeading ? state->headingFont : state->font), TRUE);
		}
		DeleteObject(oldFont); DeleteObject(oldHeading); DeleteObject(oldAddress);
		state->scroll = 0;
		auto* rect = reinterpret_cast<RECT*>(lp);
		MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(rect,MONITOR_DEFAULTTONEAREST),&monitor);
		RECT fitted=FitSettingsWindow(*rect,monitor.rcWork);
		SetWindowPos(hwnd, nullptr, fitted.left, fitted.top, fitted.right-fitted.left, fitted.bottom-fitted.top, SWP_NOZORDER | SWP_NOACTIVATE);
		LayoutSettings(hwnd, state); return 0;
	}
    case WM_NOTIFY: {
        auto* notice=reinterpret_cast<NMHDR*>(lp);
        if(notice->idFrom==IDC_SOURCES && (notice->code==NM_CLICK || notice->code==NM_RETURN)) {ShowDataSources(hwnd,state->language);return 0;}
        break;
    }
	case WM_ERASEBKGND: return 1;
	case WM_MOVE: SettingsMessage(hwnd,IDC_COUNTRY,CB_SHOWDROPDOWN,FALSE,0);return 0;
	case WM_SIZE: LayoutSettings(hwnd, state); return 0;
	case WM_PRINTCLIENT:
	case WM_PAINT: {
		PAINTSTRUCT paint{}; HDC dc = msg==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):BeginPaint(hwnd, &paint);
		RECT client{}; GetClientRect(hwnd, &client);
		if(msg==WM_PRINTCLIENT)FillRect(dc,&client,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		RECT footer{0,state->viewportHeight,client.right,client.bottom};
        FillRect(dc,&footer,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SetDCPenColor(dc,RGB(220,225,232));auto previous=SelectObject(dc,GetStockObject(DC_PEN));
        MoveToEx(dc,0,footer.top,nullptr);LineTo(dc,client.right,footer.top);SelectObject(dc,previous);
		if(msg==WM_PAINT)EndPaint(hwnd, &paint);
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
        if(item->CtlID==IDC_LOCATION_STATUS){
            FillRect(item->hDC,&item->rcItem,state->panelBrush);
            auto previous=SelectObject(item->hDC,state->font);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(35,35,35));
            auto text=ControlText(hwnd,IDC_LOCATION_STATUS);RECT area=item->rcItem;
            DrawTextW(item->hDC,text.c_str(),-1,&area,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX);
            SelectObject(item->hDC,previous);return TRUE;
        }
		if (item->CtlID == IDC_ADVANCED) {
			HBRUSH shade = CreateSolidBrush(RGB(226, 232, 239));
			FillRect(item->hDC, &item->rcItem, shade); DeleteObject(shade);
			FrameRect(item->hDC, &item->rcItem, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
			RECT text = item->rcItem; text.left += Scale(state, 8); text.right -= Scale(state, 32);
			SetBkMode(item->hDC, TRANSPARENT); SetTextColor(item->hDC, RGB(35,35,35));
			HGDIOBJ previous = SelectObject(item->hDC, state->headingFont);
			std::wstring title = ControlText(hwnd, IDC_ADVANCED);
			DrawTextW(item->hDC, title.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			RECT arrow = item->rcItem; arrow.left = arrow.right - Scale(state, 30);
			DrawTextW(item->hDC, state->advanced ? L"∧" : L"∨", -1, &arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			SelectObject(item->hDC, previous);
			if (item->itemState & ODS_FOCUS) { RECT focus = item->rcItem; InflateRect(&focus, -2, -2); DrawFocusRect(item->hDC, &focus); }
			return TRUE;
		}
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
	case WM_COMMAND: {
		if (state->initializing) return 0;
		if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == BN_CLICKED) {
			if (LOWORD(wp) == IDC_LANGUAGE) {
				ReadAppearance(hwnd, state->draft);
				ClearLocationSearch(hwnd, state); state->locationWeather.reset();
				ResetSearchMessage(hwnd,state);
				RefreshLanguage(hwnd, state); LayoutSettings(hwnd, state);
			}
			if (LOWORD(wp) == IDC_LANGUAGE || (LOWORD(wp) >= IDC_SHOW_ICON && LOWORD(wp) <= IDC_MIN_COLOR)
				|| (LOWORD(wp) >= IDC_TOOLTIP && LOWORD(wp) <= IDC_TOOLTIP_LAST)) PreviewSettings(hwnd, state);
		}
		int control = LOWORD(wp), notification = HIWORD(wp);
		if ((control == IDC_QUERY && notification == EN_CHANGE) || (control == IDC_COUNTRY && notification == CBN_SELCHANGE)) {
			ClearLocationSearch(hwnd, state);
			if (state->pendingLocation.locationMode == LocationMode::Search) state->locationWeather.reset();
			ResetSearchMessage(hwnd,state); LayoutSettings(hwnd, state);
		}
		if ((control == IDC_URL || control == IDC_LATITUDE || control == IDC_LONGITUDE) && notification == EN_CHANGE) {
			state->locationWeather.reset();
			SetControlText(hwnd, control == IDC_URL ? IDC_URL_ERROR : IDC_COORD_ERROR, L"");
			if(control==IDC_URL)UpdateUrlScrollbar(hwnd);LayoutSettings(hwnd, state);
		}
		if (control == IDC_RESULTS && notification == LBN_SELCHANGE) {
			state->locationWeather.reset();
			EnableWindow(SettingsControl(hwnd, IDC_APPLY_LOCATION), SettingsMessage(hwnd, IDC_RESULTS, LB_GETCURSEL, 0, 0) != LB_ERR);
		}
		if (control == IDC_ADVANCED_MODE && notification == CBN_SELCHANGE) { state->locationWeather.reset(); LayoutSettings(hwnd, state); }
		switch (LOWORD(wp)) {
		case IDC_ADVANCED:
			state->advanced = !state->advanced;
			LayoutSettings(hwnd, state); break;
		case IDC_SEARCH: {
			state->locationMessageId = IDC_LOCATION_STATUS;
			ClearLocationSearch(hwnd, state); state->locationWeather.reset();
			std::wstring query = ControlText(hwnd, IDC_QUERY);
			if (query.size() < 2 || query.size() > 100) { LocationMessage(hwnd, state, L"请输入城市或区县名（2～100字）", L"Enter a city/district name (2–100 characters)", L"都市・区名を2～100文字で入力してください"); break; }
			try { state->search = StartLocationSearch(query, SelectedCountry(hwnd), state->language); LocationMessage(hwnd, state, L"搜索中…", L"Searching…", L"検索中…"); }
			catch (...) { LocationMessage(hwnd, state, L"无法启动搜索，请重试", L"Could not start search; retry", L"検索を開始できません。再試行してください"); }
			break;
		}
		case IDC_APPLY_LOCATION: {
			state->locationMessageId = IDC_LOCATION_STATUS;
			LRESULT index = SettingsMessage(hwnd, IDC_RESULTS, LB_GETCURSEL, 0, 0);
			if (index < 0 || static_cast<size_t>(index) >= state->candidates.size()) break;
			const auto& candidate = state->candidates[index]; auto next = state->draft;
			next.locationMode = LocationMode::Search; next.locationName = candidate.name;
			next.locationCountryCode = candidate.countryCode; next.locationCountry = candidate.country; next.locationAdmin = candidate.admin;
			next.latitude = candidate.latitude; next.longitude = candidate.longitude; next.locationTimezone = candidate.timezone;
			next.url = BuildForecastUrl(next.latitude, next.longitude, next.locationTimezone);
			ApplyLocation(hwnd, state, std::move(next)); break;
		}
		case IDC_APPLY_COORDINATES: {
			state->locationMessageId = IDC_COORD_ERROR;
			auto latitude = ControlText(hwnd, IDC_LATITUDE), longitude = ControlText(hwnd, IDC_LONGITUDE);
			if (!ValidCoordinate(latitude, 90) || !ValidCoordinate(longitude, 180)) { LocationMessage(hwnd, state, L"纬度需在 -90～90，经度需在 -180～180。", L"Latitude -90…90; longitude -180…180.", L"緯度は-90～90、経度は-180～180。"); break; }
			auto trim = [](std::wstring text) { size_t a = text.find_first_not_of(L" \t\r\n"), b = text.find_last_not_of(L" \t\r\n"); return text.substr(a, b-a+1); };
			auto next = state->draft; next.locationMode = LocationMode::Coordinates;
			next.locationCountryCode.clear(); next.locationCountry.clear(); next.locationAdmin.clear();
			next.latitude = trim(latitude); next.longitude = trim(longitude); next.locationName = next.latitude + L", " + next.longitude;
			next.locationTimezone = L"auto"; next.url = BuildForecastUrl(next.latitude, next.longitude, L"auto");
			ApplyLocation(hwnd, state, std::move(next)); break;
		}
		case IDC_APPLY_URL: {
			state->locationMessageId = IDC_URL_ERROR;
			BandConfig next;
			if (!ReadCustomLocation(hwnd, state->draft, next)) { LocationMessage(hwnd, state, L"请检查 Open-Meteo 地址", L"Check the Open-Meteo URL", L"Open-Meteo URLを確認してください"); break; }
			ApplyLocation(hwnd, state, std::move(next)); break;
		}
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
					EnableWindow(SettingsControl(hwnd, IDC_CHECK_UPDATE), FALSE);
					SetControlText(hwnd, IDC_UPDATE_STATUS, Text(state->language, L"正在检查 GitHub 正式版本…", L"Checking GitHub releases…", L"GitHub の正式リリースを確認しています…"));
					if (!SetTimer(hwnd, kUpdateTimer, 200, nullptr)) {
						state->update.reset();
						EnableWindow(SettingsControl(hwnd, IDC_CHECK_UPDATE), TRUE);
						SetControlText(hwnd, IDC_UPDATE_STATUS, Text(state->language, L"无法显示检查结果，请稍后重试。", L"Could not display the result. Try again later.", L"確認結果を表示できません。後でもう一度お試しください。"));
					}
				}
				catch (...) { SetControlText(hwnd, IDC_UPDATE_STATUS, Text(state->language, L"无法启动检查，请稍后重试。", L"Could not start the check. Try again later.", L"確認を開始できませんでした。後でもう一度お試しください。")); }
			}
			break;
		}
		if(control==IDC_RELEASES || control==IDC_CHECK_UPDATE)LayoutSettings(hwnd,state);
		return 0;
	}
	case WM_TIMER:
		if (wp == kLocationTimer) { PollLocation(hwnd, state); return 0; }

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
			EnableWindow(SettingsControl(hwnd, IDC_CHECK_UPDATE), TRUE);
			KillTimer(hwnd, kUpdateTimer);
			LayoutSettings(hwnd,state);
		}
		return 0;
	case WM_VSCROLL: {
		SCROLLINFO info{ sizeof(info), SIF_ALL };
		GetScrollInfo(state->content, SB_VERT, &info);
		int next = info.nPos;
		switch (LOWORD(wp)) {
		case SB_LINEUP: next -= Scale(state, 24); break;
		case SB_LINEDOWN: next += Scale(state, 24); break;
		case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
		case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
		case SB_TOP: next = 0; break;
		case SB_BOTTOM: next = info.nMax; break;
		case SB_THUMBTRACK: next = info.nTrackPos; break;
		case SB_THUMBPOSITION: next = HIWORD(wp); break;
		}
		ScrollSettings(state,next);
		return 0;
	}
	case WM_MOUSEWHEEL:
		SendMessageW(hwnd, WM_VSCROLL, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
		return 0;
	case WM_CLOSE: DestroyWindow(hwnd); return 0;
	case WM_DESTROY:
		if(state->content)SetWindowLongPtrW(state->content,GWLP_USERDATA,0);
		KillTimer(hwnd, kUpdateTimer);
		KillTimer(hwnd, kLocationTimer);
		state->search.reset(); state->locationWeather.reset();
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
	INITCOMMONCONTROLSEX common{sizeof(common),ICC_LINK_CLASS};InitCommonControlsEx(&common);
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
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN;
    RECT rect{0,0,S(480),S(360)};AdjustWindowRectExForDpi(&rect,style,FALSE,0,state->dpi);
    int width=(std::min)(rect.right-rect.left,monitor.rcWork.right-monitor.rcWork.left-S(24));
    int height=(std::min)(rect.bottom-rect.top,monitor.rcWork.bottom-monitor.rcWork.top-S(24));
    int x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2;
    int y=monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2;
    HWND hwnd=CreateWindowExW(WS_EX_CONTROLPARENT,className,T(L"天气设置",L"Weather settings",L"天気の設定"),style,x,y,width,height,owner,nullptr,g_hinstDll,state);
    if(!hwnd){DeleteObject(state->font);DeleteObject(state->headingFont);DeleteObject(state->panelBrush);delete state;return nullptr;}
    WNDCLASSEXW contentClass=wc;contentClass.lpszClassName=L"WeatherBarSettingsContent";contentClass.lpfnWndProc=SettingsContentProc;
    RegisterClassExW(&contentClass);
    state->content=CreateWindowExW(WS_EX_CONTROLPARENT,contentClass.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,width,height,hwnd,reinterpret_cast<HMENU>(IDC_CONTENT),g_hinstDll,state);
    if(!state->content){DestroyWindow(hwnd);return nullptr;}
    auto control=[&](const wchar_t* klass,SettingsState::LocalText text,DWORD flags,int id){
     bool footer=id==IDC_RESET || id==IDC_SAVE || id==IDC_CANCEL;
     HWND child=AddControl(footer?hwnd:state->content,klass,T(text.zh,text.en,text.ja),flags,id,0,0,1,1,state->font);
     if(text.zh[0])state->labels.push_back({child,text});return child;
    };
    auto label=[&](SettingsState::LocalText text,int id){return control(L"STATIC",text,SS_LEFT|SS_CENTERIMAGE|SS_NOPREFIX,id);};
    auto section=[&](SettingsState::LocalText text,int id){HWND title=label(text,id);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(state->headingFont),TRUE);};
    auto edit=[&](int id){return control(L"EDIT",{L"",L"",L""},WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,id);};
    auto button=[&](SettingsState::LocalText text,int id){return control(L"BUTTON",text,WS_TABSTOP|BS_PUSHBUTTON,id);};
    auto sizeCombo=[&](int id,SizeLevel level){HWND combo=control(L"COMBOBOX",{L"",L"",L""},WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,id);
     for(auto text:{T(L"小",L"Small",L"小"),T(L"中",L"Medium",L"中"),T(L"大",L"Large",L"大")})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(combo,CB_SETCURSEL,static_cast<int>(level),0);};
    section({L"基础设置",L"Basic settings",L"基本設定"},IDC_WEATHER_TITLE);
    label({L"界面语言",L"Language",L"表示言語"},IDC_LANGUAGE_LABEL);
    HWND languages=control(L"COMBOBOX",{L"",L"",L""},WS_TABSTOP|CBS_DROPDOWNLIST,IDC_LANGUAGE);
    for(auto text:{T(L"跟随系统",L"System default",L"システムに従う"),L"简体中文",L"English",L"日本語"})SendMessageW(languages,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    SendMessageW(languages,CB_SETCURSEL,static_cast<int>(current.language),0);
    label({L"国家/地区",L"Country/region",L"国・地域"},IDC_COUNTRY_LABEL);
    HWND countries=control(L"COMBOBOX",{L"",L"",L""},WS_TABSTOP|CBS_DROPDOWNLIST|CBS_NOINTEGRALHEIGHT|WS_VSCROLL,IDC_COUNTRY);
    size_t count=0;auto countryItems=LocationCountries(count);int selection=0;
    for(size_t i=0;i<count;++i){SendMessageW(countries,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(T(countryItems[i].zh,countryItems[i].en,countryItems[i].ja)));if(current.searchCountry==countryItems[i].code)selection=static_cast<int>(i);}
    SendMessageW(countries,CB_SETCURSEL,selection,0);
    label({L"地点",L"Place",L"地点"},IDC_QUERY_LABEL);HWND query=edit(IDC_QUERY);
    SendMessageW(query,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(T(L"输入城市或区县",L"Enter a city or district",L"都市・区を入力")));SendMessageW(query,EM_SETLIMITTEXT,100,0);
    button({L"搜索",L"Search",L"検索"},IDC_SEARCH);
    control(L"LISTBOX",{L"",L"",L""},WS_TABSTOP|WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,IDC_RESULTS);
    control(L"STATIC",{L"请选择候选，再应用地点。",L"Select a result, then Apply.",L"候補を選んで適用。"},SS_OWNERDRAW,IDC_LOCATION_STATUS);
    button({L"应用地点",L"Apply location",L"地点を適用"},IDC_APPLY_LOCATION);EnableWindow(SettingsControl(hwnd,IDC_APPLY_LOCATION),FALSE);
    control(L"EDIT",{L"",L"",L""},ES_READONLY|ES_AUTOHSCROLL,IDC_CURRENT_LOCATION);control(L"EDIT",{L"",L"",L""},ES_READONLY|ES_AUTOHSCROLL,IDC_CURRENT_ZONE);
    control(L"BUTTON",{L"自定义地点设置",L"Custom location settings",L"地点のカスタム設定"},WS_TABSTOP|BS_OWNERDRAW,IDC_ADVANCED);
    label({L"设置方式",L"Input method",L"設定方法"},IDC_MODE_LABEL);
    HWND mode=control(L"COMBOBOX",{L"",L"",L""},WS_TABSTOP|CBS_DROPDOWNLIST,IDC_ADVANCED_MODE);
    for(auto text:{T(L"手动经纬度",L"Manual coordinates",L"座標を手動入力"),T(L"自定义地址",L"Custom URL",L"カスタムURL")})SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(mode,CB_SETCURSEL,current.locationMode==LocationMode::Custom?1:0,0);
    label({L"WGS84（GPS）经纬度",L"WGS84 (GPS) coordinates",L"WGS84（GPS）緯度・経度"},IDC_COORDINATE_LABEL);
    label({L"纬度",L"Latitude",L"緯度"},IDC_LATITUDE_LABEL);edit(IDC_LATITUDE);label({L"经度",L"Longitude",L"経度"},IDC_LONGITUDE_LABEL);edit(IDC_LONGITUDE);
    control(L"STATIC",{L"",L"",L""},SS_CENTER|SS_CENTERIMAGE,IDC_COORD_ERROR);button({L"应用坐标",L"Apply coordinates",L"座標を適用"},IDC_APPLY_COORDINATES);
    label({L"Open-Meteo API 地址",L"Open-Meteo API URL",L"Open-Meteo API URL"},IDC_URL_LABEL);
    HWND address=control(L"EDIT",{L"",L"",L""},WS_TABSTOP|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,IDC_URL);
    state->addressFont=CreateFontW(-S(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");SendMessageW(address,WM_SETFONT,reinterpret_cast<WPARAM>(state->addressFont),TRUE);SendMessageW(address,EM_SETWORDBREAKPROC,0,reinterpret_cast<LPARAM>(UrlWordBreak));
    control(L"STATIC",{L"",L"",L""},SS_CENTER|SS_CENTERIMAGE,IDC_URL_ERROR);button({L"应用地址",L"Apply URL",L"URLを適用"},IDC_APPLY_URL);
    section({L"显示与外观",L"Display",L"表示と外観"},IDC_DISPLAY_TITLE);
    label({L"项目",L"Item",L"項目"},IDC_ITEM_LABEL);label({L"显示",L"Show",L"表示"},IDC_SHOW_LABEL);label({L"大小",L"Size",L"サイズ"},IDC_SIZE_LABEL);label({L"颜色",L"Color",L"色"},IDC_COLOR_LABEL);
    auto appearance=[&](SettingsState::LocalText text,int labelId,int showId,bool show,int sizeId,SizeLevel level,int colorId,COLORREF color){
     label(text,labelId);if(showId){auto check=control(L"BUTTON",{L"",L"",L""},WS_TABSTOP|BS_AUTOCHECKBOX,showId);Button_SetCheck(check,show?BST_CHECKED:BST_UNCHECKED);}else label({L"始终",L"Always",L"常に"},IDC_ALWAYS_LABEL);
     sizeCombo(sizeId,level);if(colorId){control(L"BUTTON",{L"",L"",L""},WS_TABSTOP|BS_OWNERDRAW,colorId);SetColor(hwnd,colorId,color);}else label({L"—",L"—",L"—"},IDC_ICON_COLOR_LABEL);
    };
    appearance({L"当前温度",L"Current temperature",L"現在の気温"},IDC_CUR_LABEL,0,true,IDC_CUR_SIZE,current.currentLevel,IDC_CUR_COLOR,current.currentColor);
    appearance({L"天气图标",L"Weather icon",L"天気アイコン"},IDC_ICON_LABEL,IDC_SHOW_ICON,current.showIcon,IDC_ICON_SIZE,current.iconLevel,0,0);
    appearance({L"今日最高温",L"Today's high",L"今日の最高気温"},IDC_MAX_LABEL,IDC_SHOW_MAX,current.showMax,IDC_MAX_SIZE,current.maxLevel,IDC_MAX_COLOR,current.maxColor);
    appearance({L"今日最低温",L"Today's low",L"今日の最低気温"},IDC_MIN_LABEL,IDC_SHOW_MIN,current.showMin,IDC_MIN_SIZE,current.minLevel,IDC_MIN_COLOR,current.minColor);
    section({L"间距",L"Spacing",L"間隔"},IDC_SPACING_TITLE);label({L"图标与当前温度",L"Icon to temperature",L"アイコンと気温"},IDC_GAP_ICON_LABEL);sizeCombo(IDC_GAP_ICON,current.gapIconLevel);label({L"当前温度与高低温",L"Temperature to high / low",L"気温と最高・最低気温"},IDC_GAP_EXT_LABEL);sizeCombo(IDC_GAP_EXT,current.gapExtremeLevel);
    section({L"悬停提示",L"Hover tooltip",L"ツールチップ"},IDC_TOOLTIP_TITLE);auto hover=control(L"BUTTON",{L"开启悬停提示",L"Enable tooltip",L"ツールチップを表示"},WS_TABSTOP|BS_AUTOCHECKBOX,IDC_TOOLTIP);Button_SetCheck(hover,current.tooltipEnabled?BST_CHECKED:BST_UNCHECKED);
    SettingsState::LocalText tooltipLabels[]={
     {L"明天天气",L"Tomorrow",L"明日の天気"},{L"紫外线指数",L"UV index",L"紫外線指数"},{L"风向",L"Wind direction",L"風向"},{L"风速",L"Wind speed",L"風速"},{L"相对湿度",L"Relative humidity",L"相対湿度"},{L"体感温度",L"Feels like",L"体感温度"},{L"降水概率",L"Precipitation chance",L"降水確率"},{L"后天天气",L"Day after tomorrow",L"明後日の天気"},{L"上次刷新时间",L"Last refresh time",L"最終更新時刻"},{L"数据状态",L"Data status",L"データ状態"}};
    const int order[]={0,7,1,2,3,4,5,6,8,9};
    for(int field:order){auto check=control(L"BUTTON",tooltipLabels[field],WS_TABSTOP|BS_AUTOCHECKBOX,IDC_TOOLTIP_FIRST+field);bool selected=field==8?current.tooltipRefresh:field==9?current.tooltipStatus:!!(current.tooltipFields&(1u<<field));Button_SetCheck(check,selected?BST_CHECKED:BST_UNCHECKED);EnableWindow(check,current.tooltipEnabled);}
    section({L"关于与更新",L"About & updates",L"このアプリについて"},IDC_ABOUT_TITLE);label({L"",L"",L""},IDC_VERSION);SetControlText(hwnd,IDC_VERSION,std::wstring(L"WeatherBar  ")+kAppVersion);
    control(WC_LINK,{L"<a>数据来源</a>",L"<a>Data sources</a>",L"<a>データ出典</a>"},WS_TABSTOP,IDC_SOURCES);button({L"检查更新",L"Check updates",L"更新を確認"},IDC_CHECK_UPDATE);button({L"发布页面",L"Release page",L"リリースページ"},IDC_RELEASES);
    control(L"STATIC",{L"",L"",L""},SS_LEFT|SS_NOPREFIX,IDC_UPDATE_STATUS);
    button({L"恢复默认",L"Reset appearance",L"外観を初期化"},IDC_RESET);control(L"BUTTON",{L"保存",L"Save",L"保存"},WS_TABSTOP|BS_DEFPUSHBUTTON,IDC_SAVE);button({L"取消",L"Cancel",L"キャンセル"},IDC_CANCEL);

	SetControlText(hwnd, IDC_URL, current.locationMode == LocationMode::Custom ? current.url : L"");
	SetControlText(hwnd, IDC_LATITUDE, current.latitude);
	SetControlText(hwnd, IDC_LONGITUDE, current.longitude);
	state->advanced = current.locationMode == LocationMode::Custom;
	SettingsMessage(hwnd, IDC_URL, EM_SETLIMITTEXT, 2048, 0);
	SettingsMessage(hwnd, IDC_URL, EM_SETSEL, 0, 0);
	SettingsMessage(hwnd, IDC_URL, EM_SCROLLCARET, 0, 0);
	if (band) band->BeginSettingsPreview();
	RefreshLanguage(hwnd, state);
	LocationInfo(hwnd, state);
	if (!current.canSave) SetControlText(hwnd, IDC_LOCATION_STATUS, T(L"配置不可读取或版本过高，原文件已保护。", L"Config unreadable/newer; original file kept.", L"設定を読めないか新しい形式です。元ファイルを保護中。"));
    auto measured=MeasureSettings(hwnd,state);
    // Default and single-column tracking widths are separate; only screen limits permit a default fallback.
    RECT defaultSize{0,0,measured.doubleColumn+SettingsScrollbarWidth(state),S(360)};
    AdjustWindowRectExForDpi(&defaultSize,style,FALSE,0,state->dpi);
    width=(std::min)(defaultSize.right-defaultSize.left,monitor.rcWork.right-monitor.rcWork.left);
    x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2;
    SetWindowPos(hwnd,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
	LayoutSettings(hwnd, state);
    RECT window{},client{};GetWindowRect(hwnd,&window);GetClientRect(hwnd,&client);
    int desired=(std::min)(state->contentHeight+S(kFooterHeight)+static_cast<int>(window.bottom-window.top-client.bottom),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
    SetWindowPos(hwnd,nullptr,x,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-desired)/2,width,desired,SWP_NOZORDER|SWP_NOACTIVATE);
	SetTimer(hwnd, kLocationTimer, 200, nullptr);
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
