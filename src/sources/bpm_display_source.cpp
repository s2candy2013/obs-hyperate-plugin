#include "sources/bpm_display_source.hpp"

#include "heart_rate/heart_rate_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif

namespace {

struct BpmDisplaySource {
	obs_source_t *source = nullptr;
	uint32_t width = 360;
	uint32_t height = 160;
	uint32_t color = 0xFFFFFFFF;
	uint32_t resting_color = 0xFF38D878;
	uint32_t active_color = 0xFF47A8FF;
	uint32_t high_color = 0xFF00D7FF;
	uint32_t peak_color = 0xFF3B3BFF;
	int render_mode = 0;
	int digit_style = 0;
	std::string font_face = "GS3 Obviously";
	int font_size = 96;
	int font_flags = OBS_FONT_BOLD;
	bool use_zone_colors = false;
	bool use_smoothed = true;
	bool show_when_disconnected = true;
	bool glow_enabled = false;
	double glow_softness = 6.0;
	double active_bpm = 100.0;
	double high_bpm = 130.0;
	double peak_bpm = 160.0;
	bool displayed_bpm_initialized = false;
	double displayed_bpm = 0.0;
	std::chrono::steady_clock::time_point last_animation_frame = std::chrono::steady_clock::now();
	gs_texture_t *text_texture = nullptr;
	std::string cached_text;
	uint32_t cached_color = 0;
	uint32_t cached_width = 0;
	uint32_t cached_height = 0;
	std::string cached_font_face;
	int cached_font_size = 0;
	int cached_font_flags = 0;
	bool cached_font_valid = false;
	uint32_t cached_text_layout_w = 0;
	// Heart icon
	gs_texture_t *heart_texture = nullptr;
	bool heart_texture_tried = false;
	double heart_beat_phase = 0.0;
	float heart_scale_delta = 0.0f;
	std::chrono::steady_clock::time_point last_beat_frame = std::chrono::steady_clock::now();
	// Per-frame layout (set in bpm_display_render)
	float heart_layout_x = 0.0f;
	float heart_layout_y = 0.0f;
	float heart_layout_size = 0.0f;
	float text_layout_x = 0.0f;
	uint32_t text_layout_w = 0;
};

// Effective canvas width available for the BPM text (full width when no heart icon).
static uint32_t effective_text_w(const BpmDisplaySource *d)
{
	return d->text_layout_w > 0 ? d->text_layout_w : d->width;
}

enum RenderMode {
	RenderModeSystemFont = 0,
	RenderModeSegments = 1,
};

enum DigitStyle {
	DigitStyleDigital = 0,
	DigitStyleSlim = 1,
	DigitStyleBold = 2,
	DigitStyleBlock = 3,
};

enum Segment {
	SegmentTop = 1 << 0,
	SegmentUpperRight = 1 << 1,
	SegmentLowerRight = 1 << 2,
	SegmentBottom = 1 << 3,
	SegmentLowerLeft = 1 << 4,
	SegmentUpperLeft = 1 << 5,
	SegmentMiddle = 1 << 6,
};

const uint8_t kDigitSegments[] = {
	SegmentTop | SegmentUpperRight | SegmentLowerRight | SegmentBottom | SegmentLowerLeft | SegmentUpperLeft,
	SegmentUpperRight | SegmentLowerRight,
	SegmentTop | SegmentUpperRight | SegmentMiddle | SegmentLowerLeft | SegmentBottom,
	SegmentTop | SegmentUpperRight | SegmentMiddle | SegmentLowerRight | SegmentBottom,
	SegmentUpperLeft | SegmentMiddle | SegmentUpperRight | SegmentLowerRight,
	SegmentTop | SegmentUpperLeft | SegmentMiddle | SegmentLowerRight | SegmentBottom,
	SegmentTop | SegmentUpperLeft | SegmentMiddle | SegmentLowerLeft | SegmentLowerRight | SegmentBottom,
	SegmentTop | SegmentUpperRight | SegmentLowerRight,
	SegmentTop | SegmentUpperRight | SegmentLowerRight | SegmentBottom | SegmentLowerLeft | SegmentUpperLeft |
		SegmentMiddle,
	SegmentTop | SegmentUpperRight | SegmentLowerRight | SegmentBottom | SegmentUpperLeft | SegmentMiddle,
};

const char *bpm_display_get_name(void *)
{
	return obs_module_text("Source.BpmDisplay.Name");
}

void bpm_display_update(void *data, obs_data_t *settings)
{
	auto *display = static_cast<BpmDisplaySource *>(data);
	display->width = (uint32_t)std::max(120LL, obs_data_get_int(settings, "width"));
	display->height = (uint32_t)std::max(80LL, obs_data_get_int(settings, "height"));
	display->color = (uint32_t)obs_data_get_int(settings, "color");
	display->resting_color = (uint32_t)obs_data_get_int(settings, "resting_color");
	display->active_color = (uint32_t)obs_data_get_int(settings, "active_color");
	display->high_color = (uint32_t)obs_data_get_int(settings, "high_color");
	display->peak_color = (uint32_t)obs_data_get_int(settings, "peak_color");
	display->render_mode = (int)obs_data_get_int(settings, "render_mode");
	display->digit_style = (int)obs_data_get_int(settings, "digit_style");
	if (obs_data_t *font = obs_data_get_obj(settings, "font")) {
		const char *face = obs_data_get_string(font, "face");
		display->font_face = (face && *face) ? face : "GS3 Obviously";
		display->font_size = (int)std::clamp(obs_data_get_int(font, "size"), 8LL, 512LL);
		display->font_flags = (int)obs_data_get_int(font, "flags");
		obs_data_release(font);
	}
	display->use_zone_colors = obs_data_get_bool(settings, "use_zone_colors");
	display->use_smoothed = obs_data_get_bool(settings, "use_smoothed");
	display->show_when_disconnected = obs_data_get_bool(settings, "show_when_disconnected");
	display->glow_enabled = obs_data_get_bool(settings, "glow_enabled");
	display->glow_softness = std::clamp(obs_data_get_double(settings, "glow_softness"), 1.0, 32.0);
	display->active_bpm = std::clamp(obs_data_get_double(settings, "active_bpm"), 1.0, 300.0);
	display->high_bpm = std::clamp(obs_data_get_double(settings, "high_bpm"), display->active_bpm + 1.0, 300.0);
	display->peak_bpm = std::clamp(obs_data_get_double(settings, "peak_bpm"), display->high_bpm + 1.0, 300.0);
	display->cached_font_valid = false;
}

void *bpm_display_create(obs_data_t *settings, obs_source_t *source)
{
	auto *display = new BpmDisplaySource;
	display->source = source;
	bpm_display_update(display, settings);
	return display;
}

void bpm_display_destroy(void *data)
{
	auto *display = static_cast<BpmDisplaySource *>(data);
	if (display->text_texture)
		gs_texture_destroy(display->text_texture);
	if (display->heart_texture)
		gs_texture_destroy(display->heart_texture);
	delete display;
}

void draw_rect(float x, float y, float width, float height)
{
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_draw_sprite(nullptr, 0, (uint32_t)width, (uint32_t)height);
	gs_matrix_pop();
}

void draw_digit(uint8_t segments, float x, float y, float digit_width, float digit_height, float thickness)
{
	const float horizontal_width = digit_width - (2.0f * thickness);
	const float vertical_height = (digit_height - (3.0f * thickness)) * 0.5f;
	const float right = x + digit_width - thickness;
	const float mid_y = y + vertical_height + thickness;
	const float bottom_y = y + digit_height - thickness;

	if (segments & SegmentTop)
		draw_rect(x + thickness, y, horizontal_width, thickness);
	if (segments & SegmentMiddle)
		draw_rect(x + thickness, mid_y, horizontal_width, thickness);
	if (segments & SegmentBottom)
		draw_rect(x + thickness, bottom_y, horizontal_width, thickness);
	if (segments & SegmentUpperLeft)
		draw_rect(x, y + thickness, thickness, vertical_height);
	if (segments & SegmentUpperRight)
		draw_rect(right, y + thickness, thickness, vertical_height);
	if (segments & SegmentLowerLeft)
		draw_rect(x, mid_y + thickness, thickness, vertical_height);
	if (segments & SegmentLowerRight)
		draw_rect(right, mid_y + thickness, thickness, vertical_height);
}

void digit_metrics(int style, float digit_height, float *digit_width, float *thickness, float *spacing)
{
	switch (style) {
	case DigitStyleSlim:
		*digit_width = digit_height * 0.50f;
		*thickness = std::max(3.0f, digit_height * 0.065f);
		*spacing = *digit_width * 0.22f;
		break;
	case DigitStyleBold:
		*digit_width = digit_height * 0.62f;
		*thickness = std::max(5.0f, digit_height * 0.145f);
		*spacing = *digit_width * 0.16f;
		break;
	case DigitStyleBlock:
		*digit_width = digit_height * 0.70f;
		*thickness = std::max(6.0f, digit_height * 0.20f);
		*spacing = *digit_width * 0.11f;
		break;
	case DigitStyleDigital:
	default:
		*digit_width = digit_height * 0.56f;
		*thickness = std::max(4.0f, digit_height * 0.105f);
		*spacing = *digit_width * 0.20f;
		break;
	}
}

void draw_dash(float x, float y, float digit_width, float digit_height, float thickness)
{
	const float horizontal_width = digit_width - (2.0f * thickness);
	const float mid_y = y + ((digit_height - thickness) * 0.5f);
	draw_rect(x + thickness, mid_y, horizontal_width, thickness);
}

struct DisplayValue {
	std::string text;
	bool has_bpm = false;
	double bpm = 0.0;
};

double animated_display_bpm(BpmDisplaySource *display, double target_bpm)
{
	const auto now = std::chrono::steady_clock::now();
	double dt = std::chrono::duration<double>(now - display->last_animation_frame).count();
	display->last_animation_frame = now;
	dt = std::clamp(dt, 0.0, 0.20);

	const double rounded_target = std::clamp(std::lround(target_bpm), 0l, 999l);
	if (!display->displayed_bpm_initialized) {
		display->displayed_bpm = rounded_target;
		display->displayed_bpm_initialized = true;
		return display->displayed_bpm;
	}

	constexpr double bpm_per_second = 24.0;
	const double difference = rounded_target - display->displayed_bpm;
	const double max_step = bpm_per_second * dt;
	if (std::abs(difference) <= max_step) {
		display->displayed_bpm = rounded_target;
	} else {
		display->displayed_bpm += (difference > 0.0 ? max_step : -max_step);
	}

	return display->displayed_bpm;
}

DisplayValue display_value(BpmDisplaySource *display)
{
	const auto snapshot = hyperate::heart_rate_state().snapshot();
	if (!snapshot.has_sample || !snapshot.is_live) {
		display->displayed_bpm_initialized = false;
		display->last_animation_frame = std::chrono::steady_clock::now();
		return DisplayValue{display->show_when_disconnected ? "--" : "", false, 0.0};
	}

	const double bpm = display->use_smoothed ? snapshot.smoothed_bpm : snapshot.raw_bpm;
	const double animated_bpm = animated_display_bpm(display, bpm);
	const int rounded = std::clamp((int)std::lround(animated_bpm), 0, 999);
	return DisplayValue{std::to_string(rounded), true, animated_bpm};
}

uint32_t display_color(const BpmDisplaySource *display, const DisplayValue &value)
{
	if (!display->use_zone_colors || !value.has_bpm)
		return display->color;

	if (value.bpm >= display->peak_bpm)
		return display->peak_color;
	if (value.bpm >= display->high_bpm)
		return display->high_color;
	if (value.bpm >= display->active_bpm)
		return display->active_color;
	return display->resting_color;
}

void invalidate_text_texture(BpmDisplaySource *display)
{
	if (display->text_texture) {
		gs_texture_destroy(display->text_texture);
		display->text_texture = nullptr;
	}
	display->cached_font_valid = false;
}

void ensure_system_font_texture(BpmDisplaySource *display, const std::string &text, uint32_t color)
{
	if (display->cached_font_valid && display->text_texture && display->cached_text == text &&
	    display->cached_color == color && display->cached_width == display->width &&
	    display->cached_height == display->height && display->cached_font_face == display->font_face &&
	    display->cached_font_size == display->font_size &&
	    display->cached_font_flags == display->font_flags &&
	    display->cached_text_layout_w == effective_text_w(display)) {
		return;
	}

	invalidate_text_texture(display);

#ifdef __APPLE__
	const uint32_t tex_w = effective_text_w(display);
	if (tex_w == 0 || display->height == 0 || text.empty())
		return;

	std::vector<uint8_t> pixels((size_t)tex_w * (size_t)display->height * 4, 0);

	CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
	CGContextRef context =
		CGBitmapContextCreate(pixels.data(), tex_w, display->height, 8, tex_w * 4,
				      color_space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	CGColorSpaceRelease(color_space);
	if (!context)
		return;

	CGContextClearRect(context, CGRectMake(0, 0, tex_w, display->height));
	CGContextSetShouldAntialias(context, true);
	CGContextSetAllowsAntialiasing(context, true);

	const CGFloat red = (CGFloat)((color >> 0) & 0xFF) / 255.0f;
	const CGFloat green = (CGFloat)((color >> 8) & 0xFF) / 255.0f;
	const CGFloat blue = (CGFloat)((color >> 16) & 0xFF) / 255.0f;
	const CGFloat alpha = (CGFloat)((color >> 24) & 0xFF) / 255.0f;
	CGColorRef text_color = CGColorCreateGenericRGB(red, green, blue, alpha);
	CFStringRef text_string = CFStringCreateWithCString(kCFAllocatorDefault, text.c_str(), kCFStringEncodingUTF8);

	auto create_font = [&](CGFloat size) -> CTFontRef {
		CFStringRef face = CFStringCreateWithCString(kCFAllocatorDefault, display->font_face.c_str(),
							     kCFStringEncodingUTF8);
		CTFontRef base_font = CTFontCreateWithName(face, size, nullptr);
		if (face)
			CFRelease(face);
		if (!base_font)
			return nullptr;

		CTFontSymbolicTraits traits = 0;
		if (display->font_flags & OBS_FONT_BOLD)
			traits |= kCTFontBoldTrait;
		if (display->font_flags & OBS_FONT_ITALIC)
			traits |= kCTFontItalicTrait;

		CTFontRef font =
			traits ? CTFontCreateCopyWithSymbolicTraits(base_font, size, nullptr, traits, traits)
			       : (CTFontRef)CFRetain(base_font);
		CFRelease(base_font);
		return font;
	};

	auto create_line = [&](CTFontRef font) -> CTLineRef {
		const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
		const void *values[] = {font, text_color};
		CFDictionaryRef attrs =
			CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
					   &kCFTypeDictionaryValueCallBacks);
		CFAttributedStringRef attr_string = CFAttributedStringCreate(kCFAllocatorDefault, text_string, attrs);
		CTLineRef line = CTLineCreateWithAttributedString(attr_string);
		CFRelease(attr_string);
		CFRelease(attrs);
		return line;
	};

	const CGFloat max_text_width = (CGFloat)tex_w * 0.92f;
	const CGFloat max_text_height = (CGFloat)display->height * 0.84f;
	CGFloat effective_font_size = std::min((CGFloat)display->font_size, std::max((CGFloat)8.0, max_text_height));
	CTFontRef font = create_font(effective_font_size);
	if (!font) {
		CFRelease(text_string);
		CGColorRelease(text_color);
		CGContextRelease(context);
		return;
	}

	CTLineRef line = create_line(font);
	CGFloat ascent = 0.0f;
	CGFloat descent = 0.0f;
	CGFloat leading = 0.0f;
	double line_width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
	double line_height = ascent + descent;

	if ((line_width > max_text_width || line_height > max_text_height) && line_width > 0.0 && line_height > 0.0) {
		const double width_scale = (double)max_text_width / line_width;
		const double height_scale = (double)max_text_height / line_height;
		effective_font_size = (CGFloat)std::max(8.0, (double)effective_font_size *
							     std::min(width_scale, height_scale));
		CFRelease(line);
		CFRelease(font);
		font = create_font(effective_font_size);
		if (!font) {
			CFRelease(text_string);
			CGColorRelease(text_color);
			CGContextRelease(context);
			return;
		}
		line = create_line(font);
		line_width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
		line_height = ascent + descent;
	}

	CGRect ink_bounds = CTLineGetImageBounds(line, context);
	const double x = ((double)tex_w - CGRectGetWidth(ink_bounds)) * 0.5 - CGRectGetMinX(ink_bounds);
	const double y = ((double)display->height - CGRectGetHeight(ink_bounds)) * 0.5 - CGRectGetMinY(ink_bounds);

	// OBS uploads the bitmap rows directly as a texture, so draw in bitmap coordinates instead of flipping the
	// CoreText context like an AppKit view would.
	CGContextSetTextMatrix(context, CGAffineTransformIdentity);
	CGContextSetTextPosition(context, (CGFloat)x, (CGFloat)y);
	CTLineDraw(line, context);

	CFRelease(line);
	CFRelease(text_string);
	CGColorRelease(text_color);
	CFRelease(font);
	CGContextRelease(context);

	const uint8_t *texture_data = pixels.data();
	display->text_texture = gs_texture_create(tex_w, display->height, GS_BGRA, 1, &texture_data, 0);
	display->cached_font_valid = display->text_texture != nullptr;
	display->cached_text = text;
	display->cached_color = color;
	display->cached_width = display->width;
	display->cached_height = display->height;
	display->cached_font_face = display->font_face;
	display->cached_font_size = display->font_size;
	display->cached_font_flags = display->font_flags;
	display->cached_text_layout_w = tex_w;
#elif defined(_WIN32)
	const uint32_t tex_w = effective_text_w(display);
	if (tex_w == 0 || display->height == 0 || text.empty())
		return;

	const int width = (int)tex_w;
	const int height = (int)display->height;
	std::vector<uint8_t> pixels((size_t)width * (size_t)height * 4, 0);

	HDC screen_dc = GetDC(nullptr);
	if (!screen_dc)
		return;

	HDC memory_dc = CreateCompatibleDC(screen_dc);
	if (!memory_dc) {
		ReleaseDC(nullptr, screen_dc);
		return;
	}

	BITMAPINFO bitmap_info = {};
	bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_info.bmiHeader.biWidth = width;
	bitmap_info.bmiHeader.biHeight = -height;
	bitmap_info.bmiHeader.biPlanes = 1;
	bitmap_info.bmiHeader.biBitCount = 32;
	bitmap_info.bmiHeader.biCompression = BI_RGB;

	void *bitmap_bits = nullptr;
	HBITMAP bitmap = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, &bitmap_bits, nullptr, 0);
	ReleaseDC(nullptr, screen_dc);
	if (!bitmap || !bitmap_bits) {
		DeleteDC(memory_dc);
		return;
	}

	HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
	RECT full_rect{0, 0, width, height};
	HBRUSH black_brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
	FillRect(memory_dc, &full_rect, black_brush);
	SetBkMode(memory_dc, TRANSPARENT);
	SetTextColor(memory_dc, RGB(255, 255, 255));

	auto utf8_to_wide = [](const std::string &value) {
		if (value.empty())
			return std::wstring();

		int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
		if (count <= 0)
			return std::wstring();

		std::wstring output((size_t)count, L'\0');
		if (!MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, output.data(), count))
			return std::wstring();
		if (!output.empty() && output.back() == L'\0')
			output.pop_back();
		return output;
	};

	const std::wstring wide_text = utf8_to_wide(text);
	if (wide_text.empty()) {
		SelectObject(memory_dc, old_bitmap);
		DeleteObject(bitmap);
		DeleteDC(memory_dc);
		return;
	}

	std::wstring wide_face = utf8_to_wide(display->font_face);
	if (wide_face.empty())
		wide_face = L"Arial";

	auto create_font = [&](int pixel_size) {
		LOGFONTW font = {};
		font.lfHeight = -std::max(8, pixel_size);
		font.lfWeight = (display->font_flags & OBS_FONT_BOLD) ? FW_BOLD : FW_NORMAL;
		font.lfItalic = (display->font_flags & OBS_FONT_ITALIC) ? TRUE : FALSE;
		font.lfCharSet = DEFAULT_CHARSET;
		font.lfQuality = ANTIALIASED_QUALITY;
		wcsncpy_s(font.lfFaceName, wide_face.c_str(), _TRUNCATE);
		return CreateFontIndirectW(&font);
	};

	auto measure_text = [&](HFONT font, RECT *rect) {
		HGDIOBJ old_font = SelectObject(memory_dc, font);
		*rect = RECT{0, 0, width, height};
		DrawTextW(memory_dc, wide_text.c_str(), -1, rect, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
		SelectObject(memory_dc, old_font);
	};

	const int max_text_width = std::max(1, (int)((double)width * 0.92));
	const int max_text_height = std::max(1, (int)((double)height * 0.84));
	int effective_font_size = std::min(display->font_size, max_text_height);
	HFONT font = create_font(effective_font_size);
	if (!font) {
		SelectObject(memory_dc, old_bitmap);
		DeleteObject(bitmap);
		DeleteDC(memory_dc);
		return;
	}

	RECT measured{};
	measure_text(font, &measured);
	const int measured_width = std::max(1, (int)(measured.right - measured.left));
	const int measured_height = std::max(1, (int)(measured.bottom - measured.top));
	if (measured_width > max_text_width || measured_height > max_text_height) {
		const double width_scale = (double)max_text_width / (double)measured_width;
		const double height_scale = (double)max_text_height / (double)measured_height;
		effective_font_size =
			std::max(8, (int)std::floor((double)effective_font_size * std::min(width_scale, height_scale)));
		DeleteObject(font);
		font = create_font(effective_font_size);
		if (!font) {
			SelectObject(memory_dc, old_bitmap);
			DeleteObject(bitmap);
			DeleteDC(memory_dc);
			return;
		}
	}

	HGDIOBJ old_font = SelectObject(memory_dc, font);
	DrawTextW(memory_dc, wide_text.c_str(), -1, &full_rect,
		  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	SelectObject(memory_dc, old_font);
	DeleteObject(font);

	const uint8_t red = (uint8_t)((color >> 0) & 0xFF);
	const uint8_t green = (uint8_t)((color >> 8) & 0xFF);
	const uint8_t blue = (uint8_t)((color >> 16) & 0xFF);
	const uint8_t alpha = (uint8_t)((color >> 24) & 0xFF);
	const auto *source = static_cast<const uint8_t *>(bitmap_bits);
	for (size_t i = 0; i < (size_t)width * (size_t)height; ++i) {
		const uint8_t mask = std::max(source[(i * 4) + 0],
					      std::max(source[(i * 4) + 1], source[(i * 4) + 2]));
		pixels[(i * 4) + 0] = (uint8_t)(((uint16_t)blue * mask) / 255);
		pixels[(i * 4) + 1] = (uint8_t)(((uint16_t)green * mask) / 255);
		pixels[(i * 4) + 2] = (uint8_t)(((uint16_t)red * mask) / 255);
		pixels[(i * 4) + 3] = (uint8_t)(((uint16_t)alpha * mask) / 255);
	}

	SelectObject(memory_dc, old_bitmap);
	DeleteObject(bitmap);
	DeleteDC(memory_dc);

	const uint8_t *texture_data = pixels.data();
	const uint32_t tex_w2 = effective_text_w(display);
	display->text_texture = gs_texture_create(tex_w2, display->height, GS_BGRA, 1, &texture_data, 0);
	display->cached_font_valid = display->text_texture != nullptr;
	display->cached_text = text;
	display->cached_color = color;
	display->cached_width = display->width;
	display->cached_height = display->height;
	display->cached_font_face = display->font_face;
	display->cached_font_size = display->font_size;
	display->cached_font_flags = display->font_flags;
	display->cached_text_layout_w = tex_w2;
#else
	UNUSED_PARAMETER(display);
	UNUSED_PARAMETER(text);
	UNUSED_PARAMETER(color);
#endif
}

void draw_system_font(BpmDisplaySource *display, const std::string &text, uint32_t color)
{
	ensure_system_font_texture(display, text, color);
	if (!display->text_texture)
		return;

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture_srgb(image, display->text_texture);

	gs_matrix_push();
	gs_matrix_translate3f(display->text_layout_x, 0.0f, 0.0f);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(display->text_texture, 0, effective_text_w(display), display->height);
	gs_matrix_pop();
}

void draw_system_font_glow(BpmDisplaySource *display, const std::string &text, uint32_t color)
{
	ensure_system_font_texture(display, text, color);
	if (!display->text_texture)
		return;

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture_srgb(image, display->text_texture);

	const float radius = (float)display->glow_softness;
	const float inner = std::max(1.0f, radius * 0.45f);
	const float outer = std::max(inner + 1.0f, radius);
	const float offsets[][2] = {
		{-inner, 0.0f}, {inner, 0.0f}, {0.0f, -inner}, {0.0f, inner},
		{-inner, -inner}, {inner, -inner}, {-inner, inner}, {inner, inner},
		{-outer, 0.0f}, {outer, 0.0f}, {0.0f, -outer}, {0.0f, outer},
		{-outer, -outer}, {outer, -outer}, {-outer, outer}, {outer, outer},
	};

	const uint32_t tw = effective_text_w(display);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_ONE);
	for (const auto &offset : offsets) {
		gs_matrix_push();
		gs_matrix_translate3f(display->text_layout_x + offset[0], offset[1], 0.0f);
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(display->text_texture, 0, tw, display->height);
		gs_matrix_pop();
	}
	gs_blend_state_pop();
}

