#include "window_api.h"

#include <iostream>

namespace {

std::wstring toWStringSimple(const std::string& s) {
	return std::wstring(s.begin(), s.end());
}

struct FindWindowCtx {
	std::wstring className;
	std::wstring titleContains;
	HWND hwnd = nullptr;
};

BOOL CALLBACK enumWindowsFindProc(HWND hwnd, LPARAM lParam) {
	auto* ctx = reinterpret_cast<FindWindowCtx*>(lParam);
	if (!IsWindowVisible(hwnd)) {
		return TRUE;
	}
	if (!ctx->className.empty()) {
		wchar_t cls[256]{};
		GetClassNameW(hwnd, cls, 256);
		if (ctx->className != cls) {
			return TRUE;
		}
	}
	if (!ctx->titleContains.empty()) {
		wchar_t title[512]{};
		GetWindowTextW(hwnd, title, 512);
		const std::wstring t = title;
		if (t.find(ctx->titleContains) == std::wstring::npos) {
			return TRUE;
		}
	}
	ctx->hwnd = hwnd;
	return FALSE;
}

}  // namespace

namespace infra {
namespace win {

HWND findWindowByClassAndTitleContains(const std::string& windowClass, const std::string& titleContains) {
	FindWindowCtx ctx{};
	ctx.className = toWStringSimple(windowClass);
	ctx.titleContains = toWStringSimple(titleContains);
	EnumWindows(enumWindowsFindProc, reinterpret_cast<LPARAM>(&ctx));
	return ctx.hwnd;
}

void activateGameWindow(HWND hwnd, const RECT& windowRect, bool forceCursorCenter, bool debugLog) {
	if (!hwnd) {
		return;
	}
	ShowWindow(hwnd, SW_RESTORE);
	SetForegroundWindow(hwnd);
	BringWindowToTop(hwnd);

	if (debugLog) {
		const HWND fg = GetForegroundWindow();
		if (fg != hwnd) {
			static unsigned long long lastWarnMs = 0;
			const unsigned long long now = GetTickCount64();
			if (now - lastWarnMs > 2000) {
				std::cout << "[vrchat_fish] warn: foreground hwnd mismatch (fg=" << fg
					<< " vrchat=" << hwnd << ")" << std::endl;
				lastWarnMs = now;
			}
		}
	}

	if (windowRect.right <= windowRect.left || windowRect.bottom <= windowRect.top) {
		return;
	}

	POINT p{};
	if (!GetCursorPos(&p)) {
		return;
	}
	if (forceCursorCenter || p.x < windowRect.left || p.x >= windowRect.right ||
		p.y < windowRect.top || p.y >= windowRect.bottom) {
		SetCursorPos((windowRect.left + windowRect.right) / 2, (windowRect.top + windowRect.bottom) / 2);
	}
}

}  // namespace win
}  // namespace infra
