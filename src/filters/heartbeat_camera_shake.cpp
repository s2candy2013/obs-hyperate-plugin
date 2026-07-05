#include "filters/heartbeat_camera_shake.hpp"

#include "heart_rate/heart_rate_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

enum ShakePreset {
	ShakePresetSubtle = 0,
	ShakePresetNormal = 1,
	ShakePresetIntense = 2,
	ShakePresetCustom = 3,
};

enum ShakeMotionStyle {
	ShakeMotionPulseClassic = 0,
	ShakeMotionBeatDouble = 1,
	ShakeMotionBounce = 2,
	ShakeMotionShake = 3,
};

struct HeartbeatCameraShakeFilter {
	obs_source_t *source = nullptr;
	int motion_style = ShakeMotionPulseClassic;
	int intensity_preset = ShakePresetNormal;
	double threshold_bpm = 100.0;
	double max_bpm = 180.0;
	double max_shake_px = 18.0;
	double smoothing = 0.35;
	double pulse_decay = 0.28;
	double overscan_scale = 1.03;
	bool test_mode = false;
	double test_bpm = 120.0;

	double beat_phase = 0.0;
	float current_x = 0.0f;
	float current_y = 0.0f;
	float current_scale_delta = 0.0f;
	std::chrono::steady_clock::time_point last_frame = std::chrono::steady_clock::now();
};

double clamp01(double value)
{
	return std::clamp(value, 0.0, 1.0);
}

void apply_preset(HeartbeatCameraShakeFilter *filter)
{
	switch (filter->intensity_preset) {
	case ShakePresetSubtle:
		filter->threshold_bpm = 98.0;
		filter->max_bpm = 170.0;
		filter->max_shake_px = 18.0;
		filter->smoothing = 0.42;
		filter->pulse_decay = 0.22;
		filter->overscan_scale = 1.025;
		break;
	case ShakePresetIntense:
		filter->threshold_bpm = 82.0;
		filter->max_bpm = 150.0;
		filter->max_shake_px = 92.0;
		filter->smoothing = 0.12;
		filter->pulse_decay = 0.34;
		filter->overscan_scale = 1.13;
		break;
	case ShakePresetNormal:
		filter->threshold_bpm = 92.0;
		filter->max_bpm = 165.0;
		filter->max_shake_px = 44.0;
		filter->smoothing = 0.24;
		filter->pulse_decay = 0.28;
		filter->overscan_scale = 1.065;
		break;
	case ShakePresetCustom:
	default:
		break;
	}
}

const char *shake_get_name(void *)
{
	return obs_module_text("Filter.CameraShake.Name");
}

void shake_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<HeartbeatCameraShakeFilter *>(data);
	filter->motion_style = static_cast<int>(obs_data_get_int(settings, "motion_style"));
	filter->intensity_preset = static_cast<int>(obs_data_get_int(settings, "intensity_preset"));
	filter->threshold_bpm = obs_data_get_double(settings, "threshold_bpm");
	filter->max_bpm = std::max(filter->threshold_bpm + 1.0, obs_data_get_double(settings, "max_bpm"));
	filter->max_shake_px = obs_data_get_double(settings, "max_shake_px");
	filter->smoothing = clamp01(obs_data_get_double(settings, "smoothing"));
	filter->pulse_decay = std::max(0.02, obs_data_get_double(settings, "pulse_decay"));
	filter->overscan_scale = std::max(1.0, obs_data_get_double(settings, "overscan_scale"));
	filter->test_mode = obs_data_get_bool(settings, "test_mode");
	filter->test_bpm = obs_data_get_double(settings, "test_bpm");
	apply_preset(filter);
}

void *shake_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new HeartbeatCameraShakeFilter;
	filter->source = source;
	shake_update(filter, settings);
	return filter;
}

void shake_destroy(void *data)
{
	delete static_cast<HeartbeatCameraShakeFilter *>(data);
}

bool update_property_visibility(obs_properties_t *props, obs_data_t *settings)
{
	const bool custom = obs_data_get_int(settings, "intensity_preset") == ShakePresetCustom;
	const bool test_mode = obs_data_get_bool(settings, "test_mode");

	for (const char *name : {"threshold_bpm", "max_bpm", "max_shake_px", "smoothing", "pulse_decay",
				 "overscan_scale"}) {
		obs_property_set_visible(obs_properties_get(props, name), custom);
	}

	obs_property_set_visible(obs_properties_get(props, "test_bpm"), test_mode);
	return true;
}

bool preset_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	return update_property_visibility(props, settings);
}

bool test_mode_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	return update_property_visibility(props, settings);
}

