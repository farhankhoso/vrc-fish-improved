#include "fish_engine.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <random>
#include <sstream>
#include <vector>

#include <windows.h>
#include <opencv2/imgproc.hpp>

#include "../runtime/runtime_context.h"
#include "controller.h"
#include "detectors.h"
#include "matcher.h"

namespace {

bool isKeyPressing(int vk) {
	return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

constexpr int kPressWindowSize = 10;

}  // namespace

namespace engine {

struct FishEngine::LoopState {
	VrFishState state{ VrFishState::Cast };
	unsigned long long stateStart{};
	int biteOkFrames{};
	int minigameMissingFrames{};
	bool holding{};
	bool castMouseMoved{};
	int castMouseMoveDx{};
	int castMouseMoveDy{};
	unsigned long long lastCtrlLogMs{};
	int prevSliderY{};
	bool hasPrevSlider{};
	double smoothVelocity{};
	int prevFishY{};
	bool hasPrevFish{};
	double smoothFishVel{};
	double prevSmoothFishVel{};
	double smoothFishAccel{};
	double prevDeviation{};
	bool hasPrevDeviation{};
	unsigned long long prevCtrlTs{};
	bool hasPrevTs{};
	double lastDtRatio{ 1.0 };
	int lastGoodSliderH{};
	int lastGoodSliderCY{};
	bool hasLastGoodPos{};
	int consecutiveMiss{};
	cv::Rect fixedTrackRoi{};
	bool hasFixedTrack{};
	bool relockPending{};
	double cachedTrackScale{ 1.0 };
	double cachedTrackAngle{};
	int cachedFishTplIdx{};
	int stuckSliderCY{};
	int stuckFrames{};
	bool hasCachedFishTpl{};
	int recordFrame{};
	std::deque<int> pressWindow;
};

FishEngine::FishEngine(runtime::RuntimeContext& runtimeContext)
	: runtime_(runtimeContext) {}

unsigned long long FishEngine::nowMs() const {
	return GetTickCount64();
}

void FishEngine::writeVrLogLine(const std::string& line, bool alsoStdout) const {
	runtime_.logVrLine(line, alsoStdout);
}

void FishEngine::switchState(LoopState& loop, VrFishState next) const {
	const AppConfig& config = runtime_.config();
	if (config.vr_debug || runtime_.hasVrLogFile()) {
		std::ostringstream oss;
		oss << "[vrchat_fish] state " << static_cast<int>(loop.state)
			<< " -> " << static_cast<int>(next);
		writeVrLogLine(oss.str(), config.vr_debug);
	}
	loop.state = next;
	loop.stateStart = nowMs();
}

void FishEngine::sleepWithPause(LoopState& loop, int totalMs) const {
	if (totalMs <= 0) {
		return;
	}
	int remaining = totalMs;
	while (remaining > 0) {
		while (runtime_.isPaused() && !stopRequested_.load()) {
			if (loop.holding) {
				runtime_.mouseLeftUp();
				loop.holding = false;
			}
			Sleep(1000);
		}
		if (stopRequested_.load()) {
			return;
		}
		const int chunk = (remaining > 50) ? 50 : remaining;
		Sleep(chunk);
		remaining -= chunk;
	}
}

void FishEngine::cleanupToNextRound(LoopState& loop, const std::string& tag) const {
	const AppConfig& config = runtime_.config();
	if (loop.holding) {
		runtime_.mouseLeftUp();
		loop.holding = false;
	}

	if (config.vr_debug || runtime_.hasVrLogFile()) {
		std::ostringstream oss;
		oss << "[vrchat_fish] cleanup tag=" << tag
			<< " wait_before=" << config.cleanup_wait_before_ms
			<< " clicks=" << config.cleanup_click_count
			<< " click_interval=" << config.cleanup_click_interval_ms
			<< " reel_key=" << config.cleanup_reel_key_name
			<< " wait_after=" << config.cleanup_wait_after_ms;
		writeVrLogLine(oss.str(), config.vr_debug);
	}

	sleepWithPause(loop, config.cleanup_wait_before_ms);

	int clicks = config.cleanup_click_count;
	if (clicks < 0) {
		clicks = 0;
	}
	int intervalMs = config.cleanup_click_interval_ms;
	if (intervalMs < 0) {
		intervalMs = 0;
	}
	for (int i = 0; i < clicks; ++i) {
		runtime_.mouseLeftClickCentered();
		if (intervalMs > 0 && i + 1 < clicks) {
			sleepWithPause(loop, intervalMs);
		}
	}

	if (config.cleanup_reel_key > 0) {
		runtime_.keyTapVk(static_cast<WORD>(config.cleanup_reel_key));
	}

	sleepWithPause(loop, config.cleanup_wait_after_ms);

	if (loop.castMouseMoved) {
		runtime_.mouseMoveRelative(-loop.castMouseMoveDx, -loop.castMouseMoveDy, "cast_mouse_restore");
		if (config.vr_debug || runtime_.hasVrLogFile()) {
			std::ostringstream oss;
			oss << "[vrchat_fish] cast mouse restore dx=" << -loop.castMouseMoveDx
				<< " dy=" << -loop.castMouseMoveDy;
			writeVrLogLine(oss.str(), config.vr_debug);
		}
		loop.castMouseMoved = false;
		loop.castMouseMoveDx = 0;
		loop.castMouseMoveDy = 0;
	}
}

void FishEngine::runCastStep(LoopState& loop) const {
	const AppConfig& config = runtime_.config();
	if (loop.holding) {
		runtime_.mouseLeftUp();
		loop.holding = false;
	}

	if (loop.castMouseMoved) {
		runtime_.mouseMoveRelative(-loop.castMouseMoveDx, -loop.castMouseMoveDy, "cast_mouse_restore_before_cast");
		loop.castMouseMoved = false;
		loop.castMouseMoveDx = 0;
		loop.castMouseMoveDy = 0;
	}

	runtime_.mouseLeftClickCentered();

	if (config.cast_mouse_move_dx != 0 || config.cast_mouse_move_dy != 0) {
		static std::random_device rd;
		static std::mt19937 gen(rd());
		int range = config.cast_mouse_move_random_range;
		if (range < 0) {
			range = 0;
		}
		std::uniform_int_distribution<> dist(-range, range);
		const int finalDx = config.cast_mouse_move_dx + dist(gen);
		const int finalDy = config.cast_mouse_move_dy + dist(gen);

		if (config.cast_mouse_move_delay_max > 0) {
			std::uniform_int_distribution<> delayDist(0, config.cast_mouse_move_delay_max);
			sleepWithPause(loop, delayDist(gen));
		}

		const int durationMs = config.cast_mouse_move_duration_ms;
		const int stepMs = config.cast_mouse_move_step_ms;
		if (durationMs > 0 && stepMs > 0) {
			int steps = (durationMs + stepMs - 1) / stepMs;
			if (steps < 1) {
				steps = 1;
			}

			int remainingDx = finalDx;
			int remainingDy = finalDy;
			for (int i = 0; i < steps; ++i) {
				int stepDx = 0;
				int stepDy = 0;
				if (i == steps - 1) {
					stepDx = remainingDx;
					stepDy = remainingDy;
				} else {
					const double ratio = static_cast<double>(stepMs) / durationMs;
					stepDx = static_cast<int>(finalDx * ratio);
					stepDy = static_cast<int>(finalDy * ratio);
				}

				if (stepDx != 0 || stepDy != 0) {
					runtime_.mouseMoveRelative(stepDx, stepDy, "cast_mouse_move_step");
				}

				remainingDx -= stepDx;
				remainingDy -= stepDy;
				if (i < steps - 1) {
					sleepWithPause(loop, stepMs);
				}
			}

			loop.castMouseMoved = true;
			loop.castMouseMoveDx = finalDx;
			loop.castMouseMoveDy = finalDy;
		} else {
			runtime_.mouseMoveRelative(finalDx, finalDy, "cast_mouse_move");
			loop.castMouseMoved = true;
			loop.castMouseMoveDx = finalDx;
			loop.castMouseMoveDy = finalDy;
		}

		if (config.vr_debug || runtime_.hasVrLogFile()) {
			std::ostringstream oss;
			oss << "[vrchat_fish] cast mouse move dx=" << finalDx
				<< " dy=" << finalDy
				<< " (base: " << config.cast_mouse_move_dx
				<< "," << config.cast_mouse_move_dy
				<< " range=" << range
				<< " delay=" << config.cast_mouse_move_delay_max << "ms";
			if (durationMs > 0) {
				oss << " smooth: dur=" << durationMs << "ms step=" << stepMs << "ms";
			}
			oss << ")";
			writeVrLogLine(oss.str(), config.vr_debug);
		}
	}

	Sleep(config.cast_delay_ms);
	loop.biteOkFrames = 0;
	switchState(loop, VrFishState::WaitBite);
}

void FishEngine::runEnterMinigameStep(LoopState& loop) const {
	const AppConfig& config = runtime_.config();
	if (nowMs() - loop.stateStart < static_cast<unsigned long long>(config.minigame_enter_delay_ms)) {
		Sleep(config.capture_interval_ms);
		return;
	}

	loop.minigameMissingFrames = 0;
	loop.hasPrevSlider = false;
	loop.hasPrevFish = false;
	loop.smoothVelocity = 0.0;
	loop.smoothFishVel = 0.0;
	loop.hasFixedTrack = false;
	loop.relockPending = false;
	loop.stuckFrames = 0;
	loop.cachedTrackScale = 1.0;
	loop.cachedTrackAngle = 0.0;
	loop.hasCachedFishTpl = false;
	loop.hasPrevTs = false;
	loop.lastDtRatio = 1.0;
	loop.lastGoodSliderH = 0;
	loop.lastGoodSliderCY = 0;
	loop.hasLastGoodPos = false;
	loop.consecutiveMiss = 0;

	if (config.ml_mode == 1) {
		loop.recordFrame = 0;
		loop.pressWindow.clear();
		std::string mlError;
		if (!runtime_.beginMlRecordSession(&mlError)) {
			runtime_.logConsoleLine(mlError);
		}
		runtime_.logConsoleLine("[ML] Record mode: recording started, control slider manually.");
	}

	switchState(loop, VrFishState::ControlMinigame);
}

void FishEngine::runWaitBiteStep(LoopState& loop, const cv::Mat& frame, const cv::Mat& gray) const {
	const AppConfig& config = runtime_.config();
	TplMatch m{};
	const bool ok = detectBite(gray, runtime_.templates(), config, &m);
	if (config.vr_debug) {
		std::ostringstream oss;
		oss << "[vrchat_fish] bite score=" << m.score << " ok=" << ok;
		runtime_.logConsoleLine(oss.str());
	}

	loop.biteOkFrames = ok ? (loop.biteOkFrames + 1) : 0;
	if (loop.biteOkFrames >= config.bite_confirm_frames) {
		saveDebugFrame(frame, "bite", m.rect);
		runtime_.mouseLeftClickCentered();
		loop.hasPrevSlider = false;
		loop.hasFixedTrack = false;
		loop.relockPending = false;
		loop.stuckFrames = 0;
		switchState(loop, VrFishState::EnterMinigame);
		return;
	}

	if (nowMs() - loop.stateStart > static_cast<unsigned long long>(config.bite_timeout_ms)) {
		if (config.vr_debug) {
			runtime_.logConsoleLine("[vrchat_fish] bite timeout -> recast");
		}
		saveDebugFrame(frame, "bite_timeout");
		cleanupToNextRound(loop, "bite_timeout");
		switchState(loop, VrFishState::Cast);
		return;
	}

	Sleep(config.capture_interval_ms);
}

void FishEngine::runControlMinigameStep(LoopState& loop, const cv::Mat& frame, const cv::Mat& gray) const {
	const AppConfig& config = runtime_.config();
	const unsigned long long loopStart = nowMs();
	const double baseDtMs = std::max(1.0, config.base_dt_ms);

	auto sleepControlInterval = [&]() {
		int intervalMs = config.control_interval_ms;
		if (intervalMs < 1) {
			intervalMs = 1;
		}
		const unsigned long long elapsedMs = nowMs() - loopStart;
		if (elapsedMs < static_cast<unsigned long long>(intervalMs)) {
			Sleep(static_cast<DWORD>(static_cast<unsigned long long>(intervalMs) - elapsedMs));
		} else {
			Sleep(1);
		}
	};

	cv::Rect searchRoi = centerThirdStripRoi(gray.size());
	// Exclude the bottom HUD band (level/coins icon, health bar) from the
	// initial track-lock search -- see search_bottom_margin_pct in
	// app_config.cpp for why. This only shrinks the search area; it does not
	// touch coordinate spaces anywhere downstream since
	// matchBestRoiTrackBarAutoScale already takes searchRoi as a sub-region
	// of the full frame and returns absolute coordinates.
	if (config.search_bottom_margin_pct > 0 && searchRoi.height > 0) {
		int trimBottom = static_cast<int>(
			std::llround(searchRoi.height * (config.search_bottom_margin_pct / 100.0)));
		searchRoi.height = std::max(1, searchRoi.height - trimBottom);
	}

	// Normal end-of-minigame miss budget (fish/slider genuinely gone).
	int normalEndFrames = config.game_end_confirm_frames;
	if (normalEndFrames < config.minigame_end_min_frames) {
		normalEndFrames = config.minigame_end_min_frames;
	}

	if (!loop.hasFixedTrack) {
		double barScale = 1.0;
		double barAngle = 0.0;
		TplMatch barMatch = matchBestRoiTrackBarAutoScale(
			gray,
			runtime_.templates().minigameBarFull,
			searchRoi,
			config,
			cv::TM_CCOEFF_NORMED,
			&barScale,
			&barAngle);

		if (barMatch.score >= config.minigame_threshold) {
			const int trackX = barMatch.rect.x;
			const int trackW = barMatch.rect.width;
			const int trackY = barMatch.rect.y;
			const int trackH = barMatch.rect.height;
			const int halfX = trackX + trackW / 2;
			const int halfW = trackW - trackW / 2;
			int padY = config.track_pad_y;
			if (padY < 0) {
				padY = 0;
			}

			loop.fixedTrackRoi = cv::Rect(halfX, trackY - padY, halfW, trackH + padY * 2);
			loop.fixedTrackRoi = clampRect(loop.fixedTrackRoi, gray.size());
			loop.hasFixedTrack = true;
			loop.relockPending = false; // clean re-lock succeeded, back to normal miss budget
			loop.stuckFrames = 0;
			loop.cachedTrackScale = barScale;
			loop.cachedTrackAngle = barAngle;

			saveDebugFrame(frame, "track_lock", searchRoi, barMatch.rect, loop.fixedTrackRoi);
			if (config.vr_debug || runtime_.hasVrLogFile()) {
				std::ostringstream oss;
				oss << "[ctrl] track locked (full tpl): x=" << loop.fixedTrackRoi.x
					<< " y=" << loop.fixedTrackRoi.y
					<< " w=" << loop.fixedTrackRoi.width
					<< " h=" << loop.fixedTrackRoi.height
					<< " (bar score=" << barMatch.score
					<< " scale=" << barScale
					<< " angle=" << barAngle << ")";
				writeVrLogLine(oss.str(), config.vr_debug);
			}
		} else {
			saveDebugFrame(frame, "track_miss", searchRoi, barMatch.rect);
			loop.minigameMissingFrames++;

			int trackLockMaxMiss = config.game_end_confirm_frames * config.track_lock_miss_multiplier;
			if (trackLockMaxMiss < config.track_lock_miss_min_frames) {
				trackLockMaxMiss = config.track_lock_miss_min_frames;
			}
			if (loop.relockPending) {
				// This is a mid-minigame recovery attempt, not the initial
				// lock-on — don't let it borrow the initial lock's generous
				// (60-frame) budget, or a genuine minigame end gets mistaken
				// for "still trying to recover" and aborts ~6x slower than
				// it used to (10 frames -> 60 frames). Give it a bounded
				// extra window on top of the normal end budget instead.
				int relockCap = normalEndFrames + 2 * config.track_relock_miss_frames;
				if (trackLockMaxMiss > relockCap) {
					trackLockMaxMiss = relockCap;
				}
			}
			if (config.vr_debug || runtime_.hasVrLogFile()) {
				std::ostringstream oss;
				oss << "[ctrl] track detect MISS (score=" << barMatch.score
					<< " scale=" << barScale
					<< " angle=" << barAngle
					<< ") miss=" << loop.minigameMissingFrames << "/" << trackLockMaxMiss;
				writeVrLogLine(oss.str(), config.vr_debug);
			}
			if (loop.minigameMissingFrames >= trackLockMaxMiss) {
				if (loop.holding) {
					runtime_.mouseLeftUp();
					loop.holding = false;
				}
				saveDebugFrame(frame, "track_lock_timeout", searchRoi);
				switchState(loop, VrFishState::PostMinigame);
			}
			sleepControlInterval();
			return;
		}
	}

	const cv::Rect matchRoi = loop.fixedTrackRoi;
	FishSliderResult det{};
	bool ok = false;
	bool didFullDetect = false;

	if (!loop.hasCachedFishTpl) {
		int bestIdx = 0;
		ok = detectFishAndSliderFull(
			gray,
			matchRoi,
			runtime_.templates(),
			config,
			loop.cachedTrackScale,
			loop.cachedTrackAngle,
			&det,
			&bestIdx);
		if (ok) {
			loop.cachedFishTplIdx = bestIdx;
			loop.hasCachedFishTpl = true;
		}
		didFullDetect = true;
	} else {
		ok = detectFishAndSliderFast(
			gray,
			matchRoi,
			runtime_.templates(),
			config,
			loop.cachedTrackScale,
			loop.cachedTrackAngle,
			loop.cachedFishTplIdx,
			&det);
		if (!ok) {
			int bestIdx = 0;
			ok = detectFishAndSliderFull(
				gray,
				matchRoi,
				runtime_.templates(),
				config,
				loop.cachedTrackScale,
				loop.cachedTrackAngle,
				&det,
				&bestIdx);
			if (ok) {
				loop.cachedFishTplIdx = bestIdx;
			}
			didFullDetect = true;
		}
	}

	const unsigned long long detectMs = nowMs() - loopStart;

	if (!ok) {
		if (config.vr_debug || runtime_.hasVrLogFile()) {
			std::ostringstream oss;
			oss << "[ctrl] " << detectMs << "ms"
				<< (didFullDetect ? " [full]" : " [fast]")
				<< " MISS fs=" << det.fishScore
				<< " ss=" << det.sliderScore
				<< " hold=" << (loop.holding ? 1 : 0);
			writeVrLogLine(oss.str(), config.vr_debug);
		}
		loop.minigameMissingFrames++;
		loop.consecutiveMiss++;

		int missReleaseFrames = config.miss_release_frames;
		if (missReleaseFrames < 1) {
			missReleaseFrames = 1;
		}
		if (loop.consecutiveMiss >= missReleaseFrames && loop.holding) {
			runtime_.mouseLeftUp();
			loop.holding = false;
		}

		// Recovery step: if we've been missing for a while *and* the track
		// ROI is still the one located at lock-on, force a fresh track-bar
		// search on the next tick rather than continuing to poll a
		// possibly-stale/occluded ROI until the whole minigame times out.
		// This is what recovers from fog / lure-launch disrupting the frame
		// right when the track was first located.
		int relockFrames = config.track_relock_miss_frames;
		if (relockFrames < 1) {
			relockFrames = 1;
		}
		if (loop.hasFixedTrack && loop.consecutiveMiss >= relockFrames) {
			if (config.vr_debug || runtime_.hasVrLogFile()) {
				writeVrLogLine("[ctrl] consecutive miss threshold hit -> re-searching track bar", config.vr_debug);
			}
			loop.hasFixedTrack = false;
			loop.hasCachedFishTpl = false;
			loop.relockPending = true;
		}

		int endFrames = normalEndFrames;
		if (loop.minigameMissingFrames >= endFrames) {
			if (loop.holding) {
				runtime_.mouseLeftUp();
				loop.holding = false;
			}
			saveDebugFrame(frame, "minigame_end", loop.fixedTrackRoi);
			switchState(loop, VrFishState::PostMinigame);
		}
		sleepControlInterval();
		return;
	}

	const bool wasLongMiss = (loop.consecutiveMiss >= 2);
	loop.minigameMissingFrames = 0;
	loop.consecutiveMiss = 0;

	if (!det.hasBounds && loop.hasLastGoodPos) {
		const int scyJump = std::abs(det.sliderCenterY - loop.lastGoodSliderCY);
		if (scyJump > config.slider_tpl_jump_threshold) {
			det.sliderCenterY = loop.lastGoodSliderCY;
			det.sliderTop = loop.lastGoodSliderCY - loop.lastGoodSliderH / 2;
			det.sliderBottom = loop.lastGoodSliderCY + loop.lastGoodSliderH / 2;
			det.sliderHeight = loop.lastGoodSliderH;
		}
	}

	if (loop.hasPrevFish) {
		const int fishJump = std::abs(det.fishY - loop.prevFishY);
		if (fishJump > config.fish_jump_threshold) {
			det.fishY = loop.prevFishY;
		}
	}

	if (det.sliderHeight >= config.slider_height_stable_min) {
		loop.lastGoodSliderH = det.sliderHeight;
	} else if (loop.lastGoodSliderH > 0) {
		det.sliderHeight = loop.lastGoodSliderH;
		det.sliderTop = det.sliderCenterY - loop.lastGoodSliderH / 2;
		det.sliderBottom = det.sliderCenterY + loop.lastGoodSliderH / 2;
	}

	if (det.hasBounds) {
		loop.lastGoodSliderCY = det.sliderCenterY;
		loop.hasLastGoodPos = true;
	}

	const int fishY = det.fishY;
	const int sliderCY = det.sliderCenterY;
	const int sliderH = det.sliderHeight;
	const unsigned long long t = nowMs();

	double dtMs = baseDtMs;
	if (loop.hasPrevTs && t > loop.prevCtrlTs) {
		dtMs = static_cast<double>(t - loop.prevCtrlTs);
		if (dtMs < 1.0) {
			dtMs = 1.0;
		}
		if (dtMs > 1000.0) {
			dtMs = 1000.0;
		}
	}

	if (config.vr_debug || runtime_.hasVrLogFile()) {
		std::ostringstream oss;
		oss << "[ctrl] " << detectMs << "ms"
			<< (didFullDetect ? " [full]" : " [fast]")
			<< " dt=" << static_cast<int>(dtMs) << "ms"
			<< " t=" << t
			<< " fishY=" << fishY
			<< " sCY=" << sliderCY
			<< " sH=" << sliderH
			<< (det.hasBounds ? " [color]" : " [tpl]")
			<< " hold=" << (loop.holding ? 1 : 0);
		writeVrLogLine(oss.str(), config.vr_debug);
	}

	loop.lastDtRatio = dtMs / baseDtMs;
	loop.prevCtrlTs = t;
	loop.hasPrevTs = true;

	// Stuck-detection watchdog: a genuinely active minigame slider shouldn't
	// sit at the exact same pixel position for a long stretch while we're
	// not holding. If it does, we're very likely locked onto a static
	// element (see search_bottom_margin_pct) rather than the real slider.
	if (!loop.holding && sliderCY == loop.stuckSliderCY) {
		loop.stuckFrames++;
	} else {
		loop.stuckSliderCY = sliderCY;
		loop.stuckFrames = 0;
	}
	if (loop.hasFixedTrack && loop.stuckFrames >= config.stuck_slider_watchdog_frames) {
		if (config.vr_debug || runtime_.hasVrLogFile()) {
			writeVrLogLine("[ctrl] stuck-slider watchdog fired -> re-searching track bar", config.vr_debug);
		}
		loop.hasFixedTrack = false;
		loop.hasCachedFishTpl = false;
		loop.relockPending = true;
		loop.stuckFrames = 0;
	}

	double alpha = config.velocity_ema_alpha;
	if (alpha < 0.05) {
		alpha = 0.05;
	}
	if (alpha > 1.0) {
		alpha = 1.0;
	}

	if (loop.hasPrevSlider) {
		int jumpThresh = config.slider_tpl_jump_threshold;
		if (jumpThresh < 50) {
			jumpThresh = 50;
		}
		if (std::abs(sliderCY - loop.prevSliderY) > jumpThresh) {
			loop.hasPrevSlider = false;
			loop.smoothVelocity = 0.0;
		}
	}

	if (wasLongMiss || dtMs > 300.0) {
		double decayFactor = 0.3;
		if (dtMs > 500.0) {
			decayFactor = 0.1;
		}
		if (dtMs > 800.0) {
			decayFactor = 0.0;
		}
		loop.smoothVelocity *= decayFactor;
		loop.smoothFishVel *= decayFactor;
		loop.smoothFishAccel *= decayFactor;
		loop.prevSmoothFishVel = loop.smoothFishVel;
		loop.hasPrevDeviation = false;
		loop.hasPrevSlider = false;
		loop.hasPrevFish = false;
	}

	const double rawV = loop.hasPrevSlider
		? static_cast<double>(sliderCY - loop.prevSliderY) / loop.lastDtRatio
		: 0.0;
	if (!loop.hasPrevSlider) {
		loop.smoothVelocity = 0.0;
	} else {
		loop.smoothVelocity = alpha * rawV + (1.0 - alpha) * loop.smoothVelocity;
		double maxVel = config.slider_velocity_cap;
		if (maxVel < 1.0) {
			maxVel = 1.0;
		}
		if (loop.smoothVelocity > maxVel) {
			loop.smoothVelocity = maxVel;
		}
		if (loop.smoothVelocity < -maxVel) {
			loop.smoothVelocity = -maxVel;
		}
	}

	const double rawFV = loop.hasPrevFish
		? static_cast<double>(fishY - loop.prevFishY) / loop.lastDtRatio
		: 0.0;
	if (!loop.hasPrevFish) {
		loop.smoothFishVel = 0.0;
	} else {
		loop.smoothFishVel = alpha * rawFV + (1.0 - alpha) * loop.smoothFishVel;
		double fishVelCap = config.fish_velocity_cap;
		if (fishVelCap < 1.0) {
			fishVelCap = 1.0;
		}
		if (loop.smoothFishVel > fishVelCap) {
			loop.smoothFishVel = fishVelCap;
		}
		if (loop.smoothFishVel < -fishVelCap) {
			loop.smoothFishVel = -fishVelCap;
		}
	}

	{
		double accelAlpha = config.fish_accel_alpha;
		if (accelAlpha < 0.05) {
			accelAlpha = 0.05;
		}
		if (accelAlpha > 1.0) {
			accelAlpha = 1.0;
		}

		const double rawAccel = loop.smoothFishVel - loop.prevSmoothFishVel;
		loop.smoothFishAccel = accelAlpha * rawAccel + (1.0 - accelAlpha) * loop.smoothFishAccel;
		double accelCap = config.fish_accel_cap;
		if (accelCap < 0.5) {
			accelCap = 0.5;
		}
		if (loop.smoothFishAccel > accelCap) {
			loop.smoothFishAccel = accelCap;
		}
		if (loop.smoothFishAccel < -accelCap) {
			loop.smoothFishAccel = -accelCap;
		}
		loop.prevSmoothFishVel = loop.smoothFishVel;
	}

	if (config.ml_mode == 1) {
		const int dy = sliderCY - fishY;
		const double sliderYNorm = (gray.rows > 0) ? static_cast<double>(sliderCY) / gray.rows : 0.5;
		const int mousePressed = isKeyPressing(VK_LBUTTON) ? 1 : 0;
		loop.pressWindow.push_back(mousePressed);
		if (static_cast<int>(loop.pressWindow.size()) > kPressWindowSize) {
			loop.pressWindow.pop_front();
		}

		double dutyLabel = -1.0;
		if (static_cast<int>(loop.pressWindow.size()) >= kPressWindowSize) {
			int sum = 0;
			for (int v : loop.pressWindow) {
				sum += v;
			}
			dutyLabel = static_cast<double>(sum) / kPressWindowSize;
		}

		std::ostringstream csv;
		csv << loop.recordFrame << ","
			<< t << ","
			<< fishY << ","
			<< sliderCY << ","
			<< dy << ","
			<< loop.smoothVelocity << ","
			<< loop.smoothFishVel << ","
			<< sliderYNorm << ","
			<< mousePressed << ","
			<< dutyLabel;
		runtime_.appendMlRecordLine(csv.str());
		loop.recordFrame++;

		if (config.vr_debug && (t - loop.lastCtrlLogMs >= 500)) {
			std::ostringstream oss;
			oss << "[ML:record] frame=" << loop.recordFrame
				<< " dy=" << dy
				<< " sv=" << static_cast<int>(loop.smoothVelocity)
				<< " fv=" << static_cast<int>(loop.smoothFishVel)
				<< " sH=" << sliderH
				<< " mouse=" << mousePressed;
			runtime_.logConsoleLine(oss.str());
			loop.lastCtrlLogMs = t;
		}
	} else {
		ControlInput input{};
		input.fishY = fishY;
		input.sliderCenterY = sliderCY;
		input.sliderHeight = sliderH;
		input.smoothSliderVelocity = loop.smoothVelocity;
		input.smoothFishVelocity = loop.smoothFishVel;
		input.smoothFishAccel = loop.smoothFishAccel;
		input.holding = loop.holding;
		input.fixedTrackRoi = loop.fixedTrackRoi;
		input.lastDtRatio = loop.lastDtRatio;
		input.prevDeviation = loop.prevDeviation;
		input.hasPrevDeviation = loop.hasPrevDeviation;

		ControlDecision decision = computeControlDecision(input, config);
		const bool wantHold = decision.wantHold;
		const bool reactiveTriggered = decision.reactiveTriggered;
		const double costPress = decision.costPress;
		const double costRelease = decision.costRelease;
		loop.prevDeviation = decision.deviationForNext;
		loop.hasPrevDeviation = decision.hasDeviationForNext;

		if (wantHold && !loop.holding) {
			runtime_.mouseLeftDown();
			loop.holding = true;
		} else if (!wantHold && loop.holding) {
			runtime_.mouseLeftUp();
			loop.holding = false;
		}

			if (config.vr_debug || runtime_.hasVrLogFile()) {
				int logIntervalMs = config.bb_log_interval_ms;
				if (logIntervalMs < 0) {
					logIntervalMs = 0;
				}
				if (logIntervalMs == 0 || t - loop.lastCtrlLogMs >= static_cast<unsigned long long>(logIntervalMs)) {
					std::ostringstream oss;
					oss << "[MPC] dt=" << static_cast<int>(loop.lastDtRatio * baseDtMs) << "ms"
						<< " fishY=" << fishY
						<< " sCY=" << sliderCY
						<< " sH=" << sliderH
						<< " sv=" << static_cast<int>(loop.smoothVelocity)
						<< " fv=" << static_cast<int>(loop.smoothFishVel)
						<< " fa=" << static_cast<int>(loop.smoothFishAccel)
						<< " cP=" << static_cast<int>(costPress)
						<< " cR=" << static_cast<int>(costRelease)
						<< " hold=" << (loop.holding ? 1 : 0)
						<< (reactiveTriggered ? " [reactive]" : "");
					writeVrLogLine(oss.str(), config.vr_debug);
					loop.lastCtrlLogMs = t;
				}
			}
		}

		loop.prevSliderY = sliderCY;
		loop.hasPrevSlider = true;
		loop.prevFishY = fishY;
		loop.hasPrevFish = true;

	sleepControlInterval();
}

void FishEngine::runPostMinigameStep(LoopState& loop, const cv::Mat& frame) const {
	const AppConfig& config = runtime_.config();
	saveDebugFrame(frame, "post_minigame");
	if (config.ml_mode == 1 && runtime_.isMlRecordSessionOpen()) {
		runtime_.flushMlRecordSession();
		runtime_.logConsoleLine("[ML] Round record completed, frames written: " + std::to_string(loop.recordFrame));
	}
	cleanupToNextRound(loop, "post_minigame");
	switchState(loop, VrFishState::Cast);
}

void FishEngine::saveDebugFrame(const cv::Mat& bgr, const std::string& tag) const {
	runtime_.saveDebugImage(bgr, tag);
}

void FishEngine::saveDebugFrame(const cv::Mat& bgr, const std::string& tag, const cv::Rect& r1, const cv::Scalar& c1) const {
	if (!runtime_.config().vr_debug_pic || bgr.empty()) {
		return;
	}
	cv::Mat out = bgr.clone();
	cv::rectangle(out, r1, c1, 2, 8, 0);
	runtime_.saveDebugImage(out, tag);
}

void FishEngine::saveDebugFrame(const cv::Mat& bgr, const std::string& tag, const cv::Rect& r1, const cv::Rect& r2) const {
	if (!runtime_.config().vr_debug_pic || bgr.empty()) {
		return;
	}
	cv::Mat out = bgr.clone();
	cv::rectangle(out, r1, cv::Scalar(0, 0, 255), 2, 8, 0);
	cv::rectangle(out, r2, cv::Scalar(0, 255, 0), 2, 8, 0);
	runtime_.saveDebugImage(out, tag);
}

void FishEngine::saveDebugFrame(const cv::Mat& bgr, const std::string& tag, const cv::Rect& r1,
	const cv::Rect& r2, const cv::Rect& r3) const {
	if (!runtime_.config().vr_debug_pic || bgr.empty()) {
		return;
	}
	cv::Mat out = bgr.clone();
	cv::rectangle(out, r1, cv::Scalar(255, 0, 0), 2, 8, 0);
	cv::rectangle(out, r2, cv::Scalar(0, 255, 0), 2, 8, 0);
	cv::rectangle(out, r3, cv::Scalar(0, 0, 255), 2, 8, 0);
	runtime_.saveDebugImage(out, tag);
}

void FishEngine::togglePause() {
	runtime_.togglePause();
}

bool FishEngine::isPaused() const {
	return runtime_.isPaused();
}

void FishEngine::requestStop() {
	stopRequested_.store(true);
}

void FishEngine::runLoop() {
	AppConfig& config = runtime_.config();
	LoopState loop{};
	loop.stateStart = nowMs();
	stopRequested_.store(false);

	if (config.ml_mode == 2 && !mlpModel_.loaded) {
		std::string mlError;
		std::vector<std::string> mlNotes;
		if (!loadMlpWeights(config.ml_weights_file, mlpModel_, &mlError, &mlNotes)) {
			runtime_.logConsoleLine(mlError);
			runtime_.logConsoleLine("[ML] Failed to load weights, fallback to PD mode.");
			config.ml_mode = 0;
		} else {
			for (const std::string& note : mlNotes) {
				runtime_.logConsoleLine(note);
			}
		}
	}

	if (config.vr_debug || runtime_.hasVrLogFile()) {
		writeVrLogLine("[vrchat_fish] engine loop started", config.vr_debug);
	}

	while (!stopRequested_.load()) {
		while (runtime_.isPaused() && !stopRequested_.load()) {
			if (loop.holding) {
				runtime_.mouseLeftUp();
				loop.holding = false;
			}
			Sleep(1000);
		}
		if (stopRequested_.load()) {
			break;
		}

		if (loop.state == VrFishState::Cast) {
			runCastStep(loop);
			continue;
		}

		if (loop.state == VrFishState::EnterMinigame) {
			runEnterMinigameStep(loop);
			continue;
		}

			cv::Mat frame;
			std::string captureError;
			if (!runtime_.captureBgr(frame, &captureError)) {
				writeVrLogLine("[vrchat_fish] capture failed: " + captureError, true);
				int waitMs = config.capture_interval_ms;
				if (waitMs < 1) {
					waitMs = 30;
			}
			Sleep(static_cast<DWORD>(waitMs));
			continue;
			}
			if (frame.empty()) {
				writeVrLogLine("[vrchat_fish] capture returned empty frame", true);
				Sleep(30);
				continue;
			}

			cv::Mat gray;
			try {
				cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
			} catch (const cv::Exception& ex) {
				writeVrLogLine(std::string("[vrchat_fish] cvtColor failed after capture: ") + ex.what(), true);
				Sleep(30);
				continue;
			}

		if (loop.state == VrFishState::WaitBite) {
			runWaitBiteStep(loop, frame, gray);
			continue;
		}

		if (loop.state == VrFishState::ControlMinigame) {
			runControlMinigameStep(loop, frame, gray);
			continue;
		}

		if (loop.state == VrFishState::PostMinigame) {
			runPostMinigameStep(loop, frame);
			continue;
		}
	}

	if (loop.holding) {
		runtime_.mouseLeftUp();
		loop.holding = false;
	}
	runtime_.endMlRecordSession();
	if (config.vr_debug || runtime_.hasVrLogFile()) {
		writeVrLogLine("[vrchat_fish] engine loop stopped", config.vr_debug);
	}
}

}  // namespace engine
