#include "pch.h"
#include "ImageList.h"
#include "ImportConfig.h"
#include <d2d1_3.h>
#include <d3d11.h>
#include <shlwapi.h>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "shlwapi.lib")

Images::~Images() {
	Clear();
}

void Images::SetFileList(std::vector<std::pair<int, std::wstring>> _files) {
	files = std::move(_files);
}

void Images::Clear() {
	if (ImageList) {
		ImageList_Destroy(ImageList);
		ImageList = nullptr;
	}
	codetoindex.clear();
	pixelSize = 0;
}

namespace {

struct SvgDevice {
	ID3D11Device* d3d = nullptr;
	ID3D11DeviceContext* d3dContext = nullptr;
	ID2D1Factory1* factory = nullptr;
	ID2D1Device* device = nullptr;
	ID2D1DeviceContext5* context = nullptr;

	~SvgDevice() {
		if (context) context->Release();
		if (device) device->Release();
		if (factory) factory->Release();
		if (d3dContext) d3dContext->Release();
		if (d3d) d3d->Release();
	}

	bool Open() {
		const D3D_DRIVER_TYPE drivers[] = { D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_HARDWARE };
		for (D3D_DRIVER_TYPE driver : drivers) {
			if (d3d) {
				d3d->Release();
				d3d = nullptr;
			}
			if (d3dContext) {
				d3dContext->Release();
				d3dContext = nullptr;
			}
			D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_9_1;
			HRESULT created = D3D11CreateDevice(
				nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
				nullptr, 0, D3D11_SDK_VERSION, &d3d, &level, &d3dContext);
			if (FAILED(created))
				continue;
			IDXGIDevice* dxgi = nullptr;
			if (FAILED(d3d->QueryInterface(IID_PPV_ARGS(&dxgi))))
				continue;
			if (!factory) {
				D2D1_FACTORY_OPTIONS options{};
				if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory)))) {
					dxgi->Release();
					continue;
				}
			}
			if (FAILED(factory->CreateDevice(dxgi, &device))) {
				dxgi->Release();
				continue;
			}
			dxgi->Release();
			ID2D1DeviceContext* base = nullptr;
			if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &base)))
				continue;
			HRESULT svg = base->QueryInterface(IID_PPV_ARGS(&context));
			base->Release();
			if (SUCCEEDED(svg)) {
				context->SetDpi(96.0f, 96.0f);
				return true;
			}
		}
		return false;
	}
};

HICON IconFromPixels(int size, const BYTE* source, UINT pitch) {
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = size;
	info.bmiHeader.biHeight = -size;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
	if (!color || !mask || !bits) {
		if (color) DeleteObject(color);
		if (mask) DeleteObject(mask);
		return nullptr;
	}
	for (int y = 0; y < size; ++y)
		memcpy(static_cast<BYTE*>(bits) + y * size * 4, source + y * pitch, size * 4);
	ICONINFO iconInfo{};
	iconInfo.fIcon = TRUE;
	iconInfo.hbmColor = color;
	iconInfo.hbmMask = mask;
	HICON icon = CreateIconIndirect(&iconInfo);
	DeleteObject(color);
	DeleteObject(mask);
	return icon;
}

