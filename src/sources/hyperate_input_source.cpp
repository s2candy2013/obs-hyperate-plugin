#include "sources/hyperate_input_source.hpp"

#include "heart_rate/heart_rate_state.hpp"
#include "hyperate/hyperate_client.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <utility>

namespace {

struct HyperateInputSource {
	obs_source_t *source = nullptr;
	std::unique_ptr<hyperate::HyperateClient> client;
	std::string status_note = "Disconnected";
	std::string last_published_status;
	bool checked_legacy_source_name = false;
};

#ifndef HYPERATE_API_TOKEN
#define HYPERATE_API_TOKEN "a7f9d18c4e2b91f0c8d6e5a13b7f9c42e1d8a5f6b3c7d9e0f4a1c8b2d6e9f735"
#endif

// Private-build token compiled into the plugin so streamers do not need to enter one.
// For public releases, pass -DHYPERATE_API_TOKEN=... from CI instead of adding an OBS UI field.
const char *kBundledApiToken = HYPERATE_API_TOKEN;

const char *hyperate_input_get_name(void *)
{
	return obs_module_text("Source.Name");
}

const char *localized_status_name(hyperate::ConnectionStatus status)
{
	switch (status) {
	case hyperate::ConnectionStatus::Connecting:
		return obs_module_text("Source.Status.Connecting");
	case hyperate::ConnectionStatus::Connected:
		return obs_module_text("Source.Status.Connected");
	case hyperate::ConnectionStatus::Error:
		return obs_module_text("Source.Status.Error");
	case hyperate::ConnectionStatus::Disconnected:
	default:
		return obs_module_text("Source.Status.Disconnected");
	}
}

const char *localized_zone_name(hyperate::HeartRateZone zone)
{
	switch (zone) {
	case hyperate::HeartRateZone::Resting:
		return obs_module_text("Source.Zone.Resting");
	case hyperate::HeartRateZone::Active:
		return obs_module_text("Source.Zone.Active");
	case hyperate::HeartRateZone::High:
		return obs_module_text("Source.Zone.High");
	case hyperate::HeartRateZone::Peak:
		return obs_module_text("Source.Zone.Peak");
	case hyperate::HeartRateZone::Unknown:
	default:
		return obs_module_text("Source.Zone.Unknown");
	}
}

bool is_connected_or_connecting(const HyperateInputSource *input)
{
	const auto status = input->client->status();
	return status == hyperate::ConnectionStatus::Connecting || status == hyperate::ConnectionStatus::Connected;
}

void hyperate_input_update(void *data, obs_data_t *settings)
{
	(void)settings;
	auto *input = static_cast<HyperateInputSource *>(data);
	input->status_note = input->client->status() == hyperate::ConnectionStatus::Disconnected ? "Disconnected"
												 : input->status_note;
}

hyperate::HyperateClientConfig config_from_settings(obs_data_t *settings)
{
	const char *channel = obs_data_get_string(settings, "channel_id");
	const int smoothing_percent = static_cast<int>(obs_data_get_int(settings, "smoothing_percent"));
	const double legacy_alpha = obs_data_get_double(settings, "smoothing_alpha");
	const bool has_smoothing_percent = obs_data_has_user_value(settings, "smoothing_percent") ||
					   obs_data_has_default_value(settings, "smoothing_percent");
	const double smoothing_alpha = has_smoothing_percent
					       ? std::clamp(1.0 - (static_cast<double>(smoothing_percent) / 100.0),
							    0.01, 1.0)
					       : legacy_alpha;

	hyperate::HyperateClientConfig config;
	config.channel_id = channel ? channel : "";
	config.api_key = kBundledApiToken;
	config.host = "app.hyperate.io";
	config.path = "/ws";
	config.port = 443;
	config.use_tls = true;
	config.smoothing_alpha = smoothing_alpha;
	return config;
}

void publish_status(HyperateInputSource *input, const std::string &status)
{
	if (input->last_published_status == status)
		return;

	input->last_published_status = status;

	obs_data_t *settings = obs_source_get_settings(input->source);
	obs_data_set_string(settings, "status", status.c_str());
	obs_data_release(settings);
}

bool connection_toggle_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto *input = static_cast<HyperateInputSource *>(data);
	if (is_connected_or_connecting(input)) {
		input->client->stop();
		input->status_note = "Disconnected";
		publish_status(input, input->status_note);
		return true;
	}

	obs_data_t *settings = obs_source_get_settings(input->source);
	auto config = config_from_settings(settings);
	obs_data_release(settings);

	if (config.channel_id.empty()) {
		input->status_note = obs_module_text("Source.Status.EnterId");
		publish_status(input, input->status_note);
		return true;
	}

	hyperate::heart_rate_state().reset_session();
	input->client->start(std::move(config));
	input->status_note = "Connecting";
	publish_status(input, input->status_note);
	return true;
}

