#pragma once

#include <windows.h>

namespace infra {
namespace win {

void mouseLeftDown(HWND hwnd, const RECT& windowRect, bool debugLog);
void mouseLeftUp(HWND hwnd, const RECT& windowRect, bool debugLog);
void mouseLeftClickCentered(HWND hwnd, const RECT& windowRect, bool debugLog, int delayMs = 40);
void mouseMoveRelative(HWND hwnd, const RECT& windowRect, int dx, int dy, bool debugLog, const char* phaseTag);
void keyTapVk(HWND hwnd, const RECT& windowRect, WORD vk, bool debugLog, int delayMs = 30);

}  // namespace win
}  // namespace infra
