#pragma once

#include <string>

namespace infra {
namespace fs {

std::string joinPath(const std::string& dir, const std::string& file);
std::string dirNameOf(const std::string& path);
bool ensureDirExists(const std::string& dir);

}  // namespace fs
}  // namespace infra