HICON RasterizeSvg(SvgDevice& device, const std::wstring& path, int size) {
	IStream* stream = nullptr;
	if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream)))
		return nullptr;
	ID2D1SvgDocument* svg = nullptr;
	HRESULT created = device.context->CreateSvgDocument(stream, D2D1::SizeF((float)size, (float)size), &svg);
	stream->Release();
	if (FAILED(created) || !svg)
		return nullptr;

	ID2D1Bitmap1* target = nullptr;
	D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
	if (FAILED(device.context->CreateBitmap(D2D1::SizeU(size, size), nullptr, 0, &targetProps, &target))) {
		svg->Release();
		return nullptr;
	}
	device.context->SetTarget(target);
	device.context->BeginDraw();
	device.context->Clear(D2D1::ColorF(0, 0, 0, 0));
	device.context->DrawSvgDocument(svg);
	HRESULT drawn = device.context->EndDraw();
	device.context->SetTarget(nullptr);
	svg->Release();
	if (FAILED(drawn)) {
		target->Release();
		return nullptr;
	}

	ID2D1Bitmap1* readable = nullptr;
	D2D1_BITMAP_PROPERTIES1 readProps = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
	if (FAILED(device.context->CreateBitmap(D2D1::SizeU(size, size), nullptr, 0, &readProps, &readable))) {
		target->Release();
		return nullptr;
	}
	HRESULT copied = readable->CopyFromBitmap(nullptr, target, nullptr);
	target->Release();
	if (FAILED(copied)) {
		readable->Release();
		return nullptr;
	}
	D2D1_MAPPED_RECT mapped{};
	if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
		readable->Release();
		return nullptr;
	}
	std::vector<BYTE> pixels(static_cast<size_t>(size) * size * 4);
	for (int y = 0; y < size; ++y)
		memcpy(pixels.data() + static_cast<size_t>(y) * size * 4, mapped.bits + y * mapped.pitch, static_cast<size_t>(size) * 4);
	readable->Unmap();
	readable->Release();

	int minX = size;
	int minY = size;
	int maxX = -1;
	int maxY = -1;
	for (int y = 0; y < size; ++y) {
		const BYTE* row = pixels.data() + static_cast<size_t>(y) * size * 4;
		for (int x = 0; x < size; ++x) {
			if (row[x * 4 + 3] <= 16)
				continue;
			if (x < minX)
				minX = x;
			if (y < minY)
				minY = y;
			if (x > maxX)
				maxX = x;
			if (y > maxY)
				maxY = y;
		}
	}
	if (maxX >= minX) {
		int contentW = maxX - minX + 1;
		int contentH = maxY - minY + 1;
		int side = contentW > contentH ? contentW : contentH;
		if (side > 0 && side * 100 < size * 94) {
			int render = MulDiv(size, size, side);
			if (render > size && render <= 512) {
				readable = nullptr;
				svg = nullptr;
				stream = nullptr;
				if (SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream)) &&
					SUCCEEDED(device.context->CreateSvgDocument(stream, D2D1::SizeF((float)render, (float)render), &svg))) {
					stream->Release();
					stream = nullptr;
					target = nullptr;
					if (SUCCEEDED(device.context->CreateBitmap(D2D1::SizeU(render, render), nullptr, 0, &targetProps, &target))) {
						device.context->SetTarget(target);
						device.context->BeginDraw();
						device.context->Clear(D2D1::ColorF(0, 0, 0, 0));
						device.context->DrawSvgDocument(svg);
						drawn = device.context->EndDraw();
						device.context->SetTarget(nullptr);
						if (SUCCEEDED(drawn) &&
							SUCCEEDED(device.context->CreateBitmap(D2D1::SizeU(render, render), nullptr, 0, &readProps, &readable)) &&
							SUCCEEDED(readable->CopyFromBitmap(nullptr, target, nullptr)) &&
							SUCCEEDED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
							std::vector<BYTE> big(static_cast<size_t>(render) * render * 4);
							for (int y = 0; y < render; ++y)
								memcpy(big.data() + static_cast<size_t>(y) * render * 4, mapped.bits + y * mapped.pitch, static_cast<size_t>(render) * 4);
							readable->Unmap();
							int bMinX = render, bMinY = render, bMaxX = -1, bMaxY = -1;
							for (int y = 0; y < render; ++y) {
								const BYTE* row = big.data() + static_cast<size_t>(y) * render * 4;
								for (int x = 0; x < render; ++x) {
									if (row[x * 4 + 3] <= 16)
										continue;
									if (x < bMinX) bMinX = x;
									if (y < bMinY) bMinY = y;
									if (x > bMaxX) bMaxX = x;
									if (y > bMaxY) bMaxY = y;
								}
							}
							if (bMaxX >= bMinX) {
								int box = (bMaxX - bMinX + 1) > (bMaxY - bMinY + 1) ? (bMaxX - bMinX + 1) : (bMaxY - bMinY + 1);
								int originX = bMinX - (box - (bMaxX - bMinX + 1)) / 2;
								int originY = bMinY - (box - (bMaxY - bMinY + 1)) / 2;
								std::vector<BYTE> fitted(static_cast<size_t>(size) * size * 4, 0);
								for (int y = 0; y < size; ++y) {
									int sy = originY + (int)(((long long)y * box + box / 2) / size);
									if (sy < 0) sy = 0;
									if (sy >= render) sy = render - 1;
									for (int x = 0; x < size; ++x) {
										int sx = originX + (int)(((long long)x * box + box / 2) / size);
										if (sx < 0) sx = 0;
										if (sx >= render) sx = render - 1;
										memcpy(fitted.data() + (static_cast<size_t>(y) * size + x) * 4,
											big.data() + (static_cast<size_t>(sy) * render + sx) * 4, 4);
									}
								}
								pixels.swap(fitted);
							}
						}
						if (readable) {
							readable->Release();
							readable = nullptr;
						}
						target->Release();
						target = nullptr;
					}
				}
				if (stream) {
					stream->Release();
					stream = nullptr;
				}
				if (svg) {
					svg->Release();
					svg = nullptr;
				}
			}
		}
	}
	return IconFromPixels(size, pixels.data(), size * 4);
}

}

void Images::MakeImageList(int requestedSize) {
	Clear();
	if (requestedSize < 1)
		requestedSize = kIconSizeMin;
	pixelSize = requestedSize;
	ImageList = ImageList_Create(pixelSize, pixelSize, ILC_COLOR32, (int)files.size(), 1);
	if (!ImageList)
		return;

	SvgDevice device;
	if (!device.Open())
		return;

	std::wstring root = ModuleDirectory();
	std::map<std::wstring, int> loaded;
	for (const auto& file : files) {
		auto existing = loaded.find(file.second);
		if (existing != loaded.end()) {
			codetoindex[file.first] = existing->second;
			continue;
		}
		std::wstring filepath = root + L"\\icons\\" + file.second + L".svg";
		HICON image = RasterizeSvg(device, filepath, pixelSize);
		if (!image)
			continue;
		int added = ImageList_AddIcon(ImageList, image);
		DestroyIcon(image);
		if (added < 0)
			continue;
		codetoindex[file.first] = added;
		loaded[file.second] = added;
	}
}
