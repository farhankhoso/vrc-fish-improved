#pragma once

#include <string>

#include <windows.h>

namespace infra {
namespace win {

HWND findWindowByClassAndTitleContains(const std::string& windowClass, const std::string& titleContains);
void activateGameWindow(HWND hwnd, const RECT& windowRect, bool forceCursorCenter, bool debugLog);

}  // namespace win
}  // namespace infra
