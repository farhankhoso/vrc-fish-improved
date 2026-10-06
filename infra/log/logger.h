#pragma once

#include <fstream>
#include <string>

namespace infra {
namespace log {

class Logger {
public:
	bool openAppend(const std::string& path);
	void log(const std::string& line, bool alsoStdout = true);
	bool hasFile() const;
	void flush();
	void close();

private:
	std::ofstream file_;
};

}  // namespace log
}  // namespace infra
