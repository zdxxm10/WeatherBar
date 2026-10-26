#include "pch.h"
#include "DeskBand.h"
#include "ClsId.h"
#include "Settings.h"
#include <memory>
#include <sstream>
#include <commctrl.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#define ID_BANDWINDOW 100
#define TIMER_WAKE 1
#define TIMER_DISPLAY 2
#define CMD_SETTINGS 1
#define CMD_REFRESH 2
#define CMD_UNINSTALL 3

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "comctl32.lib")

constexpr UINT_PTR kParentSubclassId = 0x57DB;

HINSTANCE g_hinstDll = NULL;
extern void LockModule(BOOL bLock);

namespace {

RECT MakeRect(int x, int y, int width, int height) {
	RECT rect{ x, y, x + width, y + height };
	return rect;
}

RECT RowBounds(const RECT& client, bool vertical) {
	if (!vertical)
		return client;
	RECT row{ 0, 0, client.bottom - client.top, client.right - client.left };
	return row;
}

RECT MapRowToClient(const RECT& client, const RECT& rowBounds, const RECT& row, bool vertical) {
	if (!vertical)
		return row;
	RECT mapped{};
	mapped.left = client.left + (row.top - rowBounds.top);
	mapped.top = client.top + (row.left - rowBounds.left);
	mapped.right = client.left + (row.bottom - rowBounds.top);
	mapped.bottom = client.top + (row.right - rowBounds.left);
	return mapped;
}

POINT MapClientToRow(const RECT& client, const RECT& rowBounds, POINT clientPoint, bool vertical) {
	if (!vertical)
		return clientPoint;
	POINT row{};
	row.x = rowBounds.left + (clientPoint.y - client.top);
	row.y = rowBounds.top + (clientPoint.x - client.left);
	return row;
}

struct PaintSurface {
	HDC dc = nullptr;
	RGBQUAD* bits = nullptr;
	int stride = 0;
	int width = 0;
	int height = 0;
	bool alpha = false;
};

void BlendCoverage(PaintSurface& surface, int x, int y, COLORREF color, int coverage) {
	if (!surface.bits || coverage <= 0 || x < 0 || y < 0 || x >= surface.width || y >= surface.height)
		return;
	if (coverage > 255)
		coverage = 255;
	RGBQUAD& dest = surface.bits[y * surface.stride + x];
	int inv = 255 - coverage;
	dest.rgbRed = (BYTE)(GetRValue(color) * coverage / 255 + dest.rgbRed * inv / 255);
	dest.rgbGreen = (BYTE)(GetGValue(color) * coverage / 255 + dest.rgbGreen * inv / 255);
	dest.rgbBlue = (BYTE)(GetBValue(color) * coverage / 255 + dest.rgbBlue * inv / 255);
	dest.rgbReserved = (BYTE)(coverage + dest.rgbReserved * inv / 255);
}

template <typename Draw>
bool CompositeCoverage(PaintSurface& surface, const RECT& rect, bool vertical, COLORREF color, Draw&& draw) {
	if (!surface.alpha || !surface.bits || !surface.dc)
		return false;
	int destW = rect.right - rect.left;
	int destH = rect.bottom - rect.top;
	if (destW <= 0 || destH <= 0)
		return false;
	int localW = vertical ? destH : destW;
	int localH = vertical ? destW : destH;
	if (localW <= 0 || localH <= 0 || localW > 4096 || localH > 4096)
		return false;

	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = localW;
	info.bmiHeader.biHeight = -localH;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* raw = nullptr;
	HBITMAP dib = CreateDIBSection(surface.dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
	if (!dib || !raw)
		return false;
	ZeroMemory(raw, (size_t)localW * (size_t)localH * 4);
	HDC mem = CreateCompatibleDC(surface.dc);
	if (!mem) {
		DeleteObject(dib);
		return false;
	}
	HBITMAP old = (HBITMAP)SelectObject(mem, dib);
	SetBkMode(mem, TRANSPARENT);
	SetTextColor(mem, RGB(255, 255, 255));
	draw(mem, localW, localH);
	GdiFlush();
	SelectObject(mem, old);
	DeleteDC(mem);
	GdiFlush();

	const auto* src = static_cast<const RGBQUAD*>(raw);
	bool any = false;
	for (int y = 0; y < localH; ++y) {
		for (int x = 0; x < localW; ++x) {
			const RGBQUAD& pixel = src[y * localW + x];
			int coverage = pixel.rgbRed;
			if (pixel.rgbGreen > coverage)
				coverage = pixel.rgbGreen;
			if (pixel.rgbBlue > coverage)
				coverage = pixel.rgbBlue;
			if (pixel.rgbReserved > coverage)
				coverage = pixel.rgbReserved;
			if (coverage <= 0)
				continue;
			any = true;
			int dx = vertical ? rect.left + y : rect.left + x;
			int dy = vertical ? rect.top + x : rect.top + y;
			BlendCoverage(surface, dx, dy, color, coverage);
		}
	}
	DeleteObject(dib);
	return any;
}

void DrawTextLine(HDC dc, const std::wstring& text, HFONT font, COLORREF color, int width, int height) {
	if (!dc || !font || text.empty() || width <= 0 || height <= 0)
		return;
	HFONT old = (HFONT)SelectObject(dc, font);
	SetTextColor(dc, color);
	TEXTMETRICW tm{};
	GetTextMetricsW(dc, &tm);
	int ink = tm.tmAscent - tm.tmInternalLeading;
	if (ink < 1)
		ink = tm.tmHeight > 0 ? tm.tmHeight : 1;
	int shift = (tm.tmInternalLeading + ink / 2) - (tm.tmHeight / 2);
	RECT target{ 0, -shift, width, height - shift };
	DrawTextW(dc, text.c_str(), -1, &target, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	SelectObject(dc, old);
}

void DrawAlignedText(PaintSurface& surface, const std::wstring& text, HFONT font, COLORREF color, const RECT& rect, bool vertical) {
	if (!font || text.empty())
		return;
	if (CompositeCoverage(surface, rect, vertical, color, [&](HDC dc, int width, int height) {
		DrawTextLine(dc, text, font, RGB(255, 255, 255), width, height);
	}))
		return;
	if (!surface.dc)
		return;
	int saved = 0;
	if (vertical) {
		saved = SaveDC(surface.dc);
		SetGraphicsMode(surface.dc, GM_ADVANCED);
		XFORM transform{};
		transform.eM11 = 0;
		transform.eM12 = 1;
		transform.eM21 = 1;
		transform.eM22 = 0;
		transform.eDx = (float)rect.left;
		transform.eDy = (float)rect.top;
		SetWorldTransform(surface.dc, &transform);
		DrawTextLine(surface.dc, text, font, color, rect.bottom - rect.top, rect.right - rect.left);
		RestoreDC(surface.dc, saved);
		return;
	}
	int savedDc = SaveDC(surface.dc);
	IntersectClipRect(surface.dc, rect.left, rect.top, rect.right, rect.bottom);
	SetViewportOrgEx(surface.dc, rect.left, rect.top, nullptr);
	DrawTextLine(surface.dc, text, font, color, rect.right - rect.left, rect.bottom - rect.top);
	RestoreDC(surface.dc, savedDc);
}

struct TempSlots {
	wchar_t sign = 0;
	wchar_t tens = 0;
	wchar_t ones = 0;
	wchar_t tenth = 0;
};

bool ParseTempSlots(const std::wstring& text, TempSlots& slots) {
	if (text.size() < 4 || text.back() != L'\u2103')
		return false;
	size_t unit = text.size() - 1;
	if (text[unit - 1] < L'0' || text[unit - 1] > L'9' || text[unit - 2] != L'.')
		return false;
	size_t begin = 0;
	slots = TempSlots{};
	if (text[0] == L'-') {
		slots.sign = L'-';
		begin = 1;
	}
	else if (text[0] == L'+') {
		begin = 1;
	}
	size_t intEnd = unit - 2;
	if (intEnd <= begin || intEnd - begin > 2)
		return false;
	for (size_t i = begin; i < intEnd; ++i) {
		if (text[i] < L'0' || text[i] > L'9')
			return false;
	}
	if (intEnd - begin == 2) {
		slots.tens = text[begin];
		slots.ones = text[begin + 1];
	}
	else {
		slots.ones = text[begin];
	}
	slots.tenth = text[unit - 1];
	return true;
}

struct TempGrid {
	int digit = 0;
	int dot = 0;
	int unit = 0;
	int minusW = 0;
	int minusGap = 0;
	int overhang = 0;
	int columns() const { return digit * 3 + dot + unit; }
	int total() const { return overhang + columns(); }
};

int GlyphWidth(HDC dc, wchar_t ch) {
	SIZE size{};
	GetTextExtentPoint32W(dc, &ch, 1, &size);
	return size.cx;
}

TempGrid MeasureTempGrid(HDC dc, HFONT font) {
	TempGrid grid;
	if (!dc || !font)
		return grid;
	HFONT old = (HFONT)SelectObject(dc, font);
	for (wchar_t ch = L'0'; ch <= L'9'; ++ch) {
		int width = GlyphWidth(dc, ch);
		if (width > grid.digit)
			grid.digit = width;
	}
	grid.dot = GlyphWidth(dc, L'.');
	grid.unit = GlyphWidth(dc, L'\u2103');
	SelectObject(dc, old);
	return grid;
}

void PrepareMinus(TempGrid& grid) {
	grid.minusW = grid.digit * 3 / 5;
	if (grid.minusW < 3)
		grid.minusW = 3;
	if (grid.minusW > grid.digit && grid.digit > 0)
		grid.minusW = grid.digit;
	grid.minusGap = grid.digit / 5;
	if (grid.minusGap < 1)
		grid.minusGap = 1;
}

TempGrid WiderGrid(const TempGrid& a, const TempGrid& b) {
	TempGrid grid;
	grid.digit = a.digit > b.digit ? a.digit : b.digit;
	grid.dot = a.dot > b.dot ? a.dot : b.dot;
	grid.unit = a.unit > b.unit ? a.unit : b.unit;
	PrepareMinus(grid);
	return grid;
}

int SignOverhang(HDC dc, const TempSlots& slots, const TempGrid& grid) {
	if (!slots.sign || (!slots.tens && !slots.ones) || grid.digit < 1)
		return 0;
	wchar_t first = slots.tens ? slots.tens : slots.ones;
	int glyphW = GlyphWidth(dc, first);
	if (glyphW > grid.digit)
		glyphW = grid.digit;
	if (glyphW < 1)
		glyphW = 1;
	int slotOffset = slots.tens ? 0 : grid.digit;
	int glyphLeft = slotOffset + (grid.digit - glyphW) / 2;
	int minusLeft = glyphLeft - grid.minusGap - grid.minusW;
	return minusLeft < 0 ? -minusLeft : 0;
}

void ApplyOverhang(HDC dc, TempGrid& grid, bool showMax, HFONT fontMax, const std::wstring& maxText, bool showMin, HFONT fontMin, const std::wstring& minText) {
	int overhang = 0;
	auto consider = [&](bool shown, HFONT font, const std::wstring& text) {
		if (!shown || !dc || !font)
			return;
		TempSlots slots;
		if (!ParseTempSlots(text, slots))
			return;
		HFONT old = (HFONT)SelectObject(dc, font);
		int need = SignOverhang(dc, slots, grid);
		SelectObject(dc, old);
		if (need > overhang)
			overhang = need;
	};
	consider(showMax, fontMax, maxText);
	consider(showMin, fontMin, minText);
	grid.overhang = overhang;
}

void DrawMinusBar(HDC dc, COLORREF color, int x, int y, int width, int height) {
	if (width < 1 || height < 1)
		return;
	int thick = height / 7;
	if (thick < 2)
		thick = 2;
	if (thick > height)
		thick = height;
	int top = y + (height - thick) / 2;
	RECT bar{ x, top, x + width, top + thick };
	HBRUSH brush = CreateSolidBrush(color);
	if (!brush)
		return;
	FillRect(dc, &bar, brush);
	DeleteObject(brush);
}

void DrawSlot(HDC dc, wchar_t ch, int x, int y, int width, int height) {
	if (!ch || width <= 0 || height <= 0)
		return;
	TEXTMETRICW tm{};
	GetTextMetricsW(dc, &tm);
	int ink = tm.tmAscent - tm.tmInternalLeading;
	if (ink < 1)
		ink = tm.tmHeight > 0 ? tm.tmHeight : 1;
	int shift = (tm.tmInternalLeading + ink / 2) - (tm.tmHeight / 2);
	RECT rect{ x, y - shift, x + width, y + height - shift };
	DrawTextW(dc, &ch, 1, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
}

void DrawTemperatureBlock(HDC dc, const std::wstring& text, HFONT font, COLORREF color, int width, int height, const TempGrid& grid) {
	TempSlots slots;
	if (!ParseTempSlots(text, slots) || !dc || !font || width <= 0 || height <= 0)
		return;
	HFONT old = (HFONT)SelectObject(dc, font);
	SetTextColor(dc, color);
	int x = grid.overhang;
	int y = 0;
	if (slots.sign && (slots.tens || slots.ones)) {
		wchar_t first = slots.tens ? slots.tens : slots.ones;
		int glyphW = GlyphWidth(dc, first);
		if (glyphW > grid.digit)
			glyphW = grid.digit;
		if (glyphW < 1)
			glyphW = 1;
		int slotOffset = slots.tens ? 0 : grid.digit;
		int glyphLeft = x + slotOffset + (grid.digit - glyphW) / 2;
		int minusLeft = glyphLeft - grid.minusGap - grid.minusW;
		DrawMinusBar(dc, color, minusLeft, y, grid.minusW, height);
	}
	DrawSlot(dc, slots.tens, x, y, grid.digit, height);
	x += grid.digit;
	DrawSlot(dc, slots.ones, x, y, grid.digit, height);
	x += grid.digit;
	DrawSlot(dc, L'.', x, y, grid.dot, height);
	x += grid.dot;
	DrawSlot(dc, slots.tenth, x, y, grid.digit, height);
	x += grid.digit;
	DrawSlot(dc, L'\u2103', x, y, grid.unit, height);
	SelectObject(dc, old);
}

void DrawFixedTemperature(PaintSurface& surface, const std::wstring& text, HFONT font, COLORREF color, const RECT& rect, bool vertical, const TempGrid& grid) {
	TempSlots slots;
	if (!ParseTempSlots(text, slots) || grid.total() <= 0) {
		DrawAlignedText(surface, text, font, color, rect, vertical);
		return;
	}
	if (CompositeCoverage(surface, rect, vertical, color, [&](HDC dc, int width, int height) {
		DrawTemperatureBlock(dc, text, font, RGB(255, 255, 255), width, height, grid);
	}))
		return;
	if (!surface.dc)
		return;
	int saved = SaveDC(surface.dc);
	if (vertical) {
		SetGraphicsMode(surface.dc, GM_ADVANCED);
		XFORM transform{};
		transform.eM11 = 0;
		transform.eM12 = 1;
		transform.eM21 = 1;
		transform.eM22 = 0;
		transform.eDx = (float)rect.left;
		transform.eDy = (float)rect.top;
		SetWorldTransform(surface.dc, &transform);
		DrawTemperatureBlock(surface.dc, text, font, color, rect.bottom - rect.top, rect.right - rect.left, grid);
	}
	else {
		IntersectClipRect(surface.dc, rect.left, rect.top, rect.right, rect.bottom);
		SetViewportOrgEx(surface.dc, rect.left, rect.top, nullptr);
		DrawTemperatureBlock(surface.dc, text, font, color, rect.right - rect.left, rect.bottom - rect.top, grid);
	}
	RestoreDC(surface.dc, saved);
}

}

STDMETHODIMP CDeskBand::QueryInterface(REFIID riid, void** ppvObject)
{
	*ppvObject = NULL;
	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IOleWindow) || IsEqualIID(riid, IID_IDockingWindow) || IsEqualIID(riid, IID_IDeskBand) || IsEqualIID(riid, IID_IDeskBand2))
		*ppvObject = static_cast<IDeskBand*>(this);
	else if (IsEqualIID(riid, IID_IPersist) || IsEqualIID(riid, IID_IPersistStream))
		*ppvObject = static_cast<IPersistStream*>(this);
	else if (IsEqualIID(riid, IID_IObjectWithSite))
		*ppvObject = static_cast<IObjectWithSite*>(this);
	else
		return E_NOINTERFACE;
	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) CDeskBand::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CDeskBand::Release()
{
	if (InterlockedDecrement(&m_cRef) == 0) {
		delete this;
		return 0;
	}
	return m_cRef;
}

STDMETHODIMP CDeskBand::ContextSensitiveHelp(BOOL)
{
	return E_NOTIMPL;
}

STDMETHODIMP CDeskBand::GetWindow(HWND* phwnd)
{
	*phwnd = m_hwnd;
	return S_OK;
}

STDMETHODIMP CDeskBand::ShowDW(BOOL bShow)
{
	if (m_hwnd)
		ShowWindow(m_hwnd, bShow ? SW_SHOW : SW_HIDE);
	return S_OK;
}

STDMETHODIMP CDeskBand::CloseDW(DWORD)
{
	Shutdown();
	return S_OK;
}

STDMETHODIMP CDeskBand::ResizeBorderDW(LPCRECT, IUnknown*, BOOL)
{
	return E_NOTIMPL;
}

STDMETHODIMP CDeskBand::GetBandInfo(DWORD, DWORD dwViewMode, DESKBANDINFO* pdbi)
{
	if (pdbi == NULL)
		return E_INVALIDARG;
	bool nextVertical = verticalBar;
	if (dwViewMode == DBIF_VIEWMODE_VERTICAL)
		nextVertical = true;
	else if (dwViewMode == DBIF_VIEWMODE_NORMAL || dwViewMode == DBIF_VIEWMODE_FLOATING)
		nextVertical = false;
	if (nextVertical != verticalBar) {
		verticalBar = nextVertical;
		metricsReady = false;
		builtCurrentPx = 0;
	}
	CaptureEnvironment();
	Metrics metrics = Measure();
	int thickness = metrics.height;
	int limit = HeightBudget();
	if (limit > 0 && thickness > limit)
		thickness = limit;
	if (pdbi->dwMask & DBIM_MINSIZE) {
		pdbi->ptMinSize.x = verticalBar ? thickness : metrics.width;
		pdbi->ptMinSize.y = verticalBar ? metrics.width : thickness;
	}
	if (pdbi->dwMask & DBIM_MAXSIZE) {
		pdbi->ptMaxSize.x = verticalBar ? thickness : -1;
		pdbi->ptMaxSize.y = verticalBar ? metrics.width : thickness;
	}
	if (pdbi->dwMask & DBIM_INTEGRAL)
		pdbi->ptIntegral.y = 1;
	if (pdbi->dwMask & DBIM_ACTUAL) {
		pdbi->ptActual.x = verticalBar ? thickness : metrics.width;
		pdbi->ptActual.y = verticalBar ? metrics.width : thickness;
	}
	if (pdbi->dwMask & DBIM_TITLE)
		pdbi->dwMask &= ~DBIM_TITLE;
	if (pdbi->dwMask & DBIM_MODEFLAGS)
		pdbi->dwModeFlags = DBIMF_NORMAL;
	if (pdbi->dwMask & DBIM_BKCOLOR)
		pdbi->dwMask &= ~DBIM_BKCOLOR;
	return S_OK;
}

STDMETHODIMP CDeskBand::CanRenderComposited(BOOL* pfCanRenderComposited)
{
	*pfCanRenderComposited = TRUE;
	return S_OK;
}

STDMETHODIMP CDeskBand::SetCompositionState(BOOL fCompositionEnabled)
{
	m_bCompositionEnabled = fCompositionEnabled;
	InvalidateBand();
	return S_OK;
}

STDMETHODIMP CDeskBand::GetCompositionState(BOOL* pfCompositionEnabled)
{
	*pfCompositionEnabled = m_bCompositionEnabled;
	return S_OK;
}

STDMETHODIMP CDeskBand::SetSite(IUnknown* pUnkSite)
{
	if (m_pSite) {
		m_pSite->Release();
		m_pSite = nullptr;
	}
	if (pUnkSite) {
		HWND hwndParent = nullptr;
		IOleWindow* oleWindow = nullptr;
		if (SUCCEEDED(pUnkSite->QueryInterface(IID_PPV_ARGS(&oleWindow))) && oleWindow) {
			oleWindow->GetWindow(&hwndParent);
			oleWindow->Release();
		}
		if (hwndParent)
			RegisterAndCreateWindow(hwndParent);
		pUnkSite->QueryInterface(IID_PPV_ARGS(&m_pSite));
	}
	return S_OK;
}

STDMETHODIMP CDeskBand::GetSite(REFIID riid, void** ppvSite)
{
	*ppvSite = nullptr;
	if (!m_pSite)
		return E_FAIL;
	return m_pSite->QueryInterface(riid, ppvSite);
}

STDMETHODIMP CDeskBand::GetClassID(CLSID* pClassID)
{
	*pClassID = CLSID_WeatherBar;
	return S_OK;
}

STDMETHODIMP CDeskBand::IsDirty()
{
	return S_FALSE;
}

STDMETHODIMP CDeskBand::Load(IStream*)
{
	return S_OK;
}

STDMETHODIMP CDeskBand::Save(IStream*, BOOL)
{
	return S_OK;
}

STDMETHODIMP CDeskBand::GetSizeMax(ULARGE_INTEGER*)
{
	return E_NOTIMPL;
}

CDeskBand::CDeskBand()
{
	config = LoadBandConfig();
	NormalizeBandConfig(config);
	if (GetFileAttributesW(ConfigFilePath().c_str()) == INVALID_FILE_ATTRIBUTES)
		SaveBandConfig(config);
	_weather.SetURL(config.url);
	LockModule(TRUE);
}

CDeskBand::~CDeskBand()
{
	Shutdown();
	LockModule(FALSE);
}

void CDeskBand::Shutdown()
{
	if (shuttingDown)
		return;
	shuttingDown = true;
	++generation;
	UnhookParent();
	if (m_hwnd) {
		KillTimer(m_hwnd, TIMER_WAKE);
		KillTimer(m_hwnd, TIMER_DISPLAY);
	}
	if (settingsHwnd) {
		HWND settings = settingsHwnd;
		settingsHwnd = nullptr;
		CloseBandSettings(settings);
	}
	if (tooltipHwnd) {
		DestroyWindow(tooltipHwnd);
		tooltipHwnd = nullptr;
	}
	if (m_hwnd) {
		HWND hwnd = m_hwnd;
		m_hwnd = nullptr;
		DestroyWindow(hwnd);
	}
	ReleaseGraphics();
}

void CDeskBand::BeginSettingsPreview()
{
	if (!settingsSnapshot)
		settingsSnapshot = SettingsSnapshot{ config, _weather.cache, lastSuccessfulFetch, lastFetchFailed, failedUrl };
}

void CDeskBand::CancelSettingsPreview()
{
	if (!settingsSnapshot) return;
	SettingsSnapshot original = std::move(*settingsSnapshot);
	settingsSnapshot.reset();
	// 地点预览可能已替换天气缓存；取消时配置和显示数据必须一起恢复。
	_weather.cache = std::move(original.forecast);
	lastSuccessfulFetch = original.lastSuccessfulFetch;
	lastFetchFailed = original.lastFetchFailed;
	failedUrl = std::move(original.failedUrl);
	ApplyPreviewConfig(original.config);
}

bool CDeskBand::ApplySavedConfig(const BandConfig& incoming)
{
	BandConfig next = incoming;
	next.icons = config.icons;
	NormalizeBandConfig(next);
	if (!SaveBandConfig(next)) return false;
	settingsSnapshot.reset();
	ApplyPreviewConfig(next);
	return true;
}

void CDeskBand::ApplyPreviewConfig(const BandConfig& incoming)
{
	BandConfig next = incoming;
	next.icons = config.icons;
	NormalizeBandConfig(next);
	bool dataChanged = next.url != config.url || next.timezoneOffsetHours != config.timezoneOffsetHours;
	config = std::move(next);
	if (dataChanged) ++generation;
	_weather.SetURL(config.url);
	// 预览仅改内存，字体和 SVG 仍按实际像素变化复用；保存是唯一的落盘入口。
	metricsReady = false;
	ArmTimer();
	RequestResize();
	InvalidateBand();
	if (dataChanged) StartFetch();
}

void CDeskBand::OnSettingsClosed()
{
	settingsHwnd = nullptr;
}

BOOL CDeskBand::RegisterAndCreateWindow(HWND hwndParent)
{
	const wchar_t* className = L"WeatherBar";
	WNDCLASSEXW wc{};
	if (!GetClassInfoExW(g_hinstDll, className, &wc)) {
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = WindowProc;
		wc.hInstance = g_hinstDll;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = className;
		if (!RegisterClassExW(&wc))
			return FALSE;
	}
	m_hwnd = CreateWindowExW(0, className, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwndParent, (HMENU)ID_BANDWINDOW, g_hinstDll, this);
	if (m_hwnd)
		HookParent(hwndParent);
	return m_hwnd != nullptr;
}

void CDeskBand::OnCreate(HWND hwnd)
{
	m_hwnd = hwnd;
	INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
	InitCommonControlsEx(&controls);
	tooltipHwnd = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, g_hinstDll, nullptr);
	if (tooltipHwnd) {
		TOOLINFOW tool{ sizeof(tool) };
		tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		tool.hwnd = hwnd;
		tool.uId = reinterpret_cast<UINT_PTR>(hwnd);
		tool.lpszText = LPSTR_TEXTCALLBACKW;
		SendMessageW(tooltipHwnd, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
		SendMessageW(tooltipHwnd, TTM_SETMAXTIPWIDTH, 0, 440);
		SendMessageW(tooltipHwnd, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
	}
	UpdateDpi(hwnd);
	lastDateKey = DateKeyFromUtc(time(nullptr), config.timezoneOffsetHours);
	ArmTimer();
	StartFetch();
}

int CDeskBand::DpiPx(int dip) const
{
	int effective = dpi > 0 ? dpi : 96;
	int px = MulDiv(dip, effective, 96);
	return px < 1 ? 1 : px;
}

int CDeskBand::HeightBudget() const
{
	if (m_hwnd) {
		RECT client{};
		if (GetClientRect(m_hwnd, &client)) {
			int span = verticalBar ? client.right - client.left : client.bottom - client.top;
			if (span >= 16)
				return span;
		}
	}
	if (barThickness < 16)
		return 0;
	int inset = DpiPx(4);
	return barThickness > inset ? barThickness - inset : barThickness;
}

int CDeskBand::TaskbarShortSide() const
{
	HWND anchor = m_hwnd ? m_hwnd : GetDesktopWindow();
	HMONITOR monitor = MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST);
	HWND tray = nullptr;
	auto consider = [&](HWND candidate) {
		if (!candidate)
			return;
		if (MonitorFromWindow(candidate, MONITOR_DEFAULTTONEAREST) == monitor)
			tray = candidate;
	};
	consider(FindWindowW(L"Shell_TrayWnd", nullptr));
	HWND next = nullptr;
	while ((next = FindWindowExW(nullptr, next, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr)
		consider(next);
	if (!tray)
		return 0;
	RECT bounds{};
	if (!GetWindowRect(tray, &bounds))
		return 0;
	int width = bounds.right - bounds.left;
	int height = bounds.bottom - bounds.top;
	int shortSide = width < height ? width : height;
	return shortSide >= 16 ? shortSide : 0;
}

bool CDeskBand::CaptureEnvironment()
{
	int previousDpi = dpi;
	int previousThickness = barThickness;
	bool previousVertical = verticalBar;
	UpdateDpi(m_hwnd);
	int thickness = TaskbarShortSide();
	if (thickness <= 0)
		thickness = previousThickness;
	barThickness = thickness;
	HWND anchor = m_hwnd ? m_hwnd : GetDesktopWindow();
	HMONITOR monitor = MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST);
	bool changed = dpi != previousDpi || thickness != previousThickness || verticalBar != previousVertical || monitor != cachedMonitor;
	cachedMonitor = monitor;
	if (!changed)
		return false;
	metricsReady = false;
	builtCurrentPx = 0;
	return true;
}

void CDeskBand::UpdateDpi(HWND hwnd)
{
	UINT windowDpi = 0;
	if (hwnd)
		windowDpi = GetDpiForWindow(hwnd);
	HWND anchor = hwnd ? hwnd : GetDesktopWindow();
	HMONITOR monitor = MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST);
	UINT monitorX = 0;
	UINT monitorY = 0;
	UINT next = 0;
	if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &monitorX, &monitorY)))
		next = monitorY;
	if (next == 0)
		next = windowDpi;
	if (next == 0)
		next = GetDpiForSystem();
	if (next == 0)
		next = 96;
	dpi = (int)next;
}

