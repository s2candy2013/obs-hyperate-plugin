#include "sources/threshold_toggle_source.hpp"

#include "heart_rate/heart_rate_state.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <util/platform.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

enum ToggleAction {
	ToggleActionEnableAbove = 0,
	ToggleActionDisableAbove = 1,
	ToggleActionHeartbeatSoundAbove = 2,
	ToggleActionSceneSwitchAbove = 3,
	ToggleActionSourceShowAbove = 4,
	ToggleActionSourceHideAbove = 5,
};

struct ThresholdToggleSource {
	obs_source_t *source = nullptr;
	std::string target_scene;
	std::string target_source;
	std::string target_filter;
	int action = ToggleActionEnableAbove;
	double threshold_bpm = 100.0;
	double hysteresis_bpm = 5.0;
	double sound_volume = 0.35;
	bool use_smoothed = true;
	bool state_initialized = false;
	bool above_threshold = false;
	double sound_gate = 0.0;
	double sound_phase_seconds = 0.0;
	uint64_t last_audio_ts = 0;
	std::vector<float> audio_buffer;
	std::string last_status;
};

struct SourceListContext {
	obs_property_t *property = nullptr;
	obs_source_t *self = nullptr;
};

struct FilterListContext {
	obs_property_t *property = nullptr;
};

struct VisibilityTask {
	std::string target_scene;
	std::string target_source;
	bool visible = true;
};

const char *threshold_toggle_get_name(void *)
{
	return obs_module_text("Source.ThresholdToggle.Name");
}

bool source_list_cb(void *param, obs_source_t *source)
{
	auto *context = static_cast<SourceListContext *>(param);
	if (!context || !context->property || !source || source == context->self)
		return true;

	const char *id = obs_source_get_id(source);
	if (id && std::string(id) == "hyperate_threshold_toggle")
		return true;

	const char *name = obs_source_get_name(source);
	if (name && *name)
		obs_property_list_add_string(context->property, name, name);
	return true;
}

bool scene_list_cb(void *param, obs_source_t *scene)
{
	auto *property = static_cast<obs_property_t *>(param);
	const char *name = obs_source_get_name(scene);
	if (name && *name)
		obs_property_list_add_string(property, name, name);
	return true;
}

void filter_list_cb(obs_source_t *, obs_source_t *filter, void *param)
{
	auto *context = static_cast<FilterListContext *>(param);
	if (!context || !context->property || !filter)
		return;

	const char *name = obs_source_get_name(filter);
	if (name && *name)
		obs_property_list_add_string(context->property, name, name);
}

void populate_sources(obs_property_t *property, obs_source_t *self)
{
	obs_property_list_clear(property);
	obs_property_list_add_string(property, obs_module_text("Source.ThresholdToggle.SelectSource"), "");

	SourceListContext context{property, self};
	obs_enum_scenes(source_list_cb, &context);
	obs_enum_sources(source_list_cb, &context);
}

void populate_scenes(obs_property_t *property)
{
	obs_property_list_clear(property);
	obs_property_list_add_string(property, obs_module_text("Source.ThresholdToggle.SelectAnyScene"), "");
	obs_enum_scenes(scene_list_cb, property);
}

void populate_filters(obs_property_t *property, const char *source_name)
{
	obs_property_list_clear(property);
	obs_property_list_add_string(property, obs_module_text("Source.ThresholdToggle.SelectFilter"), "");

	if (!source_name || !*source_name)
		return;

	obs_source_t *target = obs_get_source_by_name(source_name);
	if (!target)
		return;

	FilterListContext context{property};
	obs_source_enum_filters(target, filter_list_cb, &context);
	obs_source_release(target);
}

void threshold_toggle_update(void *data, obs_data_t *settings)
{
	auto *toggle = static_cast<ThresholdToggleSource *>(data);
	const char *target_scene = obs_data_get_string(settings, "target_scene");
	const char *target_source = obs_data_get_string(settings, "target_source");
	const char *target_filter = obs_data_get_string(settings, "target_filter");
	toggle->target_scene = target_scene ? target_scene : "";
	toggle->target_source = target_source ? target_source : "";
	toggle->target_filter = target_filter ? target_filter : "";
	toggle->action = (int)obs_data_get_int(settings, "action");
	toggle->threshold_bpm = std::clamp(obs_data_get_double(settings, "threshold_bpm"), 1.0, 300.0);
	toggle->hysteresis_bpm = std::clamp(obs_data_get_double(settings, "hysteresis_bpm"), 0.0, 100.0);
	toggle->sound_volume = std::clamp(obs_data_get_double(settings, "sound_volume"), 0.0, 1.0);
	toggle->use_smoothed = obs_data_get_bool(settings, "use_smoothed");
	if (toggle->action != ToggleActionHeartbeatSoundAbove) {
		toggle->last_audio_ts = 0;
		toggle->sound_phase_seconds = 0.0;
		toggle->sound_gate = 0.0;
	}
	toggle->state_initialized = false;
}

