// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "gpu_timer.h"

#include <stdexcept>

namespace edge
{

void GpuTimer::set_enabled(bool enabled)
{
	if (enabled == enabled_)
	{
		return;
	}
	if (enabled)
	{
		if (!available())
		{
			throw std::runtime_error("GPU timing is not available: " + unavailable_);
		}
		start();
	}
	else
	{
		stop();
		last_pass_times_us_.clear();
		last_frame_ = 0;
		history_.clear();
	}
	enabled_ = enabled;
}

std::optional<std::vector<double>> GpuTimer::pass_times_us(uint64_t frame) const
{
	for (auto const &entry : history_)
	{
		if (entry.first == frame)
		{
			return entry.second;
		}
	}
	return std::nullopt;
}

bool GpuTimer::disjoint(uint64_t frame) const
{
	for (auto const &entry : history_)
	{
		if (entry.first == frame)
		{
			return !entry.second;
		}
	}
	return false;
}

void GpuTimer::record(uint64_t frame, std::vector<double> times)
{
	// the last durations stay those of the latest frame read back
	if (frame > last_frame_)
	{
		last_pass_times_us_ = times;
		last_frame_ = frame;
	}
	keep(frame, std::move(times));
}

void GpuTimer::record_disjoint(uint64_t frame)
{
	keep(frame, std::nullopt);
}

void GpuTimer::keep(uint64_t frame, std::optional<std::vector<double>> times)
{
	history_.push_back({frame, std::move(times)});
	if (history_.size() > HISTORY)
	{
		history_.pop_front();
	}
}

}