void draw_segment_text(BpmDisplaySource *display, const std::string &text, uint32_t color_rgba)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	vec4 color;
	vec4_from_rgba(&color, color_rgba);
	gs_effect_set_vec4(color_param, &color);

	const float region_w = display->text_layout_w > 0 ? (float)display->text_layout_w : (float)display->width;
	const float region_x = display->text_layout_x;
	const float canvas_height = (float)display->height;
	const float digit_height = canvas_height * 0.78f;
	float digit_width = 0.0f;
	float thickness = 0.0f;
	float spacing = 0.0f;
	digit_metrics(display->digit_style, digit_height, &digit_width, &thickness, &spacing);
	const float total_width = (digit_width * (float)text.size()) + (spacing * (float)(text.size() - 1));
	const float start_x = region_x + (region_w - total_width) * 0.5f;
	const float start_y = (canvas_height - digit_height) * 0.5f;

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	for (size_t i = 0; i < text.size(); ++i) {
		const float x = start_x + ((digit_width + spacing) * (float)i);
		if (text[i] >= '0' && text[i] <= '9')
			draw_digit(kDigitSegments[text[i] - '0'], x, start_y, digit_width, digit_height, thickness);
		else
			draw_dash(x, start_y, digit_width, digit_height, thickness);
	}

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

