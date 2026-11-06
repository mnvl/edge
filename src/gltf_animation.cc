// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// GltfReader's clips (gltf_reader.h): each node's channels sampled at their keys (and between a cubic spline's) into translation,
// rotation and scale keyframes, and the weights of its mesh's morph targets.

#include "gltf_reader.h"

#include <algorithm>
#include <cmath>
#include <rove/scalar.h>
#include <sstream>
#include <stdexcept>

#include "weights_sampler.h"

namespace edge
{

namespace
{

using load_error = GltfLoader::load_error;

struct Vec3ChannelTrack
{
	fastgltf::AnimationInterpolation interpolation = fastgltf::AnimationInterpolation::Linear;
	std::vector<float> times;
	std::vector<fastgltf::math::fvec3> values;
	std::vector<fastgltf::math::fvec3> in_tangents;
	std::vector<fastgltf::math::fvec3> out_tangents;
};

struct QuatChannelTrack
{
	fastgltf::AnimationInterpolation interpolation = fastgltf::AnimationInterpolation::Linear;
	std::vector<float> times;
	std::vector<fastgltf::math::fquat> values;
	std::vector<fastgltf::math::fquat> in_tangents;
	std::vector<fastgltf::math::fquat> out_tangents;
};

struct WeightsChannelTrack
{
	fastgltf::AnimationInterpolation interpolation = fastgltf::AnimationInterpolation::Linear;
	std::vector<float> times;
	std::vector<std::vector<float>> values;
	std::vector<std::vector<float>> in_tangents;
	std::vector<std::vector<float>> out_tangents;
	std::size_t weights_count = 0;
};

struct NodeAnimationTracks
{
	bool has_translation = false;
	Vec3ChannelTrack translation;
	bool has_rotation = false;
	QuatChannelTrack rotation;
	bool has_scale = false;
	Vec3ChannelTrack scale;
	bool has_weights = false;
	WeightsChannelTrack weights;
};

std::string animation_path_to_string(fastgltf::AnimationPath path)
{
	switch (path)
	{
		case fastgltf::AnimationPath::Translation: return "translation";
		case fastgltf::AnimationPath::Rotation: return "rotation";
		case fastgltf::AnimationPath::Scale: return "scale";
		case fastgltf::AnimationPath::Weights: return "weights";
	}
	return "unknown";
}

std::string make_animation_channel_context(fastgltf::Animation const &animation,
	std::size_t animation_index,
	fastgltf::AnimationChannel const &channel,
	std::size_t channel_index)
{
	std::ostringstream stream;
	stream << "glTF animation[" << animation_index << "]";
	if (!animation.name.empty())
	{
		stream << " '" << animation.name.c_str() << "'";
	}
	stream << " channel[" << channel_index << "] path='"
		<< animation_path_to_string(channel.path) << "'";
	if (channel.nodeIndex)
	{
		stream << " node=" << static_cast<std::size_t>(*channel.nodeIndex);
	}
	return stream.str();
}

void read_animation_times(fastgltf::Asset const &asset_, std::size_t accessor_index,
	std::vector<float> &times,
	std::string const &context)
{
	if (accessor_index >= asset_.accessors.size())
	{
		throw load_error(context + ": invalid input accessor index");
	}

	times.clear();
	fastgltf::Accessor const &accessor = asset_.accessors[accessor_index];
	times.reserve(accessor.count);
	fastgltf::iterateAccessorWithIndex<float>(
		asset_, accessor,
		[&](float time, std::size_t)
			{
				times.push_back(time);
			});
	if (times.empty())
	{
		throw load_error(context + ": empty input time accessor");
	}

	for (float time : times)
	{
		if (!std::isfinite(time))
		{
			throw load_error(context + ": input time accessor contains non-finite value");
		}
	}

	for (std::size_t i = 1; i < times.size(); ++i)
	{
		if (times[i] <= times[i - 1])
		{
			throw load_error(context + ": input time accessor must be strictly increasing");
		}
	}
}

void read_animation_vec3_values(fastgltf::Asset const &asset_, std::size_t accessor_index,
	fastgltf::AnimationInterpolation interpolation,
	std::size_t keyframes_count,
	Vec3ChannelTrack &track,
	std::string const &context)
{
	if (accessor_index >= asset_.accessors.size() || keyframes_count == 0)
	{
		throw load_error(context + ": invalid vec3 output accessor or empty keyframe count");
	}

	fastgltf::Accessor const &accessor = asset_.accessors[accessor_index];
	std::vector<fastgltf::math::fvec3> raw_values;
	raw_values.reserve(accessor.count);
	fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
		asset_, accessor,
		[&](fastgltf::math::fvec3 const &value, std::size_t)
			{
				raw_values.push_back(value);
			});

