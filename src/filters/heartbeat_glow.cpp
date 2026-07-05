#include "filters/heartbeat_glow.hpp"

#include "heart_rate/heart_rate_state.hpp"
#include "util/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

enum GlowPreset {
	GlowPresetSubtle = 0,
	GlowPresetNormal = 1,
	GlowPresetIntense = 2,
	GlowPresetCustom = 3,
};

enum GlowPulseStyle {
	GlowPulseClassic = 0,
	GlowPulseDoubleBeat = 1,
	GlowPulseSoft = 2,
};

struct HeartbeatGlowFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *brightness_param = nullptr;
	gs_eparam_t *opacity_param = nullptr;
	gs_eparam_t *tint_param = nullptr;

	int intensity_preset = GlowPresetNormal;
	int pulse_style = GlowPulseClassic;
	double threshold_bpm = 95.0;
	double max_bpm = 175.0;
	double glow_opacity = 0.30;
	double source_boost = 0.16;
	double glow_scale = 1.035;
	double pulse_decay = 0.30;
	uint32_t tint_color = 0x00FFF2DC;
	double tint_strength = 0.0;
	bool test_mode = false;
	double test_bpm = 120.0;

	double beat_phase = 0.0;
	std::chrono::steady_clock::time_point last_frame = std::chrono::steady_clock::now();
};

double clamp01(double value)
{
	return std::clamp(value, 0.0, 1.0);
}

void apply_preset(HeartbeatGlowFilter *filter)
{
	switch (filter->intensity_preset) {
	case GlowPresetSubtle:
		filter->threshold_bpm = 100.0;
		filter->max_bpm = 180.0;
		filter->glow_opacity = 0.18;
		filter->source_boost = 0.08;
		filter->glow_scale = 1.02;
		filter->pulse_decay = 0.24;
		filter->tint_strength = 0.0;
		break;
	case GlowPresetIntense:
		filter->threshold_bpm = 90.0;
		filter->max_bpm = 165.0;
		filter->glow_opacity = 0.58;
		filter->source_boost = 0.30;
		filter->glow_scale = 1.065;
		filter->pulse_decay = 0.36;
		filter->tint_strength = 0.0;
		break;
	case GlowPresetNormal:
		filter->threshold_bpm = 95.0;
		filter->max_bpm = 175.0;
		filter->glow_opacity = 0.32;
		filter->source_boost = 0.16;
		filter->glow_scale = 1.035;
		filter->pulse_decay = 0.30;
		filter->tint_strength = 0.0;
		break;
	case GlowPresetCustom:
	default:
		break;
	}
}

const char *glow_get_name(void *)
{
	return obs_module_text("Filter.Glow.Name");
}

void set_glow_params(HeartbeatGlowFilter *filter, float brightness, float opacity, float tint_strength)
{
	if (filter->brightness_param)
		gs_effect_set_float(filter->brightness_param, brightness);
	if (filter->opacity_param)
		gs_effect_set_float(filter->opacity_param, opacity);
	if (filter->tint_param) {
		vec4 tint;
		vec4_from_rgba(&tint, filter->tint_color);
		tint.w = tint_strength;
		gs_effect_set_vec4(filter->tint_param, &tint);
	}
}

void glow_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<HeartbeatGlowFilter *>(data);
	filter->intensity_preset = static_cast<int>(obs_data_get_int(settings, "intensity_preset"));
	filter->pulse_style = static_cast<int>(obs_data_get_int(settings, "pulse_style"));
	filter->threshold_bpm = obs_data_get_double(settings, "threshold_bpm");
	filter->max_bpm = std::max(filter->threshold_bpm + 1.0, obs_data_get_double(settings, "max_bpm"));
	filter->glow_opacity = clamp01(obs_data_get_double(settings, "glow_opacity"));
	filter->source_boost = std::clamp(obs_data_get_double(settings, "source_boost"), 0.0, 1.5);
	filter->glow_scale = std::clamp(obs_data_get_double(settings, "glow_scale"), 1.0, 1.8);
	filter->pulse_decay = std::max(0.02, obs_data_get_double(settings, "pulse_decay"));
	filter->tint_color = (uint32_t)obs_data_get_int(settings, "tint_color");
	filter->tint_strength = clamp01(obs_data_get_double(settings, "tint_strength"));
	filter->test_mode = obs_data_get_bool(settings, "test_mode");
	filter->test_bpm = obs_data_get_double(settings, "test_bpm");
	apply_preset(filter);
}

