#include <obs-module.h>

#include "filters/heartbeat_camera_shake.hpp"
#include "filters/heartbeat_glow.hpp"
#include "sources/bpm_display_source.hpp"
#include "sources/hyperate_input_source.hpp"
#include "sources/threshold_toggle_source.hpp"
#include "util/log.hpp"

#ifdef _WIN32
#include <windows.h>
#include <string>
#include <vector>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-hyperate", "en-US")

const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}

static void register_bundled_font(void)
{
#ifdef _WIN32
	char *path = obs_module_file("GS3 Obviously Wide Bold.ttf");
	if (!path)
		return;
	int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
	if (len > 0) {
		std::vector<wchar_t> wide((size_t)len);
		MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), len);
		AddFontResourceExW(wide.data(), FR_PRIVATE, nullptr);
	}
	bfree(path);
#endif

#ifdef __APPLE__
	char *path = obs_module_file("GS3 Obviously Wide Bold.ttf");
	if (!path)
		return;
	CFStringRef path_str =
		CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
	if (path_str) {
		CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path_str,
							    kCFURLPOSIXPathStyle, false);
		if (url) {
			CTFontManagerRegisterFontsForURL(url, kCTFontManagerScopeProcess, nullptr);
			CFRelease(url);
		}
		CFRelease(path_str);
	}
	bfree(path);
#endif
}

static void unregister_bundled_font(void)
{
#ifdef _WIN32
	char *path = obs_module_file("GS3 Obviously Wide Bold.ttf");
	if (!path)
		return;
	int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
	if (len > 0) {
		std::vector<wchar_t> wide((size_t)len);
		MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), len);
		RemoveFontResourceExW(wide.data(), FR_PRIVATE, nullptr);
	}
	bfree(path);
#endif

#ifdef __APPLE__
	char *path = obs_module_file("GS3 Obviously Wide Bold.ttf");
	if (!path)
		return;
	CFStringRef path_str =
		CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
	if (path_str) {
		CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path_str,
							    kCFURLPOSIXPathStyle, false);
		if (url) {
			CTFontManagerUnregisterFontsForURL(url, kCTFontManagerScopeProcess, nullptr);
			CFRelease(url);
		}
		CFRelease(path_str);
	}
	bfree(path);
#endif
}

bool obs_module_load(void)
{
	register_bundled_font();

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
	unregister_bundled_font();
	HYPERATE_LOG(LOG_INFO, "unloaded");
}
