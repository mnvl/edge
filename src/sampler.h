// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <rove/vec.h>
#include <rove/quaternion.h>
#include <rove/matrix.h>
#include "forward.h"
#include "joint.h"

namespace edge
{

class Sampler {
public:
	/// A keyframe: the transform at @p time as glTF has it, translation * rotation * scale.
	struct Key {
		rove::scalar time;
		rove::vec<3> translation;
		rove::quaternion<> rotation;
		rove::vec<3> scale;
	};

private:
	struct Sample {
		Key key;
		rove::matrix<4,4> transform;                        // the key's, composed
		rove::quaternion_slerper<> rotation_interpolator;   // from this key's rotation to the next one's
	};

	using samples_type = std::vector<Sample>;

	std::string sid_;
	JointWeakPtr joint_;
	/// The keyframes, shared by every copy of the sampler (an instance of a model holds its clips' keys once with every other instance);
	/// add_sample copies them first when they are shared.
	std::shared_ptr<samples_type> samples_ = std::make_shared<samples_type>();

public:
	void set_target(JointPtr joint_ptr, std::string const &sid) { joint_ = joint_ptr; sid_ = sid; }

	/// Appends a keyframe; std::invalid_argument for one not after the last.
	void add_sample(Key const &key);
	/// The transform at @p time: translation and scale interpolated linearly, rotation spherically; before the first key the first,
	/// after the last the last.  std::logic_error for a sampler without keys.
	rove::matrix<4,4> get(rove::scalar time) const;

	void apply(rove::scalar t) const {
		if (JointPtr joint_ptr = joint_.lock())
			joint_ptr->transform_by_sid(sid_) = get(t);
	}

	/// How long the clip runs: from time 0 (before the first key, its pose) to the last key; 0 without keys.
	rove::scalar duration() const {
		if (samples_->empty()) return 0;
		return samples_->back().key.time;
	}

	/// The transform this sampler writes: the joint's transform with this sid.
	std::string const &sid() const { return sid_; }
	/// The keyframes, in time order.
	std::size_t samples_count() const { return samples_->size(); }
	Key const &sample(std::size_t index) const { return samples_->at(index).key; }
	// stores joint as weak ptr
	JointPtr joint() const { return joint_.lock(); }
	void set_joint(JointPtr const &joint) { joint_ = joint; }
};

}