void *threshold_toggle_create(obs_data_t *settings, obs_source_t *source)
{
	auto *toggle = new ThresholdToggleSource;
	toggle->source = source;
	threshold_toggle_update(toggle, settings);
	return toggle;
}

void threshold_toggle_destroy(void *data)
{
	delete static_cast<ThresholdToggleSource *>(data);
}

std::string status_text(const ThresholdToggleSource *toggle, const char *message)
{
	std::ostringstream status;
	status << message;
	if (!toggle->target_source.empty())
		status << " | " << toggle->target_source;
	if (!toggle->target_filter.empty())
		status << " -> " << toggle->target_filter;
	return status.str();
}

void publish_status(ThresholdToggleSource *toggle, const std::string &status)
{
	if (!toggle || !toggle->source || toggle->last_status == status)
		return;

	toggle->last_status = status;
	obs_data_t *settings = obs_source_get_settings(toggle->source);
	obs_data_set_string(settings, "status", status.c_str());
	obs_data_release(settings);
}

bool desired_filter_enabled(const ThresholdToggleSource *toggle, bool above)
{
	if (toggle->action == ToggleActionDisableAbove)
		return !above;
	return above;
}

bool is_filter_action(const ThresholdToggleSource *toggle)
{
	return toggle->action == ToggleActionEnableAbove || toggle->action == ToggleActionDisableAbove;
}

bool is_scene_action(const ThresholdToggleSource *toggle)
{
	return toggle->action == ToggleActionSceneSwitchAbove;
}

bool is_source_visibility_action(const ThresholdToggleSource *toggle)
{
	return toggle->action == ToggleActionSourceShowAbove || toggle->action == ToggleActionSourceHideAbove;
}

bool desired_source_visible(const ThresholdToggleSource *toggle, bool above)
{
	if (toggle->action == ToggleActionSourceHideAbove)
		return !above;
	return above;
}

double effective_reset_drop_bpm(const ThresholdToggleSource *toggle)
{
	// A tiny mandatory reset gap prevents rapid on/off flicker when BPM hovers
	// exactly around the trigger value. Streamers can still make it wider.
	return std::max(toggle->hysteresis_bpm, 1.0);
}

void apply_target_filter_state(ThresholdToggleSource *toggle, bool above)
{
	if (!is_filter_action(toggle))
		return;

	if (toggle->target_source.empty() || toggle->target_filter.empty()) {
		publish_status(toggle, obs_module_text("Source.ThresholdToggle.Status.SelectTarget"));
		return;
	}

	obs_source_t *target = obs_get_source_by_name(toggle->target_source.c_str());
	if (!target) {
		publish_status(toggle, status_text(toggle, obs_module_text("Source.ThresholdToggle.Status.SourceMissing")));
		return;
	}

	obs_source_t *filter = obs_source_get_filter_by_name(target, toggle->target_filter.c_str());
	if (!filter) {
		obs_source_release(target);
		publish_status(toggle, status_text(toggle, obs_module_text("Source.ThresholdToggle.Status.FilterMissing")));
		return;
	}

	const bool enabled = desired_filter_enabled(toggle, above);
	if (obs_source_enabled(filter) != enabled)
		obs_source_set_enabled(filter, enabled);

	obs_source_release(filter);
	obs_source_release(target);

	std::ostringstream status;
	status << (above ? obs_module_text("Source.ThresholdToggle.Status.Above")
			 : obs_module_text("Source.ThresholdToggle.Status.Below"))
	       << " | " << obs_module_text(enabled ? "Source.ThresholdToggle.Status.FilterOn"
						   : "Source.ThresholdToggle.Status.FilterOff");
	publish_status(toggle, status.str());
}

