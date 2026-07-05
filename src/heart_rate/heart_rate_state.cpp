#include "heart_rate/heart_rate_state.hpp"

#include <algorithm>

namespace hyperate {

void HeartRateState::submit_bpm(double bpm)
{
	if (bpm <= 0.0)
		return;

	std::lock_guard<std::mutex> lock(mutex_);
	raw_bpm_ = bpm;
	smoothed_bpm_ = has_sample_ ? (smoothing_alpha_ * bpm) + ((1.0 - smoothing_alpha_) * smoothed_bpm_) : bpm;
	session_max_bpm_ = std::max(session_max_bpm_, bpm);
	zone_ = zone_for_bpm(smoothed_bpm_);
	last_sample_time_ = std::chrono::steady_clock::now();
	has_sample_ = true;
	is_live_ = true;
}

void HeartRateState::reset_session()
{
	std::lock_guard<std::mutex> lock(mutex_);
	has_sample_ = false;
	is_live_ = false;
	raw_bpm_ = 0.0;
	smoothed_bpm_ = 0.0;
	session_max_bpm_ = 0.0;
	zone_ = HeartRateZone::Unknown;
	last_sample_time_ = {};
}

void HeartRateState::set_live(bool live)
{
	std::lock_guard<std::mutex> lock(mutex_);
	is_live_ = live;
	if (!live)
		has_sample_ = false;
}

void HeartRateState::set_smoothing_alpha(double alpha)
{
	std::lock_guard<std::mutex> lock(mutex_);
	smoothing_alpha_ = std::clamp(alpha, 0.01, 1.0);
}

HeartRateSnapshot HeartRateState::snapshot() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return HeartRateSnapshot{
		has_sample_,
		is_live_,
		raw_bpm_,
		smoothed_bpm_,
		session_max_bpm_,
		zone_,
		last_sample_time_,
	};
}

HeartRateZone HeartRateState::zone_for_bpm(double bpm)
{
	if (bpm <= 0.0)
		return HeartRateZone::Unknown;
	if (bpm < 100.0)
		return HeartRateZone::Resting;
	if (bpm < 130.0)
		return HeartRateZone::Active;
	if (bpm < 160.0)
		return HeartRateZone::High;
	return HeartRateZone::Peak;
}

HeartRateState &heart_rate_state()
{
	static HeartRateState state;
	return state;
}

const char *zone_name(HeartRateZone zone)
{
	switch (zone) {
	case HeartRateZone::Resting:
		return "Resting";
	case HeartRateZone::Active:
		return "Active";
	case HeartRateZone::High:
		return "High";
	case HeartRateZone::Peak:
		return "Peak";
	case HeartRateZone::Unknown:
	default:
		return "Unknown";
	}
}

} // namespace hyperate
