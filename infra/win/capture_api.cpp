#include "capture_api.h"

#include <sstream>

#include <opencv2/imgproc.hpp>

namespace {

void assignError(std::string* error, const std::string& message) {
	if (error != nullptr) {
		*error = message;
	}
}

std::string makeWin32ErrorMessage(const char* phase, DWORD err) {
	std::ostringstream oss;
	oss << phase << " failed, err=" << err;
	return oss.str();
}

}  // namespace

namespace infra {
namespace win {
namespace capture {

bool captureDesktopRectBgr(const RECT& screenRect, cv::Mat* outBgr, std::string* error) {
	if (outBgr == nullptr) {
		assignError(error, "capture output pointer is null");
		return false;
	}
	outBgr->release();

	const int width = screenRect.right - screenRect.left;
	const int height = screenRect.bottom - screenRect.top;
	if (width <= 1 || height <= 1 || width > 9999 || height > 9999) {
		std::ostringstream oss;
		oss << "invalid capture rect: left=" << screenRect.left
			<< ", top=" << screenRect.top
			<< ", right=" << screenRect.right
			<< ", bottom=" << screenRect.bottom;
		assignError(error, oss.str());
		return false;
	}

	HDC screenDc = GetDC(nullptr);
	if (screenDc == nullptr) {
		assignError(error, makeWin32ErrorMessage("GetDC", GetLastError()));
		return false;
	}

	HDC memDc = CreateCompatibleDC(screenDc);
	if (memDc == nullptr) {
		assignError(error, makeWin32ErrorMessage("CreateCompatibleDC", GetLastError()));
		ReleaseDC(nullptr, screenDc);
		return false;
	}

	HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
	if (bitmap == nullptr) {
		assignError(error, makeWin32ErrorMessage("CreateCompatibleBitmap", GetLastError()));
		DeleteDC(memDc);
		ReleaseDC(nullptr, screenDc);
		return false;
	}

	HGDIOBJ oldObj = SelectObject(memDc, bitmap);
	if (oldObj == nullptr || oldObj == HGDI_ERROR) {
		assignError(error, makeWin32ErrorMessage("SelectObject", GetLastError()));
		DeleteObject(bitmap);
		DeleteDC(memDc);
		ReleaseDC(nullptr, screenDc);
		return false;
	}

	if (BitBlt(
		memDc,
		0,
		0,
		width,
		height,
		screenDc,
		screenRect.left,
		screenRect.top,
		SRCCOPY | CAPTUREBLT) == FALSE) {
		assignError(error, makeWin32ErrorMessage("BitBlt", GetLastError()));
		SelectObject(memDc, oldObj);
		DeleteObject(bitmap);
		DeleteDC(memDc);
		ReleaseDC(nullptr, screenDc);
		return false;
	}

	BITMAPINFOHEADER bi{};
	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = width;
	bi.biHeight = -height;  // top-down
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;

	cv::Mat bgra(height, width, CV_8UC4);
	const int dibRc = GetDIBits(
		memDc,
		bitmap,
		0,
		static_cast<UINT>(height),
		bgra.data,
		reinterpret_cast<BITMAPINFO*>(&bi),
		DIB_RGB_COLORS);

	SelectObject(memDc, oldObj);
	DeleteObject(bitmap);
	DeleteDC(memDc);
	ReleaseDC(nullptr, screenDc);

	if (dibRc == 0) {
		assignError(error, makeWin32ErrorMessage("GetDIBits", GetLastError()));
		return false;
	}

	cv::cvtColor(bgra, *outBgr, cv::COLOR_BGRA2BGR);
	if (outBgr->empty()) {
		assignError(error, "capture produced empty image");
		return false;
	}

	return true;
}

}  // namespace capture
}  // namespace win
}  // namespace infra