void *glow_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new HeartbeatGlowFilter;
	filter->source = source;

	char *effect_path = obs_module_file("effects/heartbeat_glow.effect");
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, nullptr);
	obs_leave_graphics();
	bfree(effect_path);

	if (!filter->effect)
		HYPERATE_LOG(LOG_ERROR, "Could not load heartbeat glow effect");
	else {
		filter->brightness_param = gs_effect_get_param_by_name(filter->effect, "brightness");
		filter->opacity_param = gs_effect_get_param_by_name(filter->effect, "opacity");
		filter->tint_param = gs_effect_get_param_by_name(filter->effect, "tint");
	}

	glow_update(filter, settings);
	return filter;
}

void glow_destroy(void *data)
{
	auto *filter = static_cast<HeartbeatGlowFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

bool update_property_visibility(obs_properties_t *props, obs_data_t *settings)
{
	const bool custom = obs_data_get_int(settings, "intensity_preset") == GlowPresetCustom;
	const bool test_mode = obs_data_get_bool(settings, "test_mode");

	for (const char *name : {"threshold_bpm", "max_bpm", "glow_opacity", "source_boost", "glow_scale",
				 "pulse_decay", "tint_color", "tint_strength"}) {
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

obs_properties_t *glow_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *preset = obs_properties_add_list(props, "intensity_preset",
							  obs_module_text("Filter.Glow.Preset"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(preset, obs_module_text("Filter.Glow.Preset.Subtle"), GlowPresetSubtle);
	obs_property_list_add_int(preset, obs_module_text("Filter.Glow.Preset.Normal"), GlowPresetNormal);
	obs_property_list_add_int(preset, obs_module_text("Filter.Glow.Preset.Intense"), GlowPresetIntense);
	obs_property_list_add_int(preset, obs_module_text("Filter.Glow.Preset.Custom"), GlowPresetCustom);
	obs_property_set_modified_callback(preset, preset_changed);

	obs_property_t *pulse_style = obs_properties_add_list(props, "pulse_style",
							      obs_module_text("Filter.Glow.PulseStyle"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(pulse_style, obs_module_text("Filter.Glow.PulseStyle.Classic"),
				  GlowPulseClassic);
	obs_property_list_add_int(pulse_style, obs_module_text("Filter.Glow.PulseStyle.DoubleBeat"),
				  GlowPulseDoubleBeat);
	obs_property_list_add_int(pulse_style, obs_module_text("Filter.Glow.PulseStyle.Soft"), GlowPulseSoft);

	obs_property_t *test_mode = obs_properties_add_bool(props, "test_mode", obs_module_text("Filter.Glow.TestMode"));
	obs_property_set_modified_callback(test_mode, test_mode_changed);
	obs_properties_add_float_slider(props, "test_bpm", obs_module_text("Filter.Glow.TestBpm"), 40.0, 220.0,
					1.0);

	obs_properties_add_float_slider(props, "threshold_bpm", obs_module_text("Filter.Glow.ThresholdBpm"), 40.0,
					220.0, 1.0);
	obs_properties_add_float_slider(props, "max_bpm", obs_module_text("Filter.Glow.MaxBpm"), 80.0, 240.0,
					1.0);
	obs_properties_add_float_slider(props, "glow_opacity", obs_module_text("Filter.Glow.Opacity"), 0.0, 1.0,
					0.01);
	obs_properties_add_float_slider(props, "source_boost", obs_module_text("Filter.Glow.SourceBoost"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(props, "glow_scale", obs_module_text("Filter.Glow.Scale"), 1.0, 1.6,
					0.01);
	obs_properties_add_float_slider(props, "pulse_decay", obs_module_text("Filter.Glow.PulseDecay"), 0.02,
					1.0, 0.01);
	obs_properties_add_color(props, "tint_color", obs_module_text("Filter.Glow.Color"));
	obs_properties_add_float_slider(props, "tint_strength", obs_module_text("Filter.Glow.TintStrength"), 0.0,
					1.0, 0.01);
	return props;
}

void glow_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "intensity_preset", GlowPresetNormal);
	obs_data_set_default_int(settings, "pulse_style", GlowPulseClassic);
	obs_data_set_default_double(settings, "threshold_bpm", 95.0);
	obs_data_set_default_double(settings, "max_bpm", 175.0);
	obs_data_set_default_double(settings, "glow_opacity", 0.32);
	obs_data_set_default_double(settings, "source_boost", 0.16);
	obs_data_set_default_double(settings, "glow_scale", 1.035);
	obs_data_set_default_double(settings, "pulse_decay", 0.30);
	obs_data_set_default_int(settings, "tint_color", 0x00FFF2DC);
	obs_data_set_default_double(settings, "tint_strength", 0.0);
	obs_data_set_default_bool(settings, "test_mode", false);
	obs_data_set_default_double(settings, "test_bpm", 120.0);
}

double current_bpm(const HeartbeatGlowFilter *filter)
{
	auto snapshot = hyperate::heart_rate_state().snapshot();
	if (snapshot.has_sample && snapshot.is_live)
		return snapshot.smoothed_bpm;
	return filter->test_mode ? filter->test_bpm : 0.0;
}

double pulse_at(double phase, double center, double width)
{
	const double normalized = (phase - center) / width;
	return std::exp(-normalized * normalized);
}

double pulse_envelope(const HeartbeatGlowFilter *filter, double phase)
{
	constexpr double pi = 3.14159265358979323846;
	const double width_scale = std::clamp(filter->pulse_decay / 0.30, 0.65, 1.55);

	switch (filter->pulse_style) {
	case GlowPulseDoubleBeat: {
		const double lub = pulse_at(phase, 0.035, 0.045 * width_scale);
		const double relax = pulse_at(phase, 0.145, 0.075 * width_scale);
		const double dub = pulse_at(phase, 0.265, 0.065 * width_scale);
		return std::clamp(lub - (0.15 * relax) + (0.58 * dub), 0.0, 1.25);
	}
	case GlowPulseSoft: {
		const double smooth = 0.5 * (1.0 + std::cos(std::clamp(phase, 0.0, 1.0) * pi));
		return std::pow(std::clamp(smooth, 0.0, 1.0), std::max(0.35, 1.0 / width_scale));
	}
	case GlowPulseClassic:
	default:
		return std::exp(-phase / filter->pulse_decay);
	}
}

double update_pulse(HeartbeatGlowFilter *filter)
{
	const auto now = std::chrono::steady_clock::now();
	const double dt = std::chrono::duration<double>(now - filter->last_frame).count();
	filter->last_frame = now;

	const double bpm = current_bpm(filter);
	if (bpm <= 0.0)
		return 0.0;

	const double intensity = clamp01((bpm - filter->threshold_bpm) / (filter->max_bpm - filter->threshold_bpm));
	const double beat_interval = std::max(0.2, 60.0 / bpm);
	filter->beat_phase += std::max(0.0, dt);

	if (filter->beat_phase >= beat_interval)
		filter->beat_phase = std::fmod(filter->beat_phase, beat_interval);

	const double normalized_phase = filter->beat_phase / beat_interval;
	const double envelope = pulse_envelope(filter, normalized_phase);
	return intensity * envelope;
}

void draw_filtered_texture(HeartbeatGlowFilter *filter, uint32_t width, uint32_t height, float scale)
{
	gs_matrix_push();
	gs_matrix_translate3f((float)width * 0.5f, (float)height * 0.5f, 0.0f);
	gs_matrix_scale3f(scale, scale, 1.0f);
	gs_matrix_translate3f((float)width * -0.5f, (float)height * -0.5f, 0.0f);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
	gs_matrix_pop();
}

void glow_video_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<HeartbeatGlowFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	if (!target || !filter->effect) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const uint32_t width = obs_source_get_base_width(target);
	const uint32_t height = obs_source_get_base_height(target);
	const double pulse = update_pulse(filter);

	if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING))
		return;

	if (pulse > 0.001 && filter->glow_opacity > 0.0) {
		set_glow_params(filter, (float)(1.0 + pulse * 0.85), (float)(pulse * filter->glow_opacity),
				(float)filter->tint_strength);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ONE);
		draw_filtered_texture(filter, width, height, (float)(1.0 + ((filter->glow_scale - 1.0) * pulse)));
		gs_blend_state_pop();
	}

	set_glow_params(filter, (float)(1.0 + pulse * filter->source_boost), 1.0f, 0.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	draw_filtered_texture(filter, width, height, 1.0f);
	gs_blend_state_pop();
}

} // namespace

obs_source_info heartbeat_glow_filter_info = [] {
	obs_source_info info = {};
	info.id = "hyperate_heartbeat_glow";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = glow_get_name;
	info.create = glow_create;
	info.destroy = glow_destroy;
	info.update = glow_update;
	info.get_defaults = glow_defaults;
	info.get_properties = glow_properties;
	info.video_render = glow_video_render;
	return info;
}();