using frontend_set_current_scene_t = void (*)(obs_source_t *);

frontend_set_current_scene_t resolve_frontend_set_current_scene()
{
#ifdef _WIN32
	HMODULE module = GetModuleHandleA("obs-frontend-api.dll");
	return module ? reinterpret_cast<frontend_set_current_scene_t>(GetProcAddress(module, "obs_frontend_set_current_scene"))
		      : nullptr;
#else
	return reinterpret_cast<frontend_set_current_scene_t>(dlsym(RTLD_DEFAULT, "obs_frontend_set_current_scene"));
#endif
}

void apply_scene_switch(ThresholdToggleSource *toggle, bool above)
{
	if (!above) {
		publish_status(toggle, obs_module_text("Source.ThresholdToggle.Status.Below"));
		return;
	}

	if (toggle->target_scene.empty()) {
		publish_status(toggle, obs_module_text("Source.ThresholdToggle.Status.SelectScene"));
		return;
	}

	obs_source_t *scene = obs_get_source_by_name(toggle->target_scene.c_str());
	if (!scene || !obs_scene_from_source(scene)) {
		if (scene)
			obs_source_release(scene);
		publish_status(toggle, obs_module_text("Source.ThresholdToggle.Status.SceneMissing"));
		return;
	}

	auto set_current_scene = resolve_frontend_set_current_scene();
	if (!set_current_scene) {
		obs_source_release(scene);
		publish_status(toggle, obs_module_text("Source.ThresholdToggle.Status.FrontendMissing"));
		return;
	}

	obs_queue_task(
		OBS_TASK_UI,
		[](void *param) {
			auto *queued_scene = static_cast<obs_source_t *>(param);
			if (auto set_current_scene = resolve_frontend_set_current_scene())
				set_current_scene(queued_scene);
			obs_source_release(queued_scene);
		},
		scene, false);

	std::ostringstream status;
	status << obs_module_text("Source.ThresholdToggle.Status.Above") << " | "
	       << obs_module_text("Source.ThresholdToggle.Status.SceneSwitched");
	publish_status(toggle, status.str());
}

void set_scene_item_visible(obs_source_t *scene_source, const char *target_source, bool visible)
{
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		return;

	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, target_source);
	if (item)
		obs_sceneitem_set_visible(item, visible);
}

bool set_visibility_all_scenes_cb(void *param, obs_source_t *scene)
{
	auto *task = static_cast<VisibilityTask *>(param);
	set_scene_item_visible(scene, task->target_source.c_str(), task->visible);
	return true;
}

void apply_source_visibility(ThresholdToggleSource *toggle, bool above)
{
	if (toggle->target_source.empty()) {
		publish_status(toggle, obs_module_text("Source.ThresholdToggle.Status.SelectSource"));
		return;
	}

	const bool visible = desired_source_visible(toggle, above);
	auto *task = new VisibilityTask;
	task->target_scene = toggle->target_scene;
	task->target_source = toggle->target_source;
	task->visible = visible;

	obs_queue_task(
		OBS_TASK_UI,
		[](void *param) {
			auto *visibility_task = static_cast<VisibilityTask *>(param);
			if (!visibility_task->target_scene.empty()) {
				obs_source_t *scene = obs_get_source_by_name(visibility_task->target_scene.c_str());
				if (scene) {
					set_scene_item_visible(scene, visibility_task->target_source.c_str(),
							       visibility_task->visible);
					obs_source_release(scene);
				}
			} else {
				obs_enum_scenes(set_visibility_all_scenes_cb, visibility_task);
			}
			delete visibility_task;
		},
		task, false);

	std::ostringstream status;
	status << (above ? obs_module_text("Source.ThresholdToggle.Status.Above")
			 : obs_module_text("Source.ThresholdToggle.Status.Below"))
	       << " | " << obs_module_text(visible ? "Source.ThresholdToggle.Status.SourceShown"
						    : "Source.ThresholdToggle.Status.SourceHidden");
	publish_status(toggle, status.str());
}

double smooth_unit(double value)
{
	value = std::clamp(value, 0.0, 1.0);
	return value * value * (3.0 - 2.0 * value);
}

