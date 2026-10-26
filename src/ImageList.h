#pragma once
#include <windows.h>
#include <commctrl.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern HINSTANCE g_hinstDll;

class Images {
public:
	Images() = default;
	~Images();
	Images(const Images&) = delete;
	Images& operator=(const Images&) = delete;

	void SetFileList(std::vector<std::pair<int, std::wstring>> filenames);
	void MakeImageList(int pixelSize);
	void Clear();
	HIMAGELIST ImageList = nullptr;
	std::map<int, int> codetoindex;

private:
	std::vector<std::pair<int, std::wstring>> files;
	int pixelSize = 0;
};