uint32_t with_alpha(uint32_t color, uint8_t alpha)
{
	return (color & 0x00FFFFFF) | ((uint32_t)alpha << 24);
}

void draw_segment_glow(BpmDisplaySource *display, const std::string &text, uint32_t color_rgba)
{
	const float radius = (float)display->glow_softness;
	const float inner = std::max(1.0f, radius * 0.45f);
	const float outer = std::max(inner + 1.0f, radius);
	const struct {
		float x;
		float y;
		uint8_t alpha;
	} offsets[] = {
		{-inner, 0.0f, 0x6A}, {inner, 0.0f, 0x6A}, {0.0f, -inner, 0x6A}, {0.0f, inner, 0x6A},
		{-inner, -inner, 0x52}, {inner, -inner, 0x52}, {-inner, inner, 0x52}, {inner, inner, 0x52},
		{-outer, 0.0f, 0x38}, {outer, 0.0f, 0x38}, {0.0f, -outer, 0x38}, {0.0f, outer, 0x38},
		{-outer, -outer, 0x28}, {outer, -outer, 0x28}, {-outer, outer, 0x28}, {outer, outer, 0x28},
	};

	// Glow offsets are applied in the segment draw calls via text_layout_x, so no
	// additional per-pass translation is needed for the x-axis here.
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_ONE);
	for (const auto &offset : offsets) {
		gs_matrix_push();
		gs_matrix_translate3f(offset.x, offset.y, 0.0f);
		draw_segment_text(display, text, with_alpha(color_rgba, offset.alpha));
		gs_matrix_pop();
	}
	gs_blend_state_pop();
}