float heartbeat_thump_sample(double local_time, double duration, double volume, double low_frequency,
			     double high_frequency, double decay, double click_decay, double gain)
{
	constexpr double pi = 3.14159265358979323846;
	if (local_time < 0.0 || local_time >= duration)
		return 0.0f;

	const double low = std::sin(2.0 * pi * low_frequency * local_time) * std::exp(-local_time / decay);
	const double click = std::sin(2.0 * pi * high_frequency * local_time) * std::exp(-local_time / click_decay);
	const double soft_attack = smooth_unit(local_time / 0.007);
	const double release_seconds = std::min(0.03, duration * 0.35);
	const double soft_release = smooth_unit((duration - local_time) / release_seconds);
	return (float)((low * 0.82 + click * 0.18) * soft_attack * soft_release * volume * gain);
}

float heartbeat_sample(double beat_time, double beat_interval, double volume)
{
	const double dub_offset = std::min(0.18, beat_interval * 0.34);
	const double lub_duration = std::clamp(dub_offset - 0.012, 0.075, 0.145);
	const double dub_duration = std::min(0.14, std::max(0.045, beat_interval - dub_offset - 0.025));
	const float lub = heartbeat_thump_sample(beat_time, lub_duration, volume, 52.0, 92.0, 0.062, 0.032, 0.92);
	const float dub =
		heartbeat_thump_sample(beat_time - dub_offset, dub_duration, volume, 64.0, 132.0, 0.046, 0.023, 0.62);
	return (float)std::clamp((double)lub + (double)dub, -1.0, 1.0);
}

void reset_heartbeat_audio(ThresholdToggleSource *toggle)
{
	toggle->last_audio_ts = 0;
	toggle->sound_phase_seconds = 0.0;
	toggle->sound_gate = 0.0;
}

void output_heartbeat_audio(ThresholdToggleSource *toggle, const hyperate::HeartRateSnapshot &snapshot, bool active)
{
	if (toggle->action != ToggleActionHeartbeatSoundAbove || !snapshot.has_sample || !snapshot.is_live) {
		reset_heartbeat_audio(toggle);
		return;
	}

	if (!active && toggle->sound_gate <= 0.0001) {
		reset_heartbeat_audio(toggle);
		return;
	}

	obs_audio_info audio_info = {};
	if (!obs_get_audio_info(&audio_info) || audio_info.samples_per_sec == 0)
		return;

	const double bpm = std::max(1.0, toggle->use_smoothed ? snapshot.smoothed_bpm : snapshot.raw_bpm);
	const double beat_interval = std::max(0.2, 60.0 / bpm);
	const uint32_t sample_rate = audio_info.samples_per_sec;
	const uint64_t now = os_gettime_ns();
	constexpr uint64_t nsec_per_sec = 1000000000ULL;

	if (toggle->last_audio_ts == 0 || now <= toggle->last_audio_ts || now - toggle->last_audio_ts > 500000000ULL)
		toggle->last_audio_ts = now - 20000000ULL;

	const uint64_t elapsed_ns = now - toggle->last_audio_ts;
	const uint64_t elapsed_frames = (elapsed_ns * (uint64_t)sample_rate) / nsec_per_sec;
	uint32_t frames = (uint32_t)std::clamp<uint64_t>(elapsed_frames, 1ULL, 4096ULL);
	toggle->audio_buffer.assign(frames, 0.0f);

	const double target_gate = active ? 1.0 : 0.0;
	const double gate_seconds = active ? 0.055 : 0.11;
	const double gate_step = 1.0 / (gate_seconds * (double)sample_rate);

	for (uint32_t i = 0; i < frames; ++i) {
		if (toggle->sound_gate < target_gate)
			toggle->sound_gate = std::min(target_gate, toggle->sound_gate + gate_step);
		else if (toggle->sound_gate > target_gate)
			toggle->sound_gate = std::max(target_gate, toggle->sound_gate - gate_step);

		toggle->audio_buffer[i] =
			heartbeat_sample(toggle->sound_phase_seconds, beat_interval, toggle->sound_volume * toggle->sound_gate);
		toggle->sound_phase_seconds += 1.0 / (double)sample_rate;
		if (toggle->sound_phase_seconds >= beat_interval)
			toggle->sound_phase_seconds = std::fmod(toggle->sound_phase_seconds, beat_interval);
	}

	obs_source_audio audio = {};
	audio.data[0] = reinterpret_cast<const uint8_t *>(toggle->audio_buffer.data());
	audio.frames = frames;
	audio.speakers = SPEAKERS_MONO;
	audio.format = AUDIO_FORMAT_FLOAT;
	audio.samples_per_sec = sample_rate;
	audio.timestamp = toggle->last_audio_ts;
	obs_source_output_audio(toggle->source, &audio);

	toggle->last_audio_ts += ((uint64_t)frames * nsec_per_sec) / sample_rate;

	if (!active && toggle->sound_gate <= 0.0001)
		reset_heartbeat_audio(toggle);
}