	if (raw_values.empty())
	{
		throw load_error(context + ": empty vec3 output accessor");
	}

	if (interpolation == fastgltf::AnimationInterpolation::CubicSpline)
	{
		std::size_t const expected_count = keyframes_count * 3;
		if (raw_values.size() != expected_count)
		{
			throw load_error(context + ": cubic-spline vec3 output count does not match 3x keyframe count");
		}

		track.in_tangents.resize(keyframes_count);
		track.values.resize(keyframes_count);
		track.out_tangents.resize(keyframes_count);
		for (std::size_t keyframe_index = 0; keyframe_index < keyframes_count; ++keyframe_index)
		{
			track.in_tangents[keyframe_index] = raw_values[keyframe_index * 3 + 0];
			track.values[keyframe_index] = raw_values[keyframe_index * 3 + 1];
			track.out_tangents[keyframe_index] = raw_values[keyframe_index * 3 + 2];
		}
	}
	else
	{
		if (raw_values.size() != keyframes_count)
		{
			throw load_error(context + ": vec3 output count does not match keyframe count");
		}
		track.values = raw_values;
		track.in_tangents.clear();
		track.out_tangents.clear();
	}
}

void read_animation_quat_values(fastgltf::Asset const &asset_, std::size_t accessor_index,
	fastgltf::AnimationInterpolation interpolation,
	std::size_t keyframes_count,
	QuatChannelTrack &track,
	std::string const &context)
{
	if (accessor_index >= asset_.accessors.size() || keyframes_count == 0)
	{
		throw load_error(context + ": invalid quaternion output accessor or empty keyframe count");
	}

	fastgltf::Accessor const &accessor = asset_.accessors[accessor_index];
	std::vector<fastgltf::math::fquat> raw_values;
	raw_values.reserve(accessor.count);
	fastgltf::iterateAccessorWithIndex<fastgltf::math::fquat>(
		asset_, accessor,
		[&](fastgltf::math::fquat const &value, std::size_t)
			{
				raw_values.push_back(value);
			});

	if (raw_values.empty())
	{
		throw load_error(context + ": empty quaternion output accessor");
	}

	if (interpolation == fastgltf::AnimationInterpolation::CubicSpline)
	{
		std::size_t const expected_count = keyframes_count * 3;
		if (raw_values.size() != expected_count)
		{
			throw load_error(context + ": cubic-spline quaternion output count does not match 3x keyframe count");
		}

		track.in_tangents.resize(keyframes_count);
		track.values.resize(keyframes_count);
		track.out_tangents.resize(keyframes_count);
		for (std::size_t keyframe_index = 0; keyframe_index < keyframes_count; ++keyframe_index)
		{
			track.in_tangents[keyframe_index] = raw_values[keyframe_index * 3 + 0];
			track.values[keyframe_index] = raw_values[keyframe_index * 3 + 1];
			track.out_tangents[keyframe_index] = raw_values[keyframe_index * 3 + 2];
		}
	}
	else
	{
		if (raw_values.size() != keyframes_count)
		{
			throw load_error(context + ": quaternion output count does not match keyframe count");
		}
		track.values = raw_values;
		track.in_tangents.clear();
		track.out_tangents.clear();
	}

	// glTF's rotations are unit quaternions: one of no length or not a number has no rotation in it (rove refuses it; it was the
	// identity).  The tangents of a cubic spline may be anything.
	for (std::size_t keyframe_index = 0; keyframe_index < track.values.size(); ++keyframe_index)
	{
		fastgltf::math::fquat const &q = track.values[keyframe_index];
		float const norm_sq = q.x() * q.x() + q.y() * q.y() + q.z() * q.z() + q.w() * q.w();
		if (!(norm_sq > rove::scalar_traits<float>::zero_length_sq) || !std::isfinite(norm_sq))
		{
			throw load_error(context + ": rotation keyframe " + std::to_string(keyframe_index) + " is no rotation (squared norm " +
				std::to_string(norm_sq) + ")");
		}
	}
}

void read_animation_weights_values(fastgltf::Asset const &asset_, std::size_t accessor_index,
	fastgltf::AnimationInterpolation interpolation,
	std::size_t keyframes_count,
	std::size_t weights_count,
	WeightsChannelTrack &track,
	std::string const &context)
{
	if (accessor_index >= asset_.accessors.size() || keyframes_count == 0 || weights_count == 0)
	{
		throw load_error(context + ": invalid weights output accessor or empty keyframe/weights count");
	}

	fastgltf::Accessor const &accessor = asset_.accessors[accessor_index];
	std::vector<float> raw_values;
	raw_values.reserve(accessor.count);
	fastgltf::iterateAccessorWithIndex<float>(
		asset_, accessor,
		[&](float value, std::size_t)
		{
			raw_values.push_back(value);
		});

	if (raw_values.empty())
	{
		throw load_error(context + ": empty weights output accessor");
	}

	for (float raw_value : raw_values)
	{
		if (!std::isfinite(raw_value))
		{
			throw load_error(context + ": weights output accessor contains non-finite value");
		}
	}

	track.weights_count = weights_count;
	track.values.assign(keyframes_count, std::vector<float>(weights_count, 0.0f));
	track.in_tangents.clear();
	track.out_tangents.clear();

	if (interpolation == fastgltf::AnimationInterpolation::CubicSpline)
	{
		std::size_t const expected_count = keyframes_count * weights_count * 3;
		if (raw_values.size() != expected_count)
		{
			throw load_error(context + ": cubic-spline weights output count does not match 3x keyframe count * weights count");
		}

		track.in_tangents.assign(keyframes_count, std::vector<float>(weights_count, 0.0f));
		track.out_tangents.assign(keyframes_count, std::vector<float>(weights_count, 0.0f));
		for (std::size_t keyframe_index = 0; keyframe_index < keyframes_count; ++keyframe_index)
		{
			std::size_t const base_index = keyframe_index * weights_count * 3;
			for (std::size_t weight_index = 0; weight_index < weights_count; ++weight_index)
			{
				track.in_tangents[keyframe_index][weight_index] = raw_values[base_index + weight_index];
				track.values[keyframe_index][weight_index] = raw_values[base_index + weights_count + weight_index];
				track.out_tangents[keyframe_index][weight_index] = raw_values[base_index + weights_count * 2 + weight_index];
			}
		}
	}
	else
	{
		std::size_t const expected_count = keyframes_count * weights_count;
		if (raw_values.size() != expected_count)
		{
			throw load_error(context + ": weights output count does not match keyframe count * weights count");
		}

		for (std::size_t keyframe_index = 0; keyframe_index < keyframes_count; ++keyframe_index)
		{
			std::size_t const base_index = keyframe_index * weights_count;
			for (std::size_t weight_index = 0; weight_index < weights_count; ++weight_index)
			{
				track.values[keyframe_index][weight_index] = raw_values[base_index + weight_index];
			}
		}
	}
}

template<class T>
std::size_t find_upper_keyframe_index(std::vector<float> const &times, T time)
{
	return static_cast<std::size_t>(std::lower_bound(times.begin(), times.end(), static_cast<float>(time)) - times.begin());
}

template<class T>
T cubic_hermite(T const &p0, T const &p1, T const &m0, T const &m1, float k)
{
	float const k2 = k * k;
	float const k3 = k2 * k;
	float const h00 = 2.0f * k3 - 3.0f * k2 + 1.0f;
	float const h10 = k3 - 2.0f * k2 + k;
	float const h01 = -2.0f * k3 + 3.0f * k2;
	float const h11 = k3 - k2;
	return p0 * h00 + m0 * h10 + p1 * h01 + m1 * h11;
}

fastgltf::math::fvec3 sample_vec3_track(Vec3ChannelTrack const &track, float time)
{
	if (track.times.empty() || track.values.empty())
	{
		return fastgltf::math::fvec3(0.0f);
	}

	if (track.times.size() == 1 || track.values.size() == 1 || time <= track.times.front())
	{
		return track.values.front();
	}

	if (time >= track.times.back())
	{
		return track.values.back();
	}

	std::size_t const upper_index = find_upper_keyframe_index(track.times, time);
	if (upper_index == 0 || upper_index >= track.times.size())
	{
		return track.values.back();
	}

	std::size_t const lower_index = upper_index - 1;
	if (track.interpolation == fastgltf::AnimationInterpolation::Step)
	{
		return track.values[lower_index];
	}

	float const t0 = track.times[lower_index];
	float const t1 = track.times[upper_index];
	float const time_span = t1 - t0;
	if (std::abs(time_span) <= 1e-6f)
	{
		return track.values[lower_index];
		}

		float const k = (time - t0) / time_span;
		if (track.interpolation == fastgltf::AnimationInterpolation::CubicSpline)
		{
			fastgltf::math::fvec3 const m0 = track.out_tangents[lower_index] * time_span;
			fastgltf::math::fvec3 const m1 = track.in_tangents[upper_index] * time_span;
			return cubic_hermite(track.values[lower_index], track.values[upper_index], m0, m1, k);
		}
		return fastgltf::math::lerp(track.values[lower_index], track.values[upper_index], k);
	}

fastgltf::math::fquat sample_quat_track(QuatChannelTrack const &track, float time)
{
	if (track.times.empty() || track.values.empty())
	{
		return fastgltf::math::fquat(0.0f, 0.0f, 0.0f, 1.0f);
	}

	if (track.times.size() == 1 || track.values.size() == 1 || time <= track.times.front())
	{
		return fastgltf::math::normalize(track.values.front());
	}

	if (time >= track.times.back())
	{
		return fastgltf::math::normalize(track.values.back());
	}

	std::size_t const upper_index = find_upper_keyframe_index(track.times, time);
	if (upper_index == 0 || upper_index >= track.times.size())
	{
		return fastgltf::math::normalize(track.values.back());
	}

	std::size_t const lower_index = upper_index - 1;
	if (track.interpolation == fastgltf::AnimationInterpolation::Step)
	{
		return fastgltf::math::normalize(track.values[lower_index]);
	}

	float const t0 = track.times[lower_index];
	float const t1 = track.times[upper_index];
	float const time_span = t1 - t0;
	if (std::abs(time_span) <= 1e-6f)
	{
		return fastgltf::math::normalize(track.values[lower_index]);
		}

		float const k = (time - t0) / time_span;
		if (track.interpolation == fastgltf::AnimationInterpolation::CubicSpline)
		{
			fastgltf::math::fquat const m0 = track.out_tangents[lower_index] * time_span;
			fastgltf::math::fquat const m1 = track.in_tangents[upper_index] * time_span;
			return fastgltf::math::normalize(
				cubic_hermite(track.values[lower_index], track.values[upper_index], m0, m1, k));
		}
		return fastgltf::math::normalize(
			fastgltf::math::slerp(track.values[lower_index], track.values[upper_index], k));
	}

std::vector<float> sample_weights_track(WeightsChannelTrack const &track, float time)
{
	if (track.times.empty() || track.values.empty() || track.weights_count == 0)
	{
		return std::vector<float>();
	}

	if (track.times.size() == 1 || track.values.size() == 1 || time <= track.times.front())
	{
		return track.values.front();
	}

	if (time >= track.times.back())
	{
		return track.values.back();
	}

	std::size_t const upper_index = find_upper_keyframe_index(track.times, time);
	if (upper_index == 0 || upper_index >= track.times.size())
	{
		return track.values.back();
	}

	std::size_t const lower_index = upper_index - 1;
	if (track.interpolation == fastgltf::AnimationInterpolation::Step)
	{
		return track.values[lower_index];
	}

	float const t0 = track.times[lower_index];
	float const t1 = track.times[upper_index];
	float const time_span = t1 - t0;
	if (std::abs(time_span) <= 1e-6f)
	{
		return track.values[lower_index];
	}

	float const k = (time - t0) / time_span;
	std::vector<float> result(track.weights_count, 0.0f);
	for (std::size_t weight_index = 0; weight_index < track.weights_count; ++weight_index)
	{
		float const p0 = track.values[lower_index][weight_index];
		float const p1 = track.values[upper_index][weight_index];

		if (track.interpolation == fastgltf::AnimationInterpolation::CubicSpline &&
			!track.in_tangents.empty() &&
			!track.out_tangents.empty())
		{
			float const m0 = track.out_tangents[lower_index][weight_index] * time_span;
			float const m1 = track.in_tangents[upper_index][weight_index] * time_span;
			result[weight_index] = cubic_hermite(p0, p1, m0, m1, k);
		}
		else
		{
			result[weight_index] = p0 * (1.0f - k) + p1 * k;
		}
	}

	return result;
}

fastgltf::TRS node_default_trs(fastgltf::Node const &node)
{
	if (auto const *trs_ptr = std::get_if<fastgltf::TRS>(&node.transform))
	{
		return *trs_ptr;
	}

	fastgltf::TRS result;
	fastgltf::math::fvec3 scale;
	fastgltf::math::fquat rotation;
	fastgltf::math::fvec3 translation;
	fastgltf::math::decomposeTransformMatrix(
		fastgltf::getTransformMatrix(node),
		scale,
		rotation,
		translation);
	result.translation = translation;
	result.rotation = rotation;
	result.scale = scale;
	return result;
}

}

