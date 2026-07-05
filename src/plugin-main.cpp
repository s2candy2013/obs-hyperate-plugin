#include <obs-module.h>

#include "filters/heartbeat_camera_shake.hpp"
#include "filters/heartbeat_glow.hpp"
#include "sources/bpm_display_source.hpp"
#include "sources/hyperate_input_source.hpp"
#include "sources/threshold_toggle_source.hpp"
#include "util/log.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-hyperate", "en-US")

const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}

bool obs_module_load(void)
{
	obs_register_source(&hyperate_input_source_info);
	obs_register_source(&bpm_display_source_info);
	obs_register_source(&threshold_toggle_source_info);
	obs_register_source(&heartbeat_camera_shake_filter_info);
	obs_register_source(&heartbeat_glow_filter_info);

	HYPERATE_LOG(LOG_INFO, "loaded");
	return true;
}

void obs_module_unload(void)
{
	HYPERATE_LOG(LOG_INFO, "unloaded");
}
