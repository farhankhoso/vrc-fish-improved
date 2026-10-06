#pragma once

#include <string>

#include <opencv2/core/mat.hpp>
#include <windows.h>

namespace infra {
namespace win {
namespace capture {

bool captureDesktopRectBgr(const RECT& screenRect, cv::Mat* outBgr, std::string* error = nullptr);

}  // namespace capture
}  // namespace win
}  // namespace infra