HFONT CDeskBand::MakeFontPx(int px) const
{
	LOGFONTW lf{};
	lf.lfHeight = px > 0 ? -px : -1;
	lf.lfWeight = FW_NORMAL;
	lf.lfQuality = ANTIALIASED_QUALITY;
	lf.lfOutPrecision = OUT_TT_PRECIS;
	wcscpy_s(lf.lfFaceName, L"Segoe UI");
	return CreateFontIndirectW(&lf);
}

void CDeskBand::ReleaseGraphics()
{
	if (fontCurrent) {
		DeleteObject(fontCurrent);
		fontCurrent = nullptr;
	}
	if (fontMax) {
		DeleteObject(fontMax);
		fontMax = nullptr;
	}
	if (fontMin) {
		DeleteObject(fontMin);
		fontMin = nullptr;
	}
	weathericons.Clear();
	builtCurrentPx = 0;
	builtMaxPx = 0;
	builtMinPx = 0;
	builtIconPx = -1;
}

void CDeskBand::EnsureFonts(int currentPx, int maxPx, int minPx)
{
	if (currentPx < 1)
		currentPx = 1;
	if (maxPx < 1)
		maxPx = 1;
	if (minPx < 1)
		minPx = 1;
	if (builtCurrentPx == currentPx && builtMaxPx == maxPx && builtMinPx == minPx && fontCurrent && fontMax && fontMin)
		return;
	if (fontCurrent) {
		DeleteObject(fontCurrent);
		fontCurrent = nullptr;
	}
	if (fontMax) {
		DeleteObject(fontMax);
		fontMax = nullptr;
	}
	if (fontMin) {
		DeleteObject(fontMin);
		fontMin = nullptr;
	}
	fontCurrent = MakeFontPx(currentPx);
	fontMax = MakeFontPx(maxPx);
	fontMin = MakeFontPx(minPx);
	builtCurrentPx = currentPx;
	builtMaxPx = maxPx;
	builtMinPx = minPx;
}

