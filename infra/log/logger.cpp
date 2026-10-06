#include "logger.h"

#include <iostream>

namespace infra {
namespace log {

bool Logger::openAppend(const std::string& path) {
	file_.open(path, std::ios::out | std::ios::app);
	return file_.is_open();
}

void Logger::log(const std::string& line, bool alsoStdout) {
	if (alsoStdout) {
		std::cout << line << std::endl;
	}
	if (file_.is_open()) {
		file_ << line << '\n';
	}
}

bool Logger::hasFile() const {
	return file_.is_open();
}

void Logger::flush() {
	if (file_.is_open()) {
		file_.flush();
	}
}

void Logger::close() {
	if (file_.is_open()) {
		file_.close();
	}
}

}  // namespace log
}  // namespace infra
