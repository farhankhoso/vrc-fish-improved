#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>
#include <opencv2/core/mat.hpp>

#include "../config/app_config.h"
#include "../engine/template_store.h"
#include "../infra/log/logger.h"

namespace runtime {

class RuntimeContext {
public:
	RuntimeContext();
	~RuntimeContext();

	bool initialize(const std::string& configPath = "config.ini");
	void shutdown();

	AppConfig& config();
	const AppConfig& config() const;

	const engine::TemplateStore& templates() const;

	HWND hwnd() const;
	RECT windowRect() const;
	bool captureBgr(cv::Mat& outBgr, std::string* error = nullptr) const;

	void mouseLeftDown() const;
	void mouseLeftUp() const;
	void mouseLeftClickCentered(int delayMs = 40) const;
	void mouseMoveRelative(int dx, int dy, const char* phaseTag) const;
	void keyTapVk(WORD vk, int delayMs = 30) const;

	bool hasVrLogFile() const;
	void logVrLine(const std::string& line, bool alsoStdout = true) const;
	void logConsoleLine(const std::string& line) const;

	bool beginMlRecordSession(std::string* error = nullptr);
	bool isMlRecordSessionOpen() const;
	void appendMlRecordLine(const std::string& line);
	void flushMlRecordSession();
	void endMlRecordSession();

	bool saveDebugImage(const cv::Mat& bgr, const std::string& tag) const;

	bool isPaused() const;
	bool togglePause();
	void setPaused(bool paused);
	void waitWhilePaused(int sleepMs = 1000) const;

private:
	bool refreshWindowRect(bool printHint);
	void refreshWindowRectLoop();
	void moveConsoleNearGameWindow() const;
	bool openVrLogFile(std::string* warning = nullptr);
	std::string makeDebugImagePath(const std::string& tag) const;

	AppConfig config_{};
	HWND hwnd_{};
	RECT rect_{};
	engine::TemplateStore templates_{};

	std::atomic<bool> paused_{ false };
	std::atomic<bool> stopRectThread_{ false };
	std::thread rectThread_{};
	mutable std::mutex rectMutex_{};
	mutable std::mutex ioMutex_{};
	mutable infra::log::Logger vrLogger_{};
	std::ofstream mlRecordFile_{};
};

}  // namespace runtime
