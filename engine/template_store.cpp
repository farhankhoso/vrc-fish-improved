#include "template_store.h"

#include <algorithm>
#include <sstream>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "../infra/fs/path_utils.h"

namespace {

void assignError(std::string* error, const std::string& message) {
	if (error != nullptr) {
		*error = message;
	}
}

std::string toLowerAscii(std::string s) {
	for (char& c : s) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return s;
}

bool isFishAltIconFilename(const std::string& file) {
	const std::string f = toLowerAscii(file);
	const std::string prefix = "fish_icon_alt";
	const std::string suffix = ".png";
	if (f.size() < prefix.size() + suffix.size()) return false;
	if (f.rfind(prefix, 0) != 0) return false;
	if (f.compare(f.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
	const std::string mid = f.substr(prefix.size(), f.size() - prefix.size() - suffix.size());
	if (mid.empty()) return true;
	for (char c : mid) {
		if (c < '0' || c > '9') return false;
	}
	return true;
}

int parseFishAltIconIndex(const std::string& file) {
	if (!isFishAltIconFilename(file)) {
		return -1;
	}
	const std::string f = toLowerAscii(file);
	const std::string prefix = "fish_icon_alt";
	const std::string suffix = ".png";
	const std::string mid = f.substr(prefix.size(), f.size() - prefix.size() - suffix.size());
	if (mid.empty()) {
		return -1;
	}
	int value = 0;
	for (char c : mid) {
		const int digit = static_cast<int>(c - '0');
		if (value > 100000000) {
			return -1;
		}
		value = value * 10 + digit;
	}
	return value;
}

std::vector<std::string> listFilesByWildcard(const std::string& dir, const std::string& wildcard) {
	std::vector<std::string> out;
	const std::string query = infra::fs::joinPath(dir, wildcard);
	WIN32_FIND_DATAA ffd{};
	HANDLE hFind = FindFirstFileA(query.c_str(), &ffd);
	if (hFind == INVALID_HANDLE_VALUE) {
		return out;
	}
	do {
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		}
		if (ffd.cFileName[0] == '\0') {
			continue;
		}
		out.emplace_back(ffd.cFileName);
	} while (FindNextFileA(hFind, &ffd));
	FindClose(hFind);
	return out;
}

}  // namespace

namespace engine {

bool loadGrayTplFromFile(const std::string& path, GrayTpl* out, std::string* error) {
	if (out == nullptr) {
		assignError(error, "template output pointer is null");
		return false;
	}

	cv::Mat raw = cv::imread(path, cv::IMREAD_UNCHANGED);
	if (raw.empty()) {
		std::ostringstream oss;
		oss << "template load failed: " << path;
		assignError(error, oss.str());
		return false;
	}

	GrayTpl tpl{};
	if (raw.channels() == 4) {
		std::vector<cv::Mat> channels;
		cv::split(raw, channels);
		cv::Mat alpha = channels[3];
		double minA = 0.0;
		double maxA = 0.0;
		cv::minMaxLoc(alpha, &minA, &maxA);

		cv::Mat bgr;
		cv::cvtColor(raw, bgr, cv::COLOR_BGRA2BGR);
		cv::cvtColor(bgr, tpl.gray, cv::COLOR_BGR2GRAY);
		if (minA < 255.0) {
			cv::threshold(alpha, tpl.mask, 0, 255, cv::THRESH_BINARY);
		}
	} else {
		if (raw.channels() == 1) {
			tpl.gray = raw;
		} else {
			cv::cvtColor(raw, tpl.gray, cv::COLOR_BGR2GRAY);
		}
	}

	*out = std::move(tpl);
	return true;
}

GrayTpl tryLoadGrayTplFromFile(const std::string& path) {
	cv::Mat raw = cv::imread(path, cv::IMREAD_UNCHANGED);
	if (raw.empty()) {
		return GrayTpl{};
	}

	GrayTpl tpl{};
	if (raw.channels() == 4) {
		std::vector<cv::Mat> channels;
		cv::split(raw, channels);
		cv::Mat alpha = channels[3];
		double minA = 0.0;
		double maxA = 0.0;
		cv::minMaxLoc(alpha, &minA, &maxA);

		cv::Mat bgr;
		cv::cvtColor(raw, bgr, cv::COLOR_BGRA2BGR);
		cv::cvtColor(bgr, tpl.gray, cv::COLOR_BGR2GRAY);
		if (minA < 255.0) {
			cv::threshold(alpha, tpl.mask, 0, 255, cv::THRESH_BINARY);
		}
	} else {
		if (raw.channels() == 1) {
			tpl.gray = raw;
		} else {
			cv::cvtColor(raw, tpl.gray, cv::COLOR_BGR2GRAY);
		}
	}

	return tpl;
}

std::vector<std::string> listFishAltIconFiles(const std::string& dir) {
	std::vector<std::string> files = listFilesByWildcard(dir, "fish_icon_alt*.png");
	std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
		const bool aOk = isFishAltIconFilename(a);
		const bool bOk = isFishAltIconFilename(b);
		if (aOk != bOk) return aOk;
		if (!aOk) return toLowerAscii(a) < toLowerAscii(b);

		const int ai = parseFishAltIconIndex(a);
		const int bi = parseFishAltIconIndex(b);
		if (ai != bi) {
			if (ai < 0) return true;
			if (bi < 0) return false;
			return ai < bi;
		}
		return toLowerAscii(a) < toLowerAscii(b);
	});
	files.erase(std::unique(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
		return toLowerAscii(a) == toLowerAscii(b);
	}), files.end());
	return files;
}

