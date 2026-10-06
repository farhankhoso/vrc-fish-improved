#pragma once

#include <opencv2/core/types.hpp>

enum class VrFishState {
	Cast,
	WaitBite,
	EnterMinigame,
	ControlMinigame,
	PostMinigame,
};

struct TplMatch {
	cv::Point topLeft{};
	cv::Point center{};
	cv::Rect rect{};
	double score = 0.0;
};

struct FishSliderResult {
	int fishX{};
	int fishY{};
	int sliderCenterX{};
	int sliderCenterY{};
	int sliderTop{};
	int sliderBottom{};
	int sliderHeight{};
	double fishScore{};
	double sliderScore{};
	bool hasBounds{};
};