void CDeskBand::EnsureVisuals(int iconPx)
{
	if (iconPx < 0)
		iconPx = 0;
	if (builtIconPx == iconPx && (iconPx == 0 || weathericons.ImageList))
		return;
	weathericons.Clear();
	if (iconPx > 0) {
		weathericons.SetFileList(config.icons);
		weathericons.MakeImageList(iconPx);
	}
	builtIconPx = iconPx;
}

int CDeskBand::GapPx(int cellBudget, SizeLevel level) const
{
	int full = cellBudget > 0 ? cellBudget : DpiPx(32);
	int gap = MulDiv(full, GapPercent(level), 100);
	return gap < 1 ? 1 : gap;
}

int CDeskBand::TextWidth(HDC dc, HFONT font, const std::wstring& text, int extra) const
{
	if (!dc || !font || text.empty())
		return 0;
	if (extra < 0)
		extra = 0;
	HFONT old = (HFONT)SelectObject(dc, font);
	RECT measured{ 0, 0, 0, 0 };
	DrawTextW(dc, text.c_str(), -1, &measured, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
	SelectObject(dc, old);
	int width = measured.right - measured.left;
	return width + extra;
}

int CDeskBand::FontHeight(HDC dc, HFONT font) const
{
	if (!dc || !font)
		return DpiPx(8);
	HFONT old = (HFONT)SelectObject(dc, font);
	TEXTMETRICW metrics{};
	GetTextMetricsW(dc, &metrics);
	SelectObject(dc, old);
	return metrics.tmHeight;
}

CDeskBand::Metrics CDeskBand::MeasureBody(const Metrics& seed)
{
	Metrics metrics = seed;
	if (metrics.currentPx < 1)
		metrics.currentPx = 1;
	if (metrics.maxPx < 1)
		metrics.maxPx = 1;
	if (metrics.minPx < 1)
		metrics.minPx = 1;
	if (metrics.slack < 0)
		metrics.slack = 0;
	int textExtra = metrics.slack * 2;

	HDC dc = m_hwnd ? GetDC(m_hwnd) : GetDC(nullptr);
	HFONT fontC = MakeFontPx(metrics.currentPx);
	HFONT fontM = MakeFontPx(metrics.maxPx);
	HFONT fontN = MakeFontPx(metrics.minPx);
	if (!metrics.showMax)
		metrics.maxLineH = 0;
	if (!metrics.showMin)
		metrics.minLineH = 0;
	metrics.currentH = metrics.currentPx;
	metrics.currentW = TextWidth(dc, fontC, metrics.currentText, textExtra);
	TempGrid maxGrid = metrics.showMax ? MeasureTempGrid(dc, fontM) : TempGrid{};
	TempGrid minGrid = metrics.showMin ? MeasureTempGrid(dc, fontN) : TempGrid{};
	TempGrid extremeGrid = WiderGrid(maxGrid, minGrid);
	ApplyOverhang(dc, extremeGrid, metrics.showMax, fontM, metrics.maxText, metrics.showMin, fontN, metrics.minText);
	int fixedW = extremeGrid.total();
	if (fixedW > 0)
		fixedW += metrics.slack;
	auto extremeWidth = [&](bool shown, HFONT font, const std::wstring& text) {
		if (!shown)
			return 0;
		TempSlots slots;
		if (ParseTempSlots(text, slots))
			return fixedW;
		return TextWidth(dc, font, text.empty() ? L"--" : text, textExtra);
	};
	int maxW = extremeWidth(metrics.showMax, fontM, metrics.maxText);
	int minW = extremeWidth(metrics.showMin, fontN, metrics.minText);
	if (fontC)
		DeleteObject(fontC);
	if (fontM)
		DeleteObject(fontM);
	if (fontN)
		DeleteObject(fontN);
	if (dc)
		ReleaseDC(m_hwnd ? m_hwnd : nullptr, dc);

	int extremeH = metrics.maxLineH + metrics.minLineH;
	metrics.extW = maxW > minW ? maxW : minW;
	metrics.innerH = metrics.iconPx;
	if (metrics.currentH > metrics.innerH)
		metrics.innerH = metrics.currentH;
	if (extremeH > metrics.innerH)
		metrics.innerH = extremeH;
	metrics.width = metrics.padX + metrics.iconPx + metrics.gapIcon + metrics.currentW + metrics.gapExt + metrics.extW + metrics.padX;
	metrics.height = metrics.innerH + metrics.padY * 2;
	return metrics;
}

CDeskBand::Metrics CDeskBand::Measure()
{
	UpdateDpi(m_hwnd);
	Metrics identity;
	time_t now = time(nullptr);
	tm local{};
	BreakLocalTime(now, config.timezoneOffsetHours, local);
	int today = DateKeyFromUtc(now, config.timezoneOffsetHours);
	const DayForecast* day = _weather.FindDay(today);
	identity.currentText = L"--";
	identity.weatherCode = -1;
	if (day) {
		int hour = local.tm_hour;
		if (hour >= 0 && hour <= 23 && day->hours[hour].valid) {
			identity.currentText = day->hours[hour].temperature;
			identity.weatherCode = day->hours[hour].code;
		}
		identity.maxText = day->maxTemperature;
		identity.minText = day->minTemperature;
	}
	identity.showIcon = config.showIcon;
	identity.showMax = config.showMax;
	identity.showMin = config.showMin;
	int budget = HeightBudget();
	if (metricsReady &&
		cachedDpi == dpi &&
		cachedBudget == budget &&
		cachedVertical == verticalBar &&
		cachedCurrentLevel == config.currentLevel &&
		cachedMaxLevel == config.maxLevel &&
		cachedMinLevel == config.minLevel &&
		cachedIconLevel == config.iconLevel &&
		cachedGapIconLevel == config.gapIconLevel &&
		cachedGapExtremeLevel == config.gapExtremeLevel &&
		metricsCache.currentText == identity.currentText &&
		metricsCache.maxText == identity.maxText &&
		metricsCache.minText == identity.minText &&
		metricsCache.weatherCode == identity.weatherCode &&
		metricsCache.showIcon == identity.showIcon &&
		metricsCache.showMax == identity.showMax &&
		metricsCache.showMin == identity.showMin)
		return metricsCache;

	int padY = DpiPx(2);
	int cellBudget = budget > padY * 2 ? budget - padY * 2 : budget;
	int full = cellBudget > 0 ? cellBudget : DpiPx(32);
	int currentPx = LevelPixels(config.currentLevel, full);
	int extremeFull = full;
	if (identity.showMax && identity.showMin) {
		extremeFull = full / 2;
		if (extremeFull < 1)
			extremeFull = 1;
	}
	int maxPx = LevelPixels(config.maxLevel, extremeFull);
	int minPx = LevelPixels(config.minLevel, extremeFull);
	if (cellBudget > 0 && currentPx > cellBudget)
		currentPx = cellBudget;
	if (maxPx < 1)
		maxPx = 1;
	if (minPx < 1)
		minPx = 1;
	identity.currentPx = currentPx;
	identity.maxPx = maxPx;
	identity.minPx = minPx;
	identity.currentH = currentPx;
	identity.maxLineH = identity.showMax ? maxPx : 0;
	identity.minLineH = identity.showMin ? minPx : 0;
	identity.iconPx = 0;
	if (identity.showIcon) {
		identity.iconPx = LevelPixels(config.iconLevel, full);
		if (cellBudget > 0 && identity.iconPx > cellBudget)
			identity.iconPx = cellBudget;
	}
	identity.padX = DpiPx(4);
	identity.padY = padY;
	identity.slack = DpiPx(2);
	identity.gapIcon = identity.showIcon ? GapPx(cellBudget, config.gapIconLevel) : 0;
	identity.gapExt = (identity.showMax || identity.showMin) ? GapPx(cellBudget, config.gapExtremeLevel) : 0;
	Metrics measured = MeasureBody(identity);
	if (budget > 0 && measured.height > budget)
		measured.height = budget;
	int minimum = DpiPx(48);
	if (measured.width < minimum)
		measured.width = minimum;
	metricsCache = measured;
	cachedDpi = dpi;
	cachedBudget = budget;
	cachedVertical = verticalBar;
	cachedCurrentLevel = config.currentLevel;
	cachedMaxLevel = config.maxLevel;
	cachedMinLevel = config.minLevel;
	cachedIconLevel = config.iconLevel;
	cachedGapIconLevel = config.gapIconLevel;
	cachedGapExtremeLevel = config.gapExtremeLevel;
	metricsReady = true;
	++metricsGeneration;
	return metricsCache;
}

int ScaleAtLeastOne(int value, int numer, int denom) {
	if (value <= 0 || numer <= 0 || denom <= 0)
		return 0;
	int scaled = MulDiv(value, numer, denom);
	return scaled < 1 ? 1 : scaled;
}

int ScaleOrZero(int value, int numer, int denom) {
	if (value <= 0 || numer <= 0 || denom <= 0)
		return 0;
	return MulDiv(value, numer, denom);
}

CDeskBand::Metrics CDeskBand::FitForWidth(const Metrics& preferred, int available)
{
	if (available <= 0 || preferred.width <= available)
		return preferred;
	if (fitGeneration == metricsGeneration && fitLimit == available)
		return fitCache;

	int scale = MulDiv(available, 1000, preferred.width);
	if (scale < 1)
		scale = 1;
	if (scale > 1000)
		scale = 1000;
	Metrics best = preferred;
	for (int attempt = 0; attempt < 8; ++attempt) {
		Metrics trial = preferred;
		trial.currentPx = ScaleAtLeastOne(preferred.currentPx, scale, 1000);
		trial.maxPx = preferred.showMax ? ScaleAtLeastOne(preferred.maxPx, scale, 1000) : preferred.maxPx;
		trial.minPx = preferred.showMin ? ScaleAtLeastOne(preferred.minPx, scale, 1000) : preferred.minPx;
		trial.iconPx = ScaleAtLeastOne(preferred.iconPx, scale, 1000);
		trial.currentH = trial.currentPx;
		trial.maxLineH = preferred.showMax ? trial.maxPx : 0;
		trial.minLineH = preferred.showMin ? trial.minPx : 0;
		trial.gapIcon = ScaleOrZero(preferred.gapIcon, scale, 1000);
		trial.gapExt = ScaleOrZero(preferred.gapExt, scale, 1000);
		trial.padX = ScaleOrZero(preferred.padX, scale, 1000);
		trial.padY = ScaleOrZero(preferred.padY, scale, 1000);
		trial.slack = ScaleOrZero(preferred.slack, scale, 1000);
		trial = MeasureBody(trial);
		best = trial;
		if (trial.width <= available || scale <= 1)
			break;
		int next = MulDiv(scale, available, trial.width > 0 ? trial.width : available);
		if (next >= scale)
			next = scale - 1;
		if (next < 1)
			next = 1;
		if (next == scale)
			break;
		scale = next;
	}
	fitCache = best;
	fitGeneration = metricsGeneration;
	fitLimit = available;
	return fitCache;
}

CDeskBand::Metrics CDeskBand::MetricsForClient(const RECT& client)
{
	RECT row = RowBounds(client, verticalBar);
	return FitForWidth(Measure(), row.right - row.left);
}

CDeskBand::Placement CDeskBand::LayoutIn(const RECT& bounds, const Metrics& metrics) const
{
	Placement placement;
	placement.width = metrics.width;
	placement.height = metrics.height;
	placement.showIcon = metrics.showIcon;
	placement.showMax = metrics.showMax;
	placement.showMin = metrics.showMin;
	placement.currentText = metrics.currentText;
	placement.maxText = metrics.maxText;
	placement.minText = metrics.minText;
	placement.weatherCode = metrics.weatherCode;

	int boundsW = bounds.right - bounds.left;
	int boundsH = bounds.bottom - bounds.top;
	int originX = bounds.left + (boundsW - metrics.width) / 2;
	int originY = bounds.top + (boundsH - metrics.height) / 2;
	if (originX < bounds.left)
		originX = bounds.left;
	if (originY < bounds.top)
		originY = bounds.top;
	int x = originX + metrics.padX;
	int y = originY + metrics.padY;
	if (metrics.showIcon) {
		int iconY = y + (metrics.innerH - metrics.iconPx) / 2;
		placement.icon = MakeRect(x, iconY, metrics.iconPx, metrics.iconPx);
		x += metrics.iconPx + metrics.gapIcon;
	}
	placement.current = MakeRect(x, y, metrics.currentW, metrics.innerH);
	x += metrics.currentW + metrics.gapExt;
	int stack = metrics.maxLineH + metrics.minLineH;
	int extra = metrics.innerH - stack;
	if (extra < 0)
		extra = 0;
	int lineY = y + extra / 2;
	if (metrics.showMax) {
		placement.maxTemp = MakeRect(x, lineY, metrics.extW, metrics.maxLineH);
		lineY += metrics.maxLineH;
	}
	if (metrics.showMin)
		placement.minTemp = MakeRect(x, lineY, metrics.extW, metrics.minLineH);
	return placement;
}

void CDeskBand::Paint(HWND hwnd)
{
	if (CaptureEnvironment())
		RequestResize();
	RECT client{};
	GetClientRect(hwnd, &client);
	Metrics preferred = Measure();
	if (reportedWidth != preferred.width || reportedHeight != preferred.height) {
		reportedWidth = preferred.width;
		reportedHeight = preferred.height;
		RequestResize();
	}
	RECT row = RowBounds(client, verticalBar);
	Metrics metrics = FitForWidth(preferred, row.right - row.left);
	EnsureFonts(metrics.currentPx, metrics.maxPx, metrics.minPx);
	EnsureVisuals(metrics.showIcon ? metrics.iconPx : 0);
	PAINTSTRUCT ps{};
	HDC hdc = BeginPaint(hwnd, &ps);
	Placement placement = LayoutIn(row, metrics);

	HDC drawDc = hdc;
	HPAINTBUFFER buffer = nullptr;
	if (hdc) {
		HDC buffered = nullptr;
		buffer = BeginBufferedPaint(hdc, &client, BPBF_TOPDOWNDIB, nullptr, &buffered);
		if (buffer && buffered)
			drawDc = buffered;
	}
	PaintSurface surface;
	surface.dc = drawDc;
	if (buffer && drawDc) {
		RGBQUAD* bits = nullptr;
		int rowStride = 0;
		if (SUCCEEDED(GetBufferedPaintBits(buffer, &bits, &rowStride)) && bits && rowStride > 0) {
			surface.bits = bits;
			surface.stride = rowStride;
			surface.width = client.right - client.left;
			surface.height = client.bottom - client.top;
			surface.alpha = surface.width > 0 && surface.height > 0 && surface.stride >= surface.width;
		}
	}
	if (drawDc) {
		if (m_bCompositionEnabled)
			DrawThemeParentBackground(hwnd, drawDc, &client);
		else
			FillRect(drawDc, &client, GetSysColorBrush(COLOR_MENU));
		SetBkMode(drawDc, TRANSPARENT);

		auto cell = [&](const RECT& rowRect) {
			return MapRowToClient(client, row, rowRect, verticalBar);
		};
		if (placement.showIcon && weathericons.ImageList) {
			auto found = weathericons.codetoindex.find(placement.weatherCode);
			if (found != weathericons.codetoindex.end()) {
				RECT icon = cell(placement.icon);
				ImageList_Draw(weathericons.ImageList, found->second, drawDc, icon.left, icon.top, ILD_TRANSPARENT);
			}
		}

		DrawAlignedText(surface, placement.currentText, fontCurrent, config.currentColor, cell(placement.current), verticalBar);
		TempGrid extremeGrid = WiderGrid(
			placement.showMax ? MeasureTempGrid(drawDc, fontMax) : TempGrid{},
			placement.showMin ? MeasureTempGrid(drawDc, fontMin) : TempGrid{});
		ApplyOverhang(drawDc, extremeGrid, placement.showMax, fontMax, placement.maxText, placement.showMin, fontMin, placement.minText);
		if (placement.showMax)
			DrawFixedTemperature(surface, placement.maxText, fontMax, config.maxColor, cell(placement.maxTemp), verticalBar, extremeGrid);
		if (placement.showMin)
			DrawFixedTemperature(surface, placement.minText, fontMin, config.minColor, cell(placement.minTemp), verticalBar, extremeGrid);
	}
	if (buffer)
		EndBufferedPaint(buffer, TRUE);
	EndPaint(hwnd, &ps);
}

void CDeskBand::InvalidateBand()
{
	if (m_hwnd)
		InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CDeskBand::RequestResize()
{
	if (!m_pSite)
		return;
	IOleCommandTarget* command = nullptr;
	if (FAILED(m_pSite->QueryInterface(IID_PPV_ARGS(&command))) || !command)
		return;
	command->Exec(&CGID_DeskBand, DBID_BANDINFOCHANGED, OLECMDEXECOPT_DODEFAULT, nullptr, nullptr);
	command->Release();
}

void CDeskBand::ArmTimer()
{
	if (!m_hwnd || shuttingDown)
		return;
	time_t now = time(nullptr);
	time_t hourAt = NextHourUtc(now, config.timezoneOffsetHours);
	time_t slotAt = NextScheduleUtc(now, config.timezoneOffsetHours, config.scheduleHours.data(), (int)config.scheduleHours.size(), config.delaySeconds);
	time_t wake = hourAt < slotAt ? hourAt : slotAt;
	armedForSchedule = slotAt <= hourAt;
	armedUntil = wake;
	long long milliseconds = 1000;
	if (wake > now) {
		milliseconds = ((long long)wake - (long long)now) * 1000LL;
		if (milliseconds < 200)
			milliseconds = 200;
		if (milliseconds > 0x7FFFFFFF)
			milliseconds = 0x7FFFFFFF;
	}
	KillTimer(m_hwnd, TIMER_WAKE);
	SetTimer(m_hwnd, TIMER_WAKE, (UINT)milliseconds, nullptr);
}

void CDeskBand::OnTimer()
{
	if (!m_hwnd || shuttingDown)
		return;
	KillTimer(m_hwnd, TIMER_WAKE);
	time_t now = time(nullptr);
	int today = DateKeyFromUtc(now, config.timezoneOffsetHours);
	bool fetch = armedForSchedule && now + 2 >= armedUntil;
	if (today != lastDateKey) {
		lastDateKey = today;
		if (!_weather.HasDay(today))
			fetch = true;
	}
	if (fetch)
		StartFetch();
	InvalidateBand();
	ArmTimer();
}

void CDeskBand::StartFetch()
{
	if (shuttingDown || !m_hwnd)
		return;
	if (inFlight) {
		followUp = true;
		return;
	}
	inFlight = true;
	try {
		_weather.BeginFetch(m_hwnd, this, generation, config.timezoneOffsetHours);
	}
	catch (...) {
		inFlight = false;
		lastFetchFailed = true;
	}
}

void CDeskBand::OnWeather(FetchResult* result)
{
	std::unique_ptr<FetchResult> holder(result);
	if (!result || result->owner != this || shuttingDown)
		return;
	inFlight = false;
	// 设置改变后，旧请求只负责结束排队状态，不能冒充新地点的请求结果。
	if (result->generation != generation) {
		followUp = false;
		StartFetch();
		return;
	}
	lastFetchFailed = !result->ok;
	if (result->ok) {
		_weather.cache = result->cache;
		lastSuccessfulFetch = time(nullptr);
		failedUrl.clear();
	}
	else if (failedUrl != config.url) {
		failedUrl = config.url;
		ShowNotice(m_hwnd, config.language,
			Text(config.language, L"未能获取天气", L"Weather is unavailable", L"天気を取得できませんでした"),
			Text(config.language,
				L"请检查网络及设置中的 Open-Meteo 地址。\n\n已有温度会暂时保留。悬停天气条可查看最近成功获取时间。",
				L"Check your connection and the Open-Meteo URL in settings.\n\nExisting temperatures are kept. Hover over the bar to see the last successful fetch time.",
				L"接続と設定内の Open-Meteo URL を確認してください。\n\n以前の気温は保持します。バーにマウスを重ねると、最後に取得できた日時を確認できます。"));
	}
	bool again = followUp;
	followUp = false;
	InvalidateBand();
	RequestResize();
	if (again)
		StartFetch();
}

std::wstring CDeskBand::BuildTooltip() const {
	auto T = [&](const wchar_t* zh, const wchar_t* en, const wchar_t* ja) { return Text(config.language, zh, en, ja); };
	time_t now = time(nullptr);
	const DayForecast* day = _weather.FindDay(DateKeyFromUtc(now, config.timezoneOffsetHours));
	tm local{};
	BreakLocalTime(now, config.timezoneOffsetHours, local);
	std::wstring temperature = L"--";
	if (day && local.tm_hour >= 0 && local.tm_hour < 24 && day->hours[local.tm_hour].valid)
		temperature = day->hours[local.tm_hour].temperature;
	std::wostringstream text;
	text << T(L"当前小时预报：", L"Current-hour forecast: ", L"現在の時間帯の予報：") << temperature;
	if (day)
		text << L"\n" << T(L"今日最高 / 最低：", L"Today's high / low: ", L"今日の最高・最低：")
			<< (day->maxTemperature.empty() ? L"--" : day->maxTemperature) << L" / "
			<< (day->minTemperature.empty() ? L"--" : day->minTemperature);
	if (lastSuccessfulFetch && BreakLocalTime(lastSuccessfulFetch, config.timezoneOffsetHours, local)) {
		wchar_t stamp[80]{};
		swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d (UTC%+d)", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, config.timezoneOffsetHours);
		text << L"\n" << T(L"最近成功获取：", L"Last successful fetch: ", L"最終取得：") << stamp;
	}
	text << L"\n";
	if (inFlight) text << T(L"正在更新…", L"Updating…", L"更新中…");
	else if (!_weather.cache.hasData) text << T(L"尚未获取到天气。", L"No weather data yet.", L"天気データはまだありません。");
	else if (!day || now - lastSuccessfulFetch > 8 * 3600) text << T(L"预报可能已过期，请刷新。", L"The forecast may be stale. Refresh to update.", L"予報が古くなっている可能性があります。更新してください。");
	else if (lastFetchFailed) text << T(L"更新失败，正在使用上次预报。", L"Update failed. Using the previous forecast.", L"更新に失敗しました。前回の予報を表示しています。");
	else text << T(L"状态：正常", L"Status: up to date", L"状態：正常");
	return text.str();
}

void CDeskBand::HookParent(HWND parent)
{
	if (!parent || parent == parentHwnd)
		return;
	UnhookParent();
	if (!SetWindowSubclass(parent, ParentSubclass, kParentSubclassId, reinterpret_cast<DWORD_PTR>(this)))
		return;
	parentHwnd = parent;
}

void CDeskBand::UnhookParent()
{
	if (!parentHwnd)
		return;
	RemoveWindowSubclass(parentHwnd, ParentSubclass, kParentSubclassId);
	parentHwnd = nullptr;
}

POINT CDeskBand::ClickToClient(HWND hwnd, POINT point) const
{
	RECT client{};
	if (!hwnd || !GetClientRect(hwnd, &client))
		return POINT{ -1, -1 };
	if (PtInRect(&client, point))
		return point;
	POINT asScreen = point;
	ScreenToClient(hwnd, &asScreen);
	if (PtInRect(&client, asScreen))
		return asScreen;
	HWND parent = GetParent(hwnd);
	if (parent) {
		POINT mapped = point;
		MapWindowPoints(parent, hwnd, &mapped, 1);
		if (PtInRect(&client, mapped))
			return mapped;
	}
	return POINT{ -1, -1 };
}

LRESULT CALLBACK CDeskBand::ParentSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
{
	auto* self = reinterpret_cast<CDeskBand*>(data);
	if (!self || self->shuttingDown || !self->m_hwnd)
		return DefSubclassProc(hwnd, msg, wp, lp);
	bool rightClick = msg == WM_RBUTTONUP || (msg == WM_CONTEXTMENU && lp != (LPARAM)-1);
	if (rightClick) {
		POINT screen{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		if (msg == WM_RBUTTONUP)
			ClientToScreen(hwnd, &screen);
		RECT band{};
		if (GetWindowRect(self->m_hwnd, &band) &&
			screen.x >= band.left && screen.x < band.right &&
			screen.y >= band.top && screen.y < band.bottom) {
			if (!self->menuOpen) {
				POINT client = screen;
				ScreenToClient(self->m_hwnd, &client);
				self->ShowContextMenu(self->m_hwnd, client);
			}
			return 0;
		}
	}
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, ParentSubclass, kParentSubclassId);
	return DefSubclassProc(hwnd, msg, wp, lp);
}

void CDeskBand::ShowContextMenu(HWND hwnd, POINT clientPoint)
{
	if (menuOpen)
		return;
	DWORD now = GetTickCount();
	if (lastMenuTick != 0 && now - lastMenuTick < 500)
		return;
	POINT point = ClickToClient(hwnd, clientPoint);
	RECT client{};
	GetClientRect(hwnd, &client);
	if (!PtInRect(&client, point))
		return;

	HMENU menu = CreatePopupMenu();
	if (!menu)
		return;
	AppendMenuW(menu, MF_STRING, CMD_SETTINGS, Text(config.language, L"设置", L"Settings", L"設定"));
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, CMD_UNINSTALL, Text(config.language, L"卸载", L"Uninstall", L"アンインストール"));
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, CMD_REFRESH, Text(config.language, L"立即刷新气温", L"Refresh weather", L"天気を更新"));
	POINT screen = point;
	ClientToScreen(hwnd, &screen);
	SetForegroundWindow(hwnd);
	lastMenuTick = GetTickCount();
	menuOpen = true;
	int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd, nullptr);
	menuOpen = false;
	PostMessage(hwnd, WM_NULL, 0, 0);
	DestroyMenu(menu);
	if (command == CMD_SETTINGS) {
		if (settingsHwnd && IsWindow(settingsHwnd))
			SetForegroundWindow(settingsHwnd);
		else
			settingsHwnd = ShowBandSettings(hwnd, this, config);
	}
	else if (command == CMD_REFRESH) {
		StartFetch();
	}
	else if (command == CMD_UNINSTALL) {
		std::wstring exe = ModuleDirectory() + L"\\WeatherBar.exe";
		ShellExecuteW(hwnd, L"open", exe.c_str(), L"/uninstall", nullptr, SW_SHOWNORMAL);
	}
}

