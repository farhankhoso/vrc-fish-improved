#include "runtime_context.h"

#include <iostream>
#include <sstream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "../infra/fs/path_utils.h"
#include "../infra/win/capture_api.h"
#include "../infra/win/input_api.h"
#include "../infra/win/window_api.h"

namespace {

bool sameRect(const RECT& a, const RECT& b) {
	return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

}  // namespace

namespace runtime {

RuntimeContext::RuntimeContext() = default;

RuntimeContext::~RuntimeContext() {
	shutdown();
}

bool RuntimeContext::initialize(const std::string& configPath) {
	shutdown();

	std::string configError;
	if (!loadAppConfig(configPath, &config_, &configError)) {
		std::cout << "[runtime] initialize failed at config: " << configError << std::endl;
		return false;
	}
	paused_.store(false);

	SetConsoleTitle(L"VRChat FISH! Auto Fish (Draft)");
	std::cout << "    VRChat FISH! Auto Fish (Draft)" << std::endl << std::endl;
	std::cout << "  Note: for educational use only. Follow related rules." << std::endl << std::endl;
	std::cout << "  Hint: this mode uses left mouse hold/release control. Recommended window size: "
		<< config_.target_width << "*" << config_.target_height << std::endl << std::endl;

	std::string logWarning;
	if (!openVrLogFile(&logWarning) && !logWarning.empty()) {
		std::cout << logWarning << std::endl;
	}

	hwnd_ = infra::win::findWindowByClassAndTitleContains(config_.window_class, config_.window_title_contains);
	if (!hwnd_) {
		const std::string line = "VRChat window not found: class=" + config_.window_class
			+ ", title_contains=" + config_.window_title_contains;
		std::cout << line << std::endl;
		if (hasVrLogFile()) {
			logVrLine("[runtime] initialize failed at window lookup: " + line, false);
		}
		return false;
	}

	HDC hdc = GetDC(hwnd_);
	if (hdc != nullptr) {
		const int dpi = GetDpiForWindow(hwnd_);
		ScaleViewportExtEx(hdc, dpi, dpi, dpi / 96, dpi / 96, nullptr);
		ScaleWindowExtEx(hdc, dpi, dpi, dpi / 96, dpi / 96, nullptr);
		ReleaseDC(hwnd_, hdc);
	}

	if (!refreshWindowRect(true)) {
		if (hasVrLogFile()) {
			logVrLine("[runtime] initialize failed at refreshWindowRect", false);
		}
		return false;
	}

	moveConsoleNearGameWindow();

	std::string templateError;
	if (!engine::loadTemplateStore(config_, &templates_, &templateError)) {
		const std::string line = "[runtime] initialize failed at template_store: " + templateError;
		std::cout << line << std::endl;
		if (hasVrLogFile()) {
			logVrLine(line, false);
		}
		return false;
	}

	stopRectThread_.store(false);
	rectThread_ = std::thread(&RuntimeContext::refreshWindowRectLoop, this);
	return true;
}

void RuntimeContext::shutdown() {
	stopRectThread_.store(true);
	if (rectThread_.joinable()) {
		rectThread_.join();
	}
	endMlRecordSession();
	{
		std::lock_guard<std::mutex> lock(ioMutex_);
		vrLogger_.flush();
		vrLogger_.close();
	}
}

AppConfig& RuntimeContext::config() {
	return config_;
}

const AppConfig& RuntimeContext::config() const {
	return config_;
}

const engine::TemplateStore& RuntimeContext::templates() const {
	return templates_;
}

HWND RuntimeContext::hwnd() const {
	return hwnd_;
}

RECT RuntimeContext::windowRect() const {
	std::lock_guard<std::mutex> lock(rectMutex_);
	return rect_;
}

bool RuntimeContext::captureBgr(cv::Mat& outBgr, std::string* error) const {
	return infra::win::capture::captureDesktopRectBgr(windowRect(), &outBgr, error);
}

void RuntimeContext::mouseLeftDown() const {
	infra::win::mouseLeftDown(hwnd_, windowRect(), config_.vr_debug);
}

void RuntimeContext::mouseLeftUp() const {
	infra::win::mouseLeftUp(hwnd_, windowRect(), config_.vr_debug);
}

void RuntimeContext::mouseLeftClickCentered(int delayMs) const {
	infra::win::mouseLeftClickCentered(hwnd_, windowRect(), config_.vr_debug, delayMs);
}

void RuntimeContext::mouseMoveRelative(int dx, int dy, const char* phaseTag) const {
	infra::win::mouseMoveRelative(hwnd_, windowRect(), dx, dy, config_.vr_debug, phaseTag);
}

void RuntimeContext::keyTapVk(WORD vk, int delayMs) const {
	infra::win::keyTapVk(hwnd_, windowRect(), vk, config_.vr_debug, delayMs);
}

bool RuntimeContext::hasVrLogFile() const {
	std::lock_guard<std::mutex> lock(ioMutex_);
	return vrLogger_.hasFile();
}

void RuntimeContext::logVrLine(const std::string& line, bool alsoStdout) const {
	std::lock_guard<std::mutex> lock(ioMutex_);
	vrLogger_.log(line, alsoStdout);
}

void RuntimeContext::logConsoleLine(const std::string& line) const {
	std::lock_guard<std::mutex> lock(ioMutex_);
	std::cout << line << std::endl;
}

bool RuntimeContext::beginMlRecordSession(std::string* error) {
	std::lock_guard<std::mutex> lock(ioMutex_);
	if (mlRecordFile_.is_open()) {
		return true;
	}
	if (config_.ml_record_csv.empty()) {
		if (error != nullptr) {
			*error = "[ML] record csv path is empty";
		}
		return false;
	}

	const std::string dir = infra::fs::dirNameOf(config_.ml_record_csv);
	if (!dir.empty() && !infra::fs::ensureDirExists(dir)) {
		if (error != nullptr) {
			*error = "[ML] failed to create record csv dir: " + dir;
		}
		return false;
	}

	mlRecordFile_.open(config_.ml_record_csv, std::ios::out | std::ios::app);
	if (!mlRecordFile_.is_open()) {
		if (error != nullptr) {
			*error = "[ML] failed to open record csv: " + config_.ml_record_csv;
		}
		return false;
	}

	mlRecordFile_.seekp(0, std::ios::end);
	if (mlRecordFile_.tellp() == 0) {
		mlRecordFile_ << "frame,timestamp_ms,fishY,sliderY,dy,sliderVel,fishVel,sliderY_norm,mousePressed,duty_label"
			<< '\n';
	}
	return true;
}

bool RuntimeContext::isMlRecordSessionOpen() const {
	std::lock_guard<std::mutex> lock(ioMutex_);
	return mlRecordFile_.is_open();
}

void RuntimeContext::appendMlRecordLine(const std::string& line) {
	std::lock_guard<std::mutex> lock(ioMutex_);
	if (mlRecordFile_.is_open()) {
		mlRecordFile_ << line << '\n';
	}
}

void RuntimeContext::flushMlRecordSession() {
	std::lock_guard<std::mutex> lock(ioMutex_);
	if (mlRecordFile_.is_open()) {
		mlRecordFile_.flush();
	}
}

void RuntimeContext::endMlRecordSession() {
	std::lock_guard<std::mutex> lock(ioMutex_);
	if (mlRecordFile_.is_open()) {
		mlRecordFile_.flush();
		mlRecordFile_.close();
	}
}

std::string RuntimeContext::makeDebugImagePath(const std::string& tag) const {
	const std::string dir = config_.vr_debug_dir.empty() ? "debug_vrchat" : config_.vr_debug_dir;
	if (!infra::fs::ensureDirExists(dir)) {
		return "";
	}
	return infra::fs::joinPath(dir, tag + "_" + std::to_string(GetTickCount64()) + ".png");
}

bool RuntimeContext::saveDebugImage(const cv::Mat& bgr, const std::string& tag) const {
	if (!config_.vr_debug_pic || bgr.empty()) {
		return false;
	}
	const std::string path = makeDebugImagePath(tag);
	if (path.empty()) {
		return false;
	}
	return cv::imwrite(path, bgr);
}

bool RuntimeContext::isPaused() const {
	return paused_.load();
}

bool RuntimeContext::togglePause() {
	const bool next = !paused_.load();
	paused_.store(next);
	return next;
}

void RuntimeContext::setPaused(bool paused) {
	paused_.store(paused);
}

void RuntimeContext::waitWhilePaused(int sleepMs) const {
	if (sleepMs < 1) {
		sleepMs = 1;
	}
	while (isPaused() && !stopRectThread_.load()) {
		Sleep(static_cast<DWORD>(sleepMs));
	}
}

bool RuntimeContext::refreshWindowRect(bool printHint) {
	if (!hwnd_) {
		return false;
	}

	bool gotRect = false;
	for (int attempt = 0; attempt < 4; ++attempt) {
		RECT clientRect{};
		GetClientRect(hwnd_, &clientRect);
		const int w = clientRect.right - clientRect.left;
		const int h = clientRect.bottom - clientRect.top;
		if (w <= 1 || h <= 1 || w > 9999 || h > 9999) {
			std::ostringstream oss;
			oss << "[runtime] invalid VRChat client area: w=" << w << ", h=" << h
				<< ", attempt=" << (attempt + 1);
			logConsoleLine(oss.str());
			if (hasVrLogFile()) {
				logVrLine(oss.str(), false);
			}
			return false;
		}

		POINT p1{ clientRect.left, clientRect.top };
		POINT p2{ clientRect.right, clientRect.bottom };
		ClientToScreen(hwnd_, &p1);
		ClientToScreen(hwnd_, &p2);

		RECT screenRect{};
		screenRect.left = p1.x;
		screenRect.top = p1.y;
		screenRect.right = p2.x;
		screenRect.bottom = p2.y;

		RECT previousRect{};
		{
			std::lock_guard<std::mutex> lock(rectMutex_);
			previousRect = rect_;
			rect_ = screenRect;
		}
		gotRect = true;

		const bool changed = !sameRect(screenRect, previousRect);
		if (changed && printHint) {
			logConsoleLine("Hint: detected window size " + std::to_string(w) + "*" + std::to_string(h));
		}

		const bool forceResolution = (config_.force_resolution != 0);
		const bool needResize = forceResolution && (w != config_.target_width || h != config_.target_height);
		if (needResize) {
			if (printHint) {
				logConsoleLine("Hint: auto resizing to "
					+ std::to_string(config_.target_width) + "*" + std::to_string(config_.target_height));
			}

			RECT gameWindowRect{};
			GetWindowRect(hwnd_, &gameWindowRect);
			const int dx = (gameWindowRect.right - gameWindowRect.left) - (screenRect.right - screenRect.left);
			const int dy = (gameWindowRect.bottom - gameWindowRect.top) - (screenRect.bottom - screenRect.top);
			const int newWidth = config_.target_width + dx;
			const int newHeight = config_.target_height + dy;
			const int newLeft = (screenRect.left + screenRect.right) / 2 - newWidth / 2;
			const int newTop = (screenRect.top + screenRect.bottom) / 2 - newHeight / 2;
			MoveWindow(hwnd_, newLeft, newTop, newWidth, newHeight, TRUE);
			Sleep(80);
			continue;
		}

		return true;
	}

	return gotRect;
}

void RuntimeContext::refreshWindowRectLoop() {
	Sleep(2000);
	while (!stopRectThread_.load()) {
		waitWhilePaused();
		if (stopRectThread_.load()) {
			break;
		}

		if (!refreshWindowRect(true)) {
			Sleep(500);
			continue;
		}

		for (int i = 0; i < 10 && !stopRectThread_.load(); ++i) {
			Sleep(100);
		}
	}
}

void RuntimeContext::moveConsoleNearGameWindow() const {
	const RECT r = windowRect();
	if (r.right <= r.left || r.bottom <= r.top) {
		return;
	}
	MoveWindow(
		GetConsoleWindow(),
		r.right,
		r.top,
		(r.right - r.left) / 2,
		(r.bottom - r.top) / 2,
		TRUE);
}

bool RuntimeContext::openVrLogFile(std::string* warning) {
	if (warning != nullptr) {
		warning->clear();
	}
	if (config_.vr_log_file.empty()) {
		return true;
	}

	const std::string dir = infra::fs::dirNameOf(config_.vr_log_file);
	if (!dir.empty() && !infra::fs::ensureDirExists(dir)) {
		if (warning != nullptr) {
			*warning = "[runtime] WARN: failed to create vr_log_file dir: " + dir;
		}
		return false;
	}

	std::lock_guard<std::mutex> lock(ioMutex_);
	vrLogger_.close();
	if (!vrLogger_.openAppend(config_.vr_log_file)) {
		if (warning != nullptr) {
			*warning = "[runtime] WARN: failed to open vr_log_file=" + config_.vr_log_file;
		}
		return false;
	}
	vrLogger_.log("[vrchat_fish] log start file=" + config_.vr_log_file, config_.vr_debug);
	return true;
}

}  // namespace runtime
