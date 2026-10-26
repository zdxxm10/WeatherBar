#pragma once
#include <ShlObj.h>
#include <string>
#include <optional>
#include "ImageList.h"
#include "ImportConfig.h"
#include "Weather.h"

class CDeskBand : public IDeskBand2, public IObjectWithSite, public IPersistStream {
public:
	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();
	STDMETHODIMP GetWindow(HWND* phwnd);
	STDMETHODIMP ContextSensitiveHelp(BOOL fEnterMode);
	STDMETHODIMP ShowDW(BOOL bShow);
	STDMETHODIMP CloseDW(DWORD dwReserved);
	STDMETHODIMP ResizeBorderDW(LPCRECT prcBorder, IUnknown* punkToolbarSite, BOOL fReserved);
	STDMETHODIMP GetBandInfo(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO* pdbi);
	STDMETHODIMP CanRenderComposited(BOOL* pfCanRenderComposited);
	STDMETHODIMP SetCompositionState(BOOL fCompositionEnabled);
	STDMETHODIMP GetCompositionState(BOOL* pfCompositionEnabled);
	STDMETHODIMP SetSite(IUnknown* pUnkSite);
	STDMETHODIMP GetSite(REFIID riid, void** ppvSite);
	STDMETHODIMP GetClassID(CLSID* pClassID);
	STDMETHODIMP IsDirty();
	STDMETHODIMP Load(IStream* pStm);
	STDMETHODIMP Save(IStream* pStm, BOOL fClearDirty);
	STDMETHODIMP GetSizeMax(ULARGE_INTEGER* pcbSize);

	CDeskBand();
	~CDeskBand();

	bool ApplySavedConfig(const BandConfig& incoming);
	void BeginSettingsPreview();
	void ApplyPreviewConfig(const BandConfig& incoming);
	void CancelSettingsPreview();
	const BandConfig& CurrentConfig() const { return config; }
	void OnSettingsClosed();

	struct Placement {
		int width = 0;
		int height = 0;
		RECT icon{};
		RECT current{};
		RECT maxTemp{};
		RECT minTemp{};
		bool showIcon = false;
		bool showMax = false;
		bool showMin = false;
		std::wstring currentText;
		std::wstring maxText;
		std::wstring minText;
		int weatherCode = -1;
	};

private:
	struct Metrics {
		int width = 0;
		int height = 0;
		int padX = 0;
		int padY = 0;
		int iconPx = 0;
		int currentPx = 0;
		int maxPx = 0;
		int minPx = 0;
		int slack = 0;
		int gapIcon = 0;
		int currentW = 0;
		int gapExt = 0;
		int extW = 0;
		int innerH = 0;
		int maxLineH = 0;
		int minLineH = 0;
		int currentH = 0;
		bool showIcon = false;
		bool showMax = false;
		bool showMin = false;
		std::wstring currentText;
		std::wstring maxText;
		std::wstring minText;
		int weatherCode = -1;
	};

	BOOL RegisterAndCreateWindow(HWND hwndParent);
	void OnCreate(HWND hwnd);
	void Shutdown();
	void EnsureFonts(int currentPx, int maxPx, int minPx);
	void EnsureVisuals(int iconPx);
	void ReleaseGraphics();
	int DpiPx(int dip) const;
	int HeightBudget() const;
	int GapPx(int cellBudget, SizeLevel level) const;
	void UpdateDpi(HWND hwnd);
	bool CaptureEnvironment();
	int TaskbarShortSide() const;
	Metrics Measure();
	Metrics MeasureBody(const Metrics& seed);
	Metrics FitForWidth(const Metrics& preferred, int available);
	Metrics MetricsForClient(const RECT& client);
	Placement LayoutIn(const RECT& bounds, const Metrics& metrics) const;
	void Paint(HWND hwnd);
	void ArmTimer();
	void OnTimer();
	void StartFetch();
	void OnWeather(FetchResult* result);
	std::wstring BuildTooltip() const;
	void ShowContextMenu(HWND hwnd, POINT clientPoint);
	void HookParent(HWND parent);
	void UnhookParent();
	POINT ClickToClient(HWND hwnd, POINT point) const;
	static LRESULT CALLBACK ParentSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data);
	void RequestResize();
	void InvalidateBand();
	int TextWidth(HDC dc, HFONT font, const std::wstring& text, int extra) const;
	int FontHeight(HDC dc, HFONT font) const;
	HFONT MakeFontPx(int px) const;
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	bool metricsReady = false;
	int cachedDpi = 0;
	int cachedBudget = 0;
	bool cachedVertical = false;
	SizeLevel cachedCurrentLevel = SizeLevel::Medium;
	SizeLevel cachedMaxLevel = SizeLevel::Medium;
	SizeLevel cachedMinLevel = SizeLevel::Medium;
	SizeLevel cachedIconLevel = SizeLevel::Medium;
	SizeLevel cachedGapIconLevel = SizeLevel::Medium;
	SizeLevel cachedGapExtremeLevel = SizeLevel::Medium;
	int reportedWidth = -1;
	int reportedHeight = -1;
	unsigned metricsGeneration = 0;
	unsigned fitGeneration = 0;
	int fitLimit = -1;
	HMONITOR cachedMonitor = nullptr;
	Metrics metricsCache;
	Metrics fitCache;

	LONG m_cRef = 1;
	HWND m_hwnd = nullptr;
	HWND parentHwnd = nullptr;
	HWND settingsHwnd = nullptr;
	HWND tooltipHwnd = nullptr;
	std::wstring tooltipText;
	time_t lastSuccessfulFetch = 0;
	bool lastFetchFailed = false;
	bool menuOpen = false;
	DWORD lastMenuTick = 0;
	int dpi = 96;
	int barThickness = 0;
	bool verticalBar = false;
	BOOL m_bCompositionEnabled = FALSE;
	IUnknown* m_pSite = nullptr;
	bool shuttingDown = false;
	bool inFlight = false;
	bool followUp = false;
	bool armedForSchedule = false;
	std::wstring failedUrl;
	unsigned long long generation = 1;
	time_t armedUntil = 0;
	int lastDateKey = -1;
	int builtIconPx = -1;
	int builtCurrentPx = 0;
	int builtMaxPx = 0;
	int builtMinPx = 0;
	BandConfig config;
	struct SettingsSnapshot {
		BandConfig config;
		ForecastCache forecast;
		time_t lastSuccessfulFetch;
		bool lastFetchFailed;
		std::wstring failedUrl;
	};
	std::optional<SettingsSnapshot> settingsSnapshot;
	Weather _weather;
	Images weathericons;
	HFONT fontCurrent = nullptr;
	HFONT fontMax = nullptr;
	HFONT fontMin = nullptr;
};