void publish_sound_status(ThresholdToggleSource *toggle, bool active)
{
	std::ostringstream status;
	status << (active ? obs_module_text("Source.ThresholdToggle.Status.Above")
			  : obs_module_text("Source.ThresholdToggle.Status.Below"))
	       << " | " << obs_module_text(active ? "Source.ThresholdToggle.Status.SoundOn"
						  : "Source.ThresholdToggle.Status.SoundOff");
	publish_status(toggle, status.str());
}

void threshold_toggle_tick(void *data, float)
{
	auto *toggle = static_cast<ThresholdToggleSource *>(data);
	const auto snapshot = hyperate::heart_rate_state().snapshot();
	if (!snapshot.has_sample || !snapshot.is_live) {
		publish_status(toggle, obs_module_text("Source.ThresholdToggle.Status.Waiting"));
		toggle->state_initialized = false;
		reset_heartbeat_audio(toggle);
		return;
	}

	const double bpm = toggle->use_smoothed ? snapshot.smoothed_bpm : snapshot.raw_bpm;
	bool next_above = toggle->above_threshold;
	if (!toggle->state_initialized) {
		next_above = bpm >= toggle->threshold_bpm;
		toggle->state_initialized = true;
	} else if (!toggle->above_threshold && bpm >= toggle->threshold_bpm) {
		next_above = true;
	} else if (toggle->above_threshold &&
		   bpm <= toggle->threshold_bpm - effective_reset_drop_bpm(toggle)) {
		next_above = false;
	}

	if (next_above != toggle->above_threshold || toggle->last_status.empty()) {
		toggle->above_threshold = next_above;
		if (is_filter_action(toggle))
			apply_target_filter_state(toggle, next_above);
		else if (is_scene_action(toggle))
			apply_scene_switch(toggle, next_above);
		else if (is_source_visibility_action(toggle))
			apply_source_visibility(toggle, next_above);
		else
			publish_sound_status(toggle, next_above);
	}

	output_heartbeat_audio(toggle, snapshot, toggle->above_threshold);
}

bool target_source_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_t *filters = obs_properties_get(props, "target_filter");
	populate_filters(filters, obs_data_get_string(settings, "target_source"));
	return true;
}

bool refresh_targets_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
	auto *toggle = static_cast<ThresholdToggleSource *>(data);
	obs_property_t *scenes = obs_properties_get(props, "target_scene");
	obs_property_t *sources = obs_properties_get(props, "target_source");
	obs_property_t *filters = obs_properties_get(props, "target_filter");
	populate_scenes(scenes);
	populate_sources(sources, toggle ? toggle->source : nullptr);

	obs_data_t *settings = toggle && toggle->source ? obs_source_get_settings(toggle->source) : nullptr;
	populate_filters(filters, settings ? obs_data_get_string(settings, "target_source") : "");
	if (settings)
		obs_data_release(settings);
	return true;
}

void update_action_property_visibility(obs_properties_t *props, int action)
{
	const bool filter_action = action == ToggleActionEnableAbove || action == ToggleActionDisableAbove;
	const bool scene_action = action == ToggleActionSceneSwitchAbove;
	const bool visibility_action = action == ToggleActionSourceShowAbove || action == ToggleActionSourceHideAbove;
	if (obs_property_t *property = obs_properties_get(props, "target_source"))
		obs_property_set_visible(property, filter_action || visibility_action);
	if (obs_property_t *property = obs_properties_get(props, "target_filter"))
		obs_property_set_visible(property, filter_action);
	if (obs_property_t *property = obs_properties_get(props, "target_scene"))
		obs_property_set_visible(property, scene_action || visibility_action);
	if (obs_property_t *property = obs_properties_get(props, "refresh_targets"))
		obs_property_set_visible(property, filter_action || scene_action || visibility_action);
	if (obs_property_t *property = obs_properties_get(props, "sound_volume"))
		obs_property_set_visible(property, action == ToggleActionHeartbeatSoundAbove);
}