// ---- Heart icon helpers ----

void ensure_heart_texture(BpmDisplaySource *display)
{
	if (display->heart_texture_tried)
		return;
	display->heart_texture_tried = true;
	char *path = obs_module_file("heart.png");
	if (!path)
		return;
	display->heart_texture = gs_texture_create_from_file(path);
	bfree(path);
}

void update_heart_pulse(BpmDisplaySource *display, double bpm)
{
	const auto now = std::chrono::steady_clock::now();
	const double dt = std::chrono::duration<double>(now - display->last_beat_frame).count();
	display->last_beat_frame = now;

	if (bpm <= 0.0) {
		display->heart_scale_delta *= 0.88f;
		return;
	}

	const double beat_interval = std::max(0.2, 60.0 / bpm);
	display->heart_beat_phase =
		std::fmod(display->heart_beat_phase + std::max(0.0, dt), beat_interval);
	const double np = display->heart_beat_phase / beat_interval;

	// Gaussian punch that mimics the systolic peak of an ECG waveform.
	const auto gauss = [](double x, double mu, double sigma) -> double {
		const double n = (x - mu) / sigma;
		return std::exp(-n * n);
	};
	const double punch  = gauss(np, 0.04, 0.040);
	const double settle = gauss(np, 0.15, 0.070);
	const float desired = static_cast<float>(0.20 * (punch - 0.22 * settle));
	display->heart_scale_delta += (desired - display->heart_scale_delta) * 0.38f;
}

