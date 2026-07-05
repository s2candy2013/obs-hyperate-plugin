#pragma once

#include <chrono>
#include <mutex>
#include <string>

namespace hyperate {

enum class HeartRateZone {
	Unknown,
	Resting,
	Active,
	High,
	Peak,
};

struct HeartRateSnapshot {
	bool has_sample = false;
	bool is_live = false;
	double raw_bpm = 0.0;
	double smoothed_bpm = 0.0;
	double session_max_bpm = 0.0;
	HeartRateZone zone = HeartRateZone::Unknown;
	std::chrono::steady_clock::time_point last_sample_time{};
};

class HeartRateState {
public:
	void submit_bpm(double bpm);
	void reset_session();
	void set_live(bool live);
	void set_smoothing_alpha(double alpha);
	HeartRateSnapshot snapshot() const;

private:
	static HeartRateZone zone_for_bpm(double bpm);

	mutable std::mutex mutex_;
	bool has_sample_ = false;
	bool is_live_ = false;
	double raw_bpm_ = 0.0;
	double smoothed_bpm_ = 0.0;
	double session_max_bpm_ = 0.0;
	double smoothing_alpha_ = 0.25;
	HeartRateZone zone_ = HeartRateZone::Unknown;
	std::chrono::steady_clock::time_point last_sample_time_{};
};

HeartRateState &heart_rate_state();
const char *zone_name(HeartRateZone zone);

} // namespace hyperate
