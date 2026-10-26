#pragma once
#include <windows.h>
#include "ImportConfig.h"

class CDeskBand;

HWND ShowBandSettings(HWND owner, CDeskBand* band, const BandConfig& current);
void CloseBandSettings(HWND hwnd);