void draw_heart_icon(BpmDisplaySource *display)
{
	if (!display->heart_texture || display->heart_layout_size <= 0.0f)
		return;

	// Scale around the icon's center so the pulse looks natural.
	const float scale = 1.0f + display->heart_scale_delta;
	const float draw_size = display->heart_layout_size * scale;
	const float cx = display->heart_layout_x + display->heart_layout_size * 0.5f;
	const float cy = display->heart_layout_y + display->heart_layout_size * 0.5f;
	const float draw_x = cx - draw_size * 0.5f;
	const float draw_y = cy - draw_size * 0.5f;

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image_param = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture_srgb(image_param, display->heart_texture);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	gs_matrix_push();
	gs_matrix_translate3f(draw_x, draw_y, 0.0f);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(display->heart_texture, 0, (uint32_t)draw_size, (uint32_t)draw_size);
	gs_matrix_pop();
	gs_blend_state_pop();
}

void bpm_display_render(void *data, gs_effect_t *)
{
	auto *display = static_cast<BpmDisplaySource *>(data);

	// Lazy-load the heart texture on the graphics thread.
	ensure_heart_texture(display);

	const DisplayValue value = display_value(display);
	const std::string &text = value.text;
	if (text.empty())
		return;

	// Compute layout: heart on the left, BPM text in the remaining region.
	if (display->heart_texture) {
		const float heart_size = (float)display->height * 0.70f;
		const float gap        = (float)display->height * 0.06f;
		display->heart_layout_size = heart_size;
		display->heart_layout_x    = (float)display->width * 0.01f;
		display->heart_layout_y    = ((float)display->height - heart_size) * 0.5f;
		display->text_layout_x     = display->heart_layout_x + heart_size + gap;
		display->text_layout_w     = (uint32_t)std::max(60.0f, (float)display->width - display->text_layout_x);
	} else {
		display->heart_layout_size = 0.0f;
		display->text_layout_x     = 0.0f;
		display->text_layout_w     = 0;
	}

	// Update the heart's beat-pulse animation.
	update_heart_pulse(display, value.has_bpm ? value.bpm : 0.0);

	// Draw heart icon (natural PNG colors; zone color applies to the number only).
	draw_heart_icon(display);

	const uint32_t color = display_color(display, value);
	if (display->render_mode == RenderModeSystemFont) {
		if (display->glow_enabled)
			draw_system_font_glow(display, text, color);
		draw_system_font(display, text, color);
		if (!display->text_texture) {
			if (display->glow_enabled)
				draw_segment_glow(display, text, color);
			draw_segment_text(display, text, color);
		}
	} else {
		if (display->glow_enabled)
			draw_segment_glow(display, text, color);
		draw_segment_text(display, text, color);
	}
}