obs_properties_t *shake_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *motion = obs_properties_add_list(props, "motion_style",
							 obs_module_text("Filter.CameraShake.MotionStyle"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(motion, obs_module_text("Filter.CameraShake.MotionStyle.PulseClassic"),
				  ShakeMotionPulseClassic);
	obs_property_list_add_int(motion, obs_module_text("Filter.CameraShake.MotionStyle.BeatDouble"),
				  ShakeMotionBeatDouble);
	obs_property_list_add_int(motion, obs_module_text("Filter.CameraShake.MotionStyle.Bounce"),
				  ShakeMotionBounce);
	obs_property_list_add_int(motion, obs_module_text("Filter.CameraShake.MotionStyle.Shake"), ShakeMotionShake);

	obs_property_t *preset = obs_properties_add_list(props, "intensity_preset",
							  obs_module_text("Filter.CameraShake.Preset"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(preset, obs_module_text("Filter.CameraShake.Preset.Subtle"), ShakePresetSubtle);
	obs_property_list_add_int(preset, obs_module_text("Filter.CameraShake.Preset.Normal"), ShakePresetNormal);
	obs_property_list_add_int(preset, obs_module_text("Filter.CameraShake.Preset.Intense"), ShakePresetIntense);
	obs_property_list_add_int(preset, obs_module_text("Filter.CameraShake.Preset.Custom"), ShakePresetCustom);
	obs_property_set_modified_callback(preset, preset_changed);

	obs_property_t *test_mode =
		obs_properties_add_bool(props, "test_mode", obs_module_text("Filter.CameraShake.TestMode"));
	obs_property_set_modified_callback(test_mode, test_mode_changed);
	obs_properties_add_float_slider(props, "test_bpm", obs_module_text("Filter.CameraShake.TestBpm"), 40.0,
					220.0, 1.0);

	obs_properties_add_float_slider(props, "threshold_bpm", obs_module_text("Filter.CameraShake.ThresholdBpm"),
					40.0, 220.0, 1.0);
	obs_properties_add_float_slider(props, "max_bpm", obs_module_text("Filter.CameraShake.MaxBpm"), 80.0,
					240.0, 1.0);
	obs_properties_add_float_slider(props, "max_shake_px", obs_module_text("Filter.CameraShake.MaxShakePx"),
					0.0, 140.0, 0.5);
	obs_properties_add_float_slider(props, "smoothing", obs_module_text("Filter.CameraShake.Smoothing"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(props, "pulse_decay", obs_module_text("Filter.CameraShake.PulseDecay"),
					0.02, 1.0, 0.01);
	obs_properties_add_float_slider(props, "overscan_scale", obs_module_text("Filter.CameraShake.OverscanScale"),
					1.0, 1.2, 0.005);
	return props;
}

void shake_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "motion_style", ShakeMotionPulseClassic);
	obs_data_set_default_int(settings, "intensity_preset", ShakePresetNormal);
	obs_data_set_default_double(settings, "threshold_bpm", 100.0);
	obs_data_set_default_double(settings, "max_bpm", 180.0);
	obs_data_set_default_double(settings, "max_shake_px", 16.0);
	obs_data_set_default_double(settings, "smoothing", 0.35);
	obs_data_set_default_double(settings, "pulse_decay", 0.28);
	obs_data_set_default_double(settings, "overscan_scale", 1.03);
	obs_data_set_default_bool(settings, "test_mode", false);
	obs_data_set_default_double(settings, "test_bpm", 120.0);
}

void decay_to_rest(HeartbeatCameraShakeFilter *filter)
{
	filter->current_x *= 0.85f;
	filter->current_y *= 0.85f;
	filter->current_scale_delta *= 0.85f;
}

double pulse_at(double phase, double center, double width)
{
	const double normalized = (phase - center) / width;
	return std::exp(-normalized * normalized);
}

double single_heartbeat(double phase, double width_scale)
{
	const double punch = pulse_at(phase, 0.035, 0.040 * width_scale);
	const double settle = pulse_at(phase, 0.145, 0.070 * width_scale);
	return punch - (0.24 * settle);
}

double double_heartbeat(double phase, double width_scale)
{
	const double lub = pulse_at(phase, 0.035, 0.040 * width_scale);
	const double relax = pulse_at(phase, 0.135, 0.065 * width_scale);
	const double dub = pulse_at(phase, 0.255, 0.055 * width_scale);
	return lub - (0.18 * relax) + (0.62 * dub);
}

double current_bpm(const HeartbeatCameraShakeFilter *filter)
{
	auto snapshot = hyperate::heart_rate_state().snapshot();
	if (snapshot.has_sample && snapshot.is_live)
		return snapshot.smoothed_bpm;
	return filter->test_mode ? filter->test_bpm : 0.0;
}

void update_shake_motion(HeartbeatCameraShakeFilter *filter)
{
	const auto now = std::chrono::steady_clock::now();
	const double dt = std::chrono::duration<double>(now - filter->last_frame).count();
	filter->last_frame = now;

	const double bpm = current_bpm(filter);
	if (bpm <= 0.0 || filter->max_shake_px <= 0.0) {
		decay_to_rest(filter);
		return;
	}

	const double intensity = clamp01((bpm - filter->threshold_bpm) / (filter->max_bpm - filter->threshold_bpm));
	const double beat_interval = std::max(0.2, 60.0 / bpm);
	filter->beat_phase += std::max(0.0, dt);

	if (filter->beat_phase >= beat_interval)
		filter->beat_phase = std::fmod(filter->beat_phase, beat_interval);

	const double amplitude = filter->max_shake_px * intensity;
	if (amplitude <= 0.001) {
		decay_to_rest(filter);
		return;
	}

	const double normalized_phase = filter->beat_phase / beat_interval;
	double envelope = std::exp(-normalized_phase / filter->pulse_decay);
	float desired_x = 0.0f;
	float desired_y = 0.0f;
	float desired_scale_delta = 0.0f;
	const double width_scale = std::clamp(filter->pulse_decay / 0.28, 0.65, 1.45);
	const double scale_amount = std::clamp(amplitude / 420.0, 0.0, 0.085);

	switch (filter->motion_style) {
	case ShakeMotionBeatDouble: {
		const double beat = double_heartbeat(normalized_phase, width_scale);
		desired_y = static_cast<float>(-amplitude * 0.055 * beat);
		desired_scale_delta = static_cast<float>(scale_amount * beat);
		break;
	}
	case ShakeMotionBounce: {
		const double down = pulse_at(normalized_phase, 0.035, 0.050 * width_scale);
		const double rebound = pulse_at(normalized_phase, 0.220, 0.085 * width_scale);
		desired_x = 0.0f;
		desired_y = static_cast<float>((-amplitude * 0.52 * down) + (amplitude * 0.24 * rebound));
		desired_scale_delta = static_cast<float>(scale_amount * 0.45 * (down + (0.55 * rebound)));
		break;
	}
	case ShakeMotionShake: {
		envelope = pulse_at(normalized_phase, 0.040, 0.090 * width_scale) +
			   (0.60 * pulse_at(normalized_phase, 0.250, 0.105 * width_scale));
		desired_x = static_cast<float>(amplitude * 0.34 * envelope * std::sin(normalized_phase * 37.6991118));
		desired_y = static_cast<float>(amplitude * 0.22 * envelope * std::cos(normalized_phase * 31.4159265));
		desired_scale_delta = static_cast<float>(scale_amount * 0.35 * envelope);
		break;
	}
	case ShakeMotionPulseClassic: {
		const double beat = single_heartbeat(normalized_phase, width_scale);
		desired_y = static_cast<float>(-amplitude * 0.045 * beat);
		desired_scale_delta = static_cast<float>(scale_amount * beat);
		break;
	}
	default:
		break;
	}

	const float follow = static_cast<float>(1.0 - filter->smoothing);

	filter->current_x += (desired_x - filter->current_x) * follow;
	filter->current_y += (desired_y - filter->current_y) * follow;
	filter->current_scale_delta += (desired_scale_delta - filter->current_scale_delta) * follow;
}

void shake_video_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<HeartbeatCameraShakeFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	if (!target) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	update_shake_motion(filter);

	const uint32_t width = obs_source_get_base_width(target);
	const uint32_t height = obs_source_get_base_height(target);

	// Source filters render their target into an intermediate texture between
	// begin/end. The graphics matrix below transforms that texture, which gives
	// us a native OBS filter effect without editing the scene item transform.
	if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING))
		return;

	gs_matrix_push();
	gs_matrix_translate3f((float)width * 0.5f + filter->current_x,
			      (float)height * 0.5f + filter->current_y, 0.0f);
	const float scale = (float)filter->overscan_scale + filter->current_scale_delta;
	gs_matrix_scale3f(scale, scale, 1.0f);
	gs_matrix_translate3f((float)width * -0.5f, (float)height * -0.5f, 0.0f);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	obs_source_process_filter_end(filter->source, obs_get_base_effect(OBS_EFFECT_DEFAULT), width, height);
	gs_blend_state_pop();

	gs_matrix_pop();
}

} // namespace

obs_source_info heartbeat_camera_shake_filter_info = [] {
	obs_source_info info = {};
	info.id = "hyperate_heartbeat_camera_shake";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = shake_get_name;
	info.create = shake_create;
	info.destroy = shake_destroy;
	info.update = shake_update;
	info.get_defaults = shake_defaults;
	info.get_properties = shake_properties;
	info.video_render = shake_video_render;
	return info;
}();