bool action_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	update_action_property_visibility(props, (int)obs_data_get_int(settings, "action"));
	return true;
}

obs_properties_t *threshold_toggle_properties(void *data)
{
	auto *toggle = static_cast<ThresholdToggleSource *>(data);
	obs_properties_t *props = obs_properties_create();

	obs_property_t *action = obs_properties_add_list(props, "action", obs_module_text("Source.ThresholdToggle.Action"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(action, obs_module_text("Source.ThresholdToggle.Action.EnableAbove"),
				  ToggleActionEnableAbove);
	obs_property_list_add_int(action, obs_module_text("Source.ThresholdToggle.Action.DisableAbove"),
				  ToggleActionDisableAbove);
	obs_property_list_add_int(action, obs_module_text("Source.ThresholdToggle.Action.HeartbeatSoundAbove"),
				  ToggleActionHeartbeatSoundAbove);
	obs_property_list_add_int(action, obs_module_text("Source.ThresholdToggle.Action.SceneSwitchAbove"),
				  ToggleActionSceneSwitchAbove);
	obs_property_list_add_int(action, obs_module_text("Source.ThresholdToggle.Action.SourceShowAbove"),
				  ToggleActionSourceShowAbove);
	obs_property_list_add_int(action, obs_module_text("Source.ThresholdToggle.Action.SourceHideAbove"),
				  ToggleActionSourceHideAbove);
	obs_property_set_modified_callback(action, action_changed);

	obs_property_t *scenes = obs_properties_add_list(props, "target_scene",
							 obs_module_text("Source.ThresholdToggle.TargetScene"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	populate_scenes(scenes);

	obs_property_t *sources = obs_properties_add_list(props, "target_source",
							   obs_module_text("Source.ThresholdToggle.TargetSource"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	populate_sources(sources, toggle ? toggle->source : nullptr);
	obs_property_set_modified_callback(sources, target_source_changed);

	const char *selected_source = "";
	if (toggle)
		selected_source = toggle->target_source.c_str();

	obs_property_t *filters = obs_properties_add_list(props, "target_filter",
							   obs_module_text("Source.ThresholdToggle.TargetFilter"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	populate_filters(filters, selected_source);

	obs_properties_add_float_slider(props, "threshold_bpm", obs_module_text("Source.ThresholdToggle.ThresholdBpm"),
					40.0, 220.0, 1.0);
	obs_properties_add_float_slider(props, "hysteresis_bpm",
					obs_module_text("Source.ThresholdToggle.HysteresisBpm"), 0.0, 40.0, 1.0);
	obs_properties_add_float_slider(props, "sound_volume", obs_module_text("Source.ThresholdToggle.SoundVolume"), 0.0,
					1.0, 0.01);
	obs_properties_add_bool(props, "use_smoothed", obs_module_text("Source.ThresholdToggle.UseSmoothed"));
	obs_properties_add_button(props, "refresh_targets", obs_module_text("Source.ThresholdToggle.Refresh"),
				  refresh_targets_clicked);
	obs_properties_add_text(props, "status", obs_module_text("Source.ThresholdToggle.Status"), OBS_TEXT_INFO);

	update_action_property_visibility(props, toggle ? toggle->action : ToggleActionEnableAbove);
	return props;
}

void threshold_toggle_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "action", ToggleActionEnableAbove);
	obs_data_set_default_double(settings, "threshold_bpm", 100.0);
	obs_data_set_default_double(settings, "hysteresis_bpm", 5.0);
	obs_data_set_default_double(settings, "sound_volume", 0.35);
	obs_data_set_default_bool(settings, "use_smoothed", true);
	obs_data_set_default_string(settings, "status", "Waiting for live BPM");
}

} // namespace

obs_source_info threshold_toggle_source_info = [] {
	obs_source_info info = {};
	info.id = "hyperate_threshold_toggle";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = threshold_toggle_get_name;
	info.create = threshold_toggle_create;
	info.destroy = threshold_toggle_destroy;
	info.update = threshold_toggle_update;
	info.get_defaults = threshold_toggle_defaults;
	info.get_properties = threshold_toggle_properties;
	info.video_tick = threshold_toggle_tick;
	return info;
}();
