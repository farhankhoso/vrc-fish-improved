#include "input_api.h"

#include <iostream>

#include "window_api.h"

namespace {

void sendMouseLeftRaw(DWORD flags, bool debugLog, const char* phaseTag) {
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = flags;
	input.mi.time = 0;
	if (SendInput(1, &input, sizeof(INPUT)) != 1 && debugLog) {
		std::cout << "[vrchat_fish] SendInput " << phaseTag << " failed err=" << GetLastError() << std::endl;
	}
}

}  // namespace

namespace infra {
namespace win {

void mouseLeftDown(HWND hwnd, const RECT& windowRect, bool debugLog) {
	activateGameWindow(hwnd, windowRect, false, debugLog);
	sendMouseLeftRaw(MOUSEEVENTF_LEFTDOWN, debugLog, "leftdown");
}

void mouseLeftUp(HWND hwnd, const RECT& windowRect, bool debugLog) {
	activateGameWindow(hwnd, windowRect, false, debugLog);
	sendMouseLeftRaw(MOUSEEVENTF_LEFTUP, debugLog, "leftup");
}

void mouseLeftClickCentered(HWND hwnd, const RECT& windowRect, bool debugLog, int delayMs) {
	activateGameWindow(hwnd, windowRect, true, debugLog);
	sendMouseLeftRaw(MOUSEEVENTF_LEFTDOWN, debugLog, "leftdown");
	Sleep(delayMs);
	sendMouseLeftRaw(MOUSEEVENTF_LEFTUP, debugLog, "leftup");
}

void mouseMoveRelative(HWND hwnd, const RECT& windowRect, int dx, int dy, bool debugLog, const char* phaseTag) {
	if (dx == 0 && dy == 0) {
		return;
	}
	activateGameWindow(hwnd, windowRect, false, debugLog);
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_MOVE;
	input.mi.dx = dx;
	input.mi.dy = dy;
	input.mi.time = 0;
	if (SendInput(1, &input, sizeof(INPUT)) != 1 && debugLog) {
		std::cout << "[vrchat_fish] SendInput " << phaseTag << " failed err=" << GetLastError() << std::endl;
	}
}

void keyTapVk(HWND hwnd, const RECT& windowRect, WORD vk, bool debugLog, int delayMs) {
	activateGameWindow(hwnd, windowRect, false, debugLog);
	INPUT input{};
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = vk;
	input.ki.wScan = static_cast<WORD>(MapVirtualKey(vk, MAPVK_VK_TO_VSC));
	input.ki.dwFlags = 0;
	if (SendInput(1, &input, sizeof(INPUT)) != 1 && debugLog) {
		std::cout << "[vrchat_fish] SendInput keydown failed err=" << GetLastError() << std::endl;
	}
	Sleep(delayMs);
	input.ki.dwFlags = KEYEVENTF_KEYUP;
	if (SendInput(1, &input, sizeof(INPUT)) != 1 && debugLog) {
		std::cout << "[vrchat_fish] SendInput keyup failed err=" << GetLastError() << std::endl;
	}
}

}  // namespace win
}  // namespace infra