bool render_mode_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool system_font = obs_data_get_int(settings, "render_mode") == RenderModeSystemFont;
	obs_property_set_visible(obs_properties_get(props, "font"), system_font);
	obs_property_set_visible(obs_properties_get(props, "digit_style"), !system_font);
	return true;
}

bool glow_enabled_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, "glow_softness"), obs_data_get_bool(settings, "glow_enabled"));
	return true;
}

obs_properties_t *bpm_display_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *render_mode = obs_properties_add_list(props, "render_mode",
							      obs_module_text("Source.BpmDisplay.RenderMode"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(render_mode, obs_module_text("Source.BpmDisplay.RenderMode.SystemFont"),
				  RenderModeSystemFont);
	obs_property_list_add_int(render_mode, obs_module_text("Source.BpmDisplay.RenderMode.Segments"),
				  RenderModeSegments);
	obs_property_set_modified_callback(render_mode, render_mode_changed);

	obs_properties_add_font(props, "font", obs_module_text("Source.BpmDisplay.Font"));

	obs_property_t *style = obs_properties_add_list(props, "digit_style",
							 obs_module_text("Source.BpmDisplay.DigitStyle"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(style, obs_module_text("Source.BpmDisplay.DigitStyle.Digital"),
				  DigitStyleDigital);
	obs_property_list_add_int(style, obs_module_text("Source.BpmDisplay.DigitStyle.Slim"), DigitStyleSlim);
	obs_property_list_add_int(style, obs_module_text("Source.BpmDisplay.DigitStyle.Bold"), DigitStyleBold);
	obs_property_list_add_int(style, obs_module_text("Source.BpmDisplay.DigitStyle.Block"), DigitStyleBlock);

	obs_properties_add_color_alpha(props, "color", obs_module_text("Source.BpmDisplay.Color"));
	obs_properties_add_bool(props, "use_zone_colors", obs_module_text("Source.BpmDisplay.UseZoneColors"));
	obs_properties_add_float(props, "active_bpm", obs_module_text("Source.BpmDisplay.ActiveBpm"), 1.0, 300.0,
				 1.0);
	obs_properties_add_color_alpha(props, "resting_color", obs_module_text("Source.BpmDisplay.RestingColor"));
	obs_properties_add_color_alpha(props, "active_color", obs_module_text("Source.BpmDisplay.ActiveColor"));
	obs_properties_add_float(props, "high_bpm", obs_module_text("Source.BpmDisplay.HighBpm"), 1.0, 300.0,
				 1.0);
	obs_properties_add_color_alpha(props, "high_color", obs_module_text("Source.BpmDisplay.HighColor"));
	obs_properties_add_float(props, "peak_bpm", obs_module_text("Source.BpmDisplay.PeakBpm"), 1.0, 300.0,
				 1.0);
	obs_properties_add_color_alpha(props, "peak_color", obs_module_text("Source.BpmDisplay.PeakColor"));
	obs_properties_add_int(props, "width", obs_module_text("Source.BpmDisplay.Width"), 120, 1920, 1);
	obs_properties_add_int(props, "height", obs_module_text("Source.BpmDisplay.Height"), 80, 1080, 1);
	obs_properties_add_bool(props, "use_smoothed", obs_module_text("Source.BpmDisplay.UseSmoothed"));
	obs_property_t *glow_enabled =
		obs_properties_add_bool(props, "glow_enabled", obs_module_text("Source.BpmDisplay.GlowEnabled"));
	obs_property_set_modified_callback(glow_enabled, glow_enabled_changed);
	obs_properties_add_float_slider(props, "glow_softness", obs_module_text("Source.BpmDisplay.GlowSoftness"), 1.0,
					32.0, 0.5);
	obs_properties_add_bool(props, "show_when_disconnected",
				obs_module_text("Source.BpmDisplay.ShowWhenDisconnected"));
	return props;
}

void bpm_display_defaults(obs_data_t *settings)
{
	obs_data_t *font = obs_data_create();
	obs_data_set_default_string(font, "face", "GS3 Obviously");
	obs_data_set_default_string(font, "style", "Wide Bold");
	obs_data_set_default_int(font, "size", 96);
	obs_data_set_default_int(font, "flags", OBS_FONT_BOLD);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);

	obs_data_set_default_int(settings, "color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "resting_color", 0xFF38D878);
	obs_data_set_default_int(settings, "active_color", 0xFF47A8FF);
	obs_data_set_default_int(settings, "high_color", 0xFF00D7FF);
	obs_data_set_default_int(settings, "peak_color", 0xFF3B3BFF);
	obs_data_set_default_int(settings, "render_mode", RenderModeSystemFont);
	obs_data_set_default_int(settings, "digit_style", DigitStyleDigital);
	obs_data_set_default_bool(settings, "use_zone_colors", false);
	obs_data_set_default_double(settings, "active_bpm", 100.0);
	obs_data_set_default_double(settings, "high_bpm", 130.0);
	obs_data_set_default_double(settings, "peak_bpm", 160.0);
	obs_data_set_default_int(settings, "width", 360);
	obs_data_set_default_int(settings, "height", 160);
	obs_data_set_default_bool(settings, "use_smoothed", true);
	obs_data_set_default_bool(settings, "glow_enabled", false);
	obs_data_set_default_double(settings, "glow_softness", 6.0);
	obs_data_set_default_bool(settings, "show_when_disconnected", true);
}

uint32_t bpm_display_width(void *data)
{
	return static_cast<BpmDisplaySource *>(data)->width;
}

uint32_t bpm_display_height(void *data)
{
	return static_cast<BpmDisplaySource *>(data)->height;
}

} // namespace

obs_source_info bpm_display_source_info = [] {
	obs_source_info info = {};
	info.id = "hyperate_bpm_display";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = bpm_display_get_name;
	info.create = bpm_display_create;
	info.destroy = bpm_display_destroy;
	info.update = bpm_display_update;
	info.get_defaults = bpm_display_defaults;
	info.get_properties = bpm_display_properties;
	info.get_width = bpm_display_width;
	info.get_height = bpm_display_height;
	info.video_render = bpm_display_render;
	return info;
}();