void GltfReader::build_animations()
{
	for (std::size_t animation_index = 0; animation_index < asset_.animations.size(); ++animation_index)
	{
		fastgltf::Animation const &gltf_animation = asset_.animations[animation_index];
		std::vector<NodeAnimationTracks> tracks(asset_.nodes.size(), NodeAnimationTracks());

		for (std::size_t channel_index = 0; channel_index < gltf_animation.channels.size(); ++channel_index)
		{
			fastgltf::AnimationChannel const &channel = gltf_animation.channels[channel_index];
			std::string const channel_context = make_animation_channel_context(
				gltf_animation, animation_index, channel, channel_index);

			if (!channel.nodeIndex)
			{
				throw load_error(channel_context + ": missing target node index");
			}

			std::size_t const node_index = static_cast<std::size_t>(*channel.nodeIndex);
			if (node_index >= tracks.size())
			{
				throw load_error(channel_context + ": target node index is out of bounds");
			}

			if (node_index >= imported_nodes_.size() || !imported_nodes_[node_index] ||
				node_index >= node_to_joint_.size() || node_to_joint_[node_index] < 0)
			{
				continue;
			}

			if (channel.samplerIndex >= gltf_animation.samplers.size())
			{
				throw load_error(channel_context + ": sampler index is out of bounds");
			}

			fastgltf::AnimationSampler const &gltf_sampler = gltf_animation.samplers[channel.samplerIndex];
			std::vector<float> times;
			read_animation_times(asset_, gltf_sampler.inputAccessor, times, channel_context);

			if (channel.path == fastgltf::AnimationPath::Translation)
			{
				Vec3ChannelTrack track;
				track.interpolation = gltf_sampler.interpolation;
				track.times = times;
				read_animation_vec3_values(asset_, gltf_sampler.outputAccessor,
					track.interpolation,
					track.times.size(),
					track,
					channel_context);

				tracks[node_index].translation = track;
				tracks[node_index].has_translation = true;
			}
			else if (channel.path == fastgltf::AnimationPath::Rotation)
			{
				QuatChannelTrack track;
				track.interpolation = gltf_sampler.interpolation;
				track.times = times;
				read_animation_quat_values(asset_, gltf_sampler.outputAccessor,
					track.interpolation,
					track.times.size(),
					track,
					channel_context);

				tracks[node_index].rotation = track;
				tracks[node_index].has_rotation = true;
			}
			else if (channel.path == fastgltf::AnimationPath::Scale)
			{
				Vec3ChannelTrack track;
				track.interpolation = gltf_sampler.interpolation;
				track.times = times;
				read_animation_vec3_values(asset_, gltf_sampler.outputAccessor,
					track.interpolation,
					track.times.size(),
					track,
					channel_context);

				tracks[node_index].scale = track;
				tracks[node_index].has_scale = true;
			}
			else if (channel.path == fastgltf::AnimationPath::Weights)
			{
				std::int64_t const mesh_index = model_.joints[std::size_t(node_to_joint_[node_index])].mesh_index;
				if (mesh_index < 0)
				{
					continue;
				}

				std::size_t const weights_count = model_.meshes[std::size_t(mesh_index)].morph_target_deltas.size();
				if (weights_count == 0)
				{
					throw load_error(channel_context + ": morph target weights animation targets a mesh without morph targets");
				}

				WeightsChannelTrack track;
				track.interpolation = gltf_sampler.interpolation;
				track.times = times;
				read_animation_weights_values(asset_, gltf_sampler.outputAccessor,
					track.interpolation,
					track.times.size(),
					weights_count,
					track,
					channel_context);

				tracks[node_index].weights = track;
				tracks[node_index].has_weights = true;
			}
			else
			{
				throw load_error(channel_context + ": unsupported animation path");
			}
		}

		SerializedAnimation animation;
		animation.name = gltf_animation.name.empty()
			? ("gltf_animation_" + std::to_string(animation_index))
			: std::string(gltf_animation.name.c_str());

		for (std::size_t node_index = 0; node_index < tracks.size(); ++node_index)
		{
			NodeAnimationTracks const &node_track = tracks[node_index];
			if (!node_track.has_translation && !node_track.has_rotation &&
				!node_track.has_scale && !node_track.has_weights)
			{
				continue;
			}

			std::int64_t const joint_index = node_to_joint_[node_index];
			if (joint_index < 0)
			{
				continue;
			}

			auto append_cubic_segment_samples = [&](std::vector<float> const &times,
				fastgltf::AnimationInterpolation interpolation,
				std::vector<float> &sample_times)
			{
				if (interpolation != fastgltf::AnimationInterpolation::CubicSpline || times.size() < 2)
				{
					return;
				}

				std::size_t constexpr cubic_substeps = 4;
				for (std::size_t i = 0; i + 1 < times.size(); ++i)
				{
					float const t0 = times[i];
					float const t1 = times[i + 1];
					float const time_span = t1 - t0;
					if (time_span <= 1e-6f)
					{
						continue;
					}

					for (std::size_t step = 1; step < cubic_substeps; ++step)
					{
						float const k = static_cast<float>(step) / static_cast<float>(cubic_substeps);
						sample_times.push_back(t0 + time_span * k);
					}
				}
			};

			if (node_track.has_translation || node_track.has_rotation || node_track.has_scale)
			{
				std::vector<float> sample_times;
				if (node_track.has_translation)
				{
					sample_times.insert(sample_times.end(),
						node_track.translation.times.begin(),
						node_track.translation.times.end());
					append_cubic_segment_samples(node_track.translation.times,
						node_track.translation.interpolation, sample_times);
				}
				if (node_track.has_rotation)
				{
					sample_times.insert(sample_times.end(),
						node_track.rotation.times.begin(),
						node_track.rotation.times.end());
					append_cubic_segment_samples(node_track.rotation.times,
						node_track.rotation.interpolation, sample_times);
				}
				if (node_track.has_scale)
				{
					sample_times.insert(sample_times.end(),
						node_track.scale.times.begin(),
						node_track.scale.times.end());
					append_cubic_segment_samples(node_track.scale.times,
						node_track.scale.interpolation, sample_times);
				}

				if (!sample_times.empty())
				{
					std::sort(sample_times.begin(), sample_times.end());
					sample_times.erase(
						std::unique(sample_times.begin(), sample_times.end(),
							[](float lhs, float rhs)
							{
								return std::abs(lhs - rhs) <= 1e-6f;
							}),
						sample_times.end());

					fastgltf::TRS const base_trs = node_default_trs(asset_.nodes[node_index]);
					SerializedSampler sampler;
					sampler.joint_index = joint_index;
					sampler.sid = "gltf_node_transform";

					for (float time : sample_times)
					{
							fastgltf::math::fvec3 translation = node_track.has_translation
							? sample_vec3_track(node_track.translation, time)
							: base_trs.translation;
						fastgltf::math::fquat rotation = node_track.has_rotation
							? sample_quat_track(node_track.rotation, time)
							: base_trs.rotation;
						fastgltf::math::fvec3 scale = node_track.has_scale
							? sample_vec3_track(node_track.scale, time)
							: base_trs.scale;

						// translation, rotation (x, y, z, w), scale: a .edge file's key
						sampler.keyframes.push_back(SerializedKeyframe{time, {translation[0], translation[1], translation[2], rotation[0], rotation[1], rotation[2],
							rotation[3], scale[0], scale[1], scale[2]}});
					}

					animation.samplers.push_back(sampler);
				}
			}

			if (node_track.has_weights)
			{
				std::int64_t const mesh_index = model_.joints[std::size_t(joint_index)].mesh_index;
				if (mesh_index >= 0)
				{
					std::vector<float> weight_sample_times = node_track.weights.times;
					append_cubic_segment_samples(node_track.weights.times,
						node_track.weights.interpolation,
						weight_sample_times);

					std::sort(weight_sample_times.begin(), weight_sample_times.end());
					weight_sample_times.erase(
						std::unique(weight_sample_times.begin(), weight_sample_times.end(),
							[](float lhs, float rhs)
							{
								return std::abs(lhs - rhs) <= 1e-6f;
							}),
						weight_sample_times.end());

					if (!weight_sample_times.empty())
					{
						SerializedWeightsSampler sampler;
						sampler.mesh_index = mesh_index;
						sampler.interpolation = static_cast<int>(node_track.weights.interpolation == fastgltf::AnimationInterpolation::Step
							? WeightsSampler::Interpolation::STEP : WeightsSampler::Interpolation::LINEAR);

						for (float time : weight_sample_times)
						{
								sampler.keyframes.push_back(SerializedKeyframe{time, sample_weights_track(node_track.weights, time)});
						}

						animation.weight_samplers.push_back(sampler);
					}
				}
			}
		}

		if (!animation.samplers.empty() || !animation.weight_samplers.empty())
		{
			model_.animations.push_back(std::move(animation));
		}
	}
}

}
