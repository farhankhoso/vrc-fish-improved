#include "path_utils.h"

#include <algorithm>

#include <ShlObj.h>
#include <windows.h>

namespace infra {
namespace fs {

std::string joinPath(const std::string& dir, const std::string& file) {
	if (dir.empty()) {
		return file;
	}
	const char last = dir.back();
	if (last == '\\' || last == '/') {
		return dir + file;
	}
	return dir + "\\" + file;
}

std::string dirNameOf(const std::string& path) {
	const size_t pos = path.find_last_of("/\\");
	if (pos == std::string::npos || pos == 0) {
		return "";
	}
	return path.substr(0, pos);
}

bool ensureDirExists(const std::string& dir) {
	if (dir.empty()) {
		return false;
	}

	std::string path = dir;
	std::replace(path.begin(), path.end(), '/', '\\');

	DWORD attrs = GetFileAttributesA(path.c_str());
	if (attrs != INVALID_FILE_ATTRIBUTES) {
		return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	const int rc = SHCreateDirectoryExA(nullptr, path.c_str(), nullptr);
	if (rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS) {
		return true;
	}

	attrs = GetFileAttributesA(path.c_str());
	return (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

}  // namespace fs
}  // namespace infra
