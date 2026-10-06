#pragma once

#include <string>

struct AppConfig {
	bool is_pause{};

	std::string window_class;
	std::string window_title_contains;
	int force_resolution{};
	int target_width{};
	int target_height{};
	int capture_interval_ms{};
	int cast_delay_ms{};
	int cast_mouse_move_dx{};
	int cast_mouse_move_dy{};
	int cast_mouse_move_random_range{};
	int cast_mouse_move_delay_max{};
	int cast_mouse_move_duration_ms{};
	int cast_mouse_move_step_ms{};
	int bite_timeout_ms{};
	int minigame_enter_delay_ms{};
	int cleanup_wait_before_ms{};
	int cleanup_click_count{};
	int cleanup_click_interval_ms{};
	std::string cleanup_reel_key_name;
	int cleanup_reel_key{};
	int cleanup_wait_after_ms{};
	int bite_confirm_frames{};
	int game_end_confirm_frames{};
	double bite_threshold{};
	double minigame_threshold{};
	double fish_icon_threshold{};
	double slider_threshold{};
	int control_interval_ms{};
	double velocity_ema_alpha{};
	int slider_bright_thresh{};
	int slider_min_height{};
	double bb_gravity{};
	double bb_thrust{};
	double bb_drag{};
	int bb_sim_horizon{};
	double bb_margin_ratio{};
	double bb_boundary_zone{};
	double bb_boundary_weight{};
	int bb_log_interval_ms{};
	int ml_mode{};
	std::string ml_record_csv;
	std::string ml_weights_file;

	std::string resource_dir;
	std::string tpl_bite_exclamation_bottom;
	std::string tpl_bite_exclamation_full;
	std::string tpl_minigame_bar_full;
	std::string tpl_fish_icon;
	std::string tpl_fish_icon_alt;
	std::string tpl_fish_icon_alt2;
	std::string tpl_player_slider;

	double fish_scale_1{};
	double fish_scale_2{};
	double fish_scale_3{};
	double fish_scale_4{};
	double track_scale_1{};
	double track_scale_2{};
	double track_scale_3{};
	double track_scale_4{};
	double track_scale_min{};
	double track_scale_max{};
	double track_scale_step{};
	int track_scale_refine_topk{};
	double track_scale_refine_radius{};
	double track_scale_refine_step{};
	double track_angle_min{};
	double track_angle_max{};
	double track_angle_step{};
	int track_angle_refine_topk{};
	double track_angle_refine_radius{};
	double track_angle_refine_step{};

	int slider_detect_half_width{};
	int slider_detect_merge_gap{};
	int track_pad_y{};
	int search_bottom_margin_pct{};
	int track_lock_miss_multiplier{};
	int track_lock_miss_min_frames{};
	int track_relock_miss_frames{};
	int slider_bright_auto{};
	int stuck_slider_watchdog_frames{};
	int miss_release_frames{};
	int minigame_end_min_frames{};
	int slider_tpl_jump_threshold{};
	int fish_jump_threshold{};
	int slider_height_stable_min{};
	double slider_velocity_cap{};
	double fish_velocity_cap{};
	double base_dt_ms{};

	int fish_bounce_predict{};
	double fish_accel_alpha{};
	double fish_vel_decay{};
	double fish_accel_cap{};

	int reactive_override{};
	double reactive_dev_ratio{};
	double reactive_grow_threshold{};

	bool vr_debug{};
	bool vr_debug_pic{};
	std::string vr_debug_dir;
	std::string vr_log_file;
};

AppConfig loadAppConfig(const std::string& path);
bool loadAppConfig(const std::string& path, AppConfig* outConfig, std::string* error = nullptr);