LRESULT CALLBACK CDeskBand::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CREATE) {
		auto* created = (LPCREATESTRUCT)lParam;
		auto* self = (CDeskBand*)created->lpCreateParams;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
		if (self)
			self->OnCreate(hwnd);
		return 0;
	}
	auto* self = (CDeskBand*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (!self)
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	try {
		switch (uMsg) {
		case WM_NOTIFY:
			if (reinterpret_cast<NMHDR*>(lParam)->code == TTN_GETDISPINFOW) {
				self->tooltipText = self->BuildTooltip();
				reinterpret_cast<NMTTDISPINFOW*>(lParam)->lpszText = self->tooltipText.data();
				return 0;
			}
			break;
		case WM_PAINT:
			self->Paint(hwnd);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_RBUTTONUP:
			self->ShowContextMenu(hwnd, POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
			return 0;
		case WM_CONTEXTMENU: {
			POINT point{};
			if (lParam == (LPARAM)-1) {
				RECT client{};
				GetClientRect(hwnd, &client);
				point.x = (client.left + client.right) / 2;
				point.y = (client.top + client.bottom) / 2;
			}
			else {
				point.x = GET_X_LPARAM(lParam);
				point.y = GET_Y_LPARAM(lParam);
				ScreenToClient(hwnd, &point);
			}
			self->ShowContextMenu(hwnd, point);
			return 0;
		}
		case WM_TIMER:
			if (wParam == TIMER_WAKE)
				self->OnTimer();
			else if (wParam == TIMER_DISPLAY) {
				KillTimer(hwnd, TIMER_DISPLAY);
				self->metricsReady = false;
				self->builtCurrentPx = 0;
				self->cachedMonitor = nullptr;
				self->CaptureEnvironment();
				self->RequestResize();
				self->InvalidateBand();
			}
			return 0;
		case WM_WEATHER_READY:
			self->OnWeather(reinterpret_cast<FetchResult*>(lParam));
			return 0;
		case WM_DPICHANGED:
		case WM_DPICHANGED_AFTERPARENT: {
			int nextDpi = (int)HIWORD(wParam);
			if (nextDpi > 0)
				self->dpi = nextDpi;
			self->metricsReady = false;
			self->builtCurrentPx = 0;
			self->CaptureEnvironment();
			self->RequestResize();
			self->InvalidateBand();
			return 0;
		}
		case WM_DISPLAYCHANGE:
			self->metricsReady = false;
			self->builtCurrentPx = 0;
			self->cachedMonitor = nullptr;
			self->CaptureEnvironment();
			self->RequestResize();
			self->InvalidateBand();
			SetTimer(hwnd, TIMER_DISPLAY, 400, nullptr);
			return 0;
		case WM_SETTINGCHANGE:
			if (self->CaptureEnvironment()) {
				self->RequestResize();
				self->InvalidateBand();
			}
			return 0;
		case WM_SIZE:
			self->InvalidateBand();
			return 0;
		default:
			break;
		}
	}
	catch (...) {
		return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