bool loadTemplateStore(const AppConfig& config, TemplateStore* out, std::string* error) {
	if (out == nullptr) {
		assignError(error, "template store output pointer is null");
		return false;
	}

	TemplateStore store{};
	if (!loadGrayTplFromFile(
		infra::fs::joinPath(config.resource_dir, config.tpl_bite_exclamation_bottom),
		&store.biteExclBottom,
		error)) {
		return false;
	}
	if (!loadGrayTplFromFile(
		infra::fs::joinPath(config.resource_dir, config.tpl_bite_exclamation_full),
		&store.biteExclFull,
		error)) {
		return false;
	}
	if (!loadGrayTplFromFile(
		infra::fs::joinPath(config.resource_dir, config.tpl_minigame_bar_full),
		&store.minigameBarFull,
		error)) {
		return false;
	}
	if (!loadGrayTplFromFile(
		infra::fs::joinPath(config.resource_dir, config.tpl_player_slider),
		&store.playerSlider,
		error)) {
		return false;
	}

	store.fishIcons.clear();
	store.fishIconFiles.clear();
	std::vector<std::string> seenFishFilesLower;
	bool requiredFishLoadFailed = false;

	auto addFishTplFile = [&](const std::string& file, bool required, GrayTpl* legacyOut) {
		if (file.empty()) {
			return;
		}
		const std::string key = toLowerAscii(file);
		if (std::find(seenFishFilesLower.begin(), seenFishFilesLower.end(), key) != seenFishFilesLower.end()) {
			return;
		}
		GrayTpl tpl{};
		if (required) {
			std::string localError;
			if (!loadGrayTplFromFile(
				infra::fs::joinPath(config.resource_dir, file),
				&tpl,
				&localError)) {
				assignError(error, localError);
				requiredFishLoadFailed = true;
				return;
			}
		} else {
			tpl = tryLoadGrayTplFromFile(infra::fs::joinPath(config.resource_dir, file));
		}
		if (tpl.empty()) {
			return;
		}
		seenFishFilesLower.push_back(key);
		if (legacyOut != nullptr) {
			*legacyOut = tpl;
		}
		store.fishIcons.push_back(tpl);
		store.fishIconFiles.push_back(file);
	};

	addFishTplFile(config.tpl_fish_icon, true, &store.fishIcon);
	if (requiredFishLoadFailed) {
		return false;
	}
	addFishTplFile(config.tpl_fish_icon_alt, false, &store.fishIconAlt);
	addFishTplFile(config.tpl_fish_icon_alt2, false, &store.fishIconAlt2);
	for (const std::string& file : listFishAltIconFiles(config.resource_dir)) {
		if (!isFishAltIconFilename(file)) {
			continue;
		}
		addFishTplFile(file, false, nullptr);
	}
	if (requiredFishLoadFailed) {
		return false;
	}

	if (store.fishIcons.empty()) {
		assignError(error, "no fish icon templates were loaded; check resource_dir and tpl_fish_icon");
		return false;
	}

	*out = std::move(store);
	return true;
}

}  // namespace engine