void *hyperate_input_create(obs_data_t *settings, obs_source_t *source)
{
	auto *input = new HyperateInputSource;
	input->source = source;
	input->client = std::make_unique<hyperate::HyperateClient>();
	hyperate_input_update(input, settings);
	publish_status(input, input->status_note);
	return input;
}

void hyperate_input_destroy(void *data)
{
	auto *input = static_cast<HyperateInputSource *>(data);
	input->client->stop();
	delete input;
}

void migrate_legacy_source_name(HyperateInputSource *input)
{
	if (!input || !input->source || input->checked_legacy_source_name)
		return;

	input->checked_legacy_source_name = true;
	const char *current_name = obs_source_get_name(input->source);
	if (!current_name || std::strcmp(current_name, "HypeRate Heart Rate Input") != 0)
		return;

	obs_source_t *source = obs_source_get_ref(input->source);
	if (!source)
		return;

	obs_queue_task(
		OBS_TASK_UI,
		[](void *param) {
			auto *queued_source = static_cast<obs_source_t *>(param);
			obs_source_set_name(queued_source, obs_module_text("Source.Name"));
			obs_source_release(queued_source);
		},
		source, false);
}

void hyperate_input_tick(void *data, float)
{
	auto *input = static_cast<HyperateInputSource *>(data);
	migrate_legacy_source_name(input);
	auto snapshot = hyperate::heart_rate_state().snapshot();

	std::ostringstream status;
	const auto connection_status = input->client->status();
	if (connection_status == hyperate::ConnectionStatus::Disconnected) {
		status << input->status_note;
	} else if (connection_status == hyperate::ConnectionStatus::Connected && snapshot.has_sample) {
		status << localized_status_name(connection_status);
	} else {
		status << localized_status_name(connection_status) << " - " << input->client->status_message();
	}

	if (snapshot.has_sample) {
		status << " | " << obs_module_text("Source.Status.Bpm") << ": "
		       << static_cast<int>(snapshot.smoothed_bpm + 0.5) << " | "
		       << obs_module_text("Source.Status.Max") << ": "
		       << static_cast<int>(snapshot.session_max_bpm + 0.5) << " | "
		       << obs_module_text("Source.Status.Zone") << ": " << localized_zone_name(snapshot.zone);
	}

	publish_status(input, status.str());
}

obs_properties_t *hyperate_input_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	auto *input = static_cast<HyperateInputSource *>(data);
	obs_properties_add_text(props, "channel_id", obs_module_text("Source.ChannelId"), OBS_TEXT_DEFAULT);

	obs_property_t *hyperate_link =
		obs_properties_add_button(props, "hyperate_download", obs_module_text("Source.HypeRateDownload"), nullptr);
	obs_property_button_set_type(hyperate_link, OBS_BUTTON_URL);
	obs_property_button_set_url(hyperate_link, const_cast<char *>("https://hyperate.io"));

	obs_properties_add_button(
		props, "connection_toggle",
		input && is_connected_or_connecting(input) ? obs_module_text("Source.Disconnect")
							   : obs_module_text("Source.Connect"),
		connection_toggle_clicked);
	obs_properties_add_text(props, "status", obs_module_text("Source.Status"), OBS_TEXT_INFO);

	obs_property_t *smoothing = obs_properties_add_list(props, "smoothing_percent",
							    obs_module_text("Source.Smoothing"), OBS_COMBO_TYPE_LIST,
							    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(smoothing, obs_module_text("Source.Smoothing.Direct"), 0);
	obs_property_list_add_int(smoothing, obs_module_text("Source.Smoothing.Balanced"), 50);
	obs_property_list_add_int(smoothing, obs_module_text("Source.Smoothing.Smooth"), 75);
	obs_property_list_add_int(smoothing, obs_module_text("Source.Smoothing.VerySmooth"), 90);

	return props;
}

void hyperate_input_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "host", "app.hyperate.io");
	obs_data_set_default_string(settings, "path", "/ws");
	obs_data_set_default_int(settings, "port", 443);
	obs_data_set_default_bool(settings, "use_tls", true);
	obs_data_set_default_double(settings, "smoothing_alpha", 0.25);
	obs_data_set_default_int(settings, "smoothing_percent", 75);
	obs_data_set_default_string(settings, "status", "Disconnected");
}

} // namespace

obs_source_info hyperate_input_source_info = [] {
	obs_source_info info = {};
	info.id = "hyperate_heart_rate_input";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = 0;
	info.get_name = hyperate_input_get_name;
	info.create = hyperate_input_create;
	info.destroy = hyperate_input_destroy;
	info.update = hyperate_input_update;
	info.get_defaults = hyperate_input_defaults;
	info.get_properties = hyperate_input_properties;
	info.video_tick = hyperate_input_tick;
	return info;
}();
