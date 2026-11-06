// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <memory>
#include <rove/matrix.h>
#include "forward.h"

namespace edge {

/// A node of a model's skeleton: plain data (whoever builds the model fills it), and the walk that turns its local transforms into
/// world matrices.
struct Joint {
	/// A local transform, named (glTF's node transform is "gltf_node_transform"; a sampler finds its target by the name).
	struct Transform {
		std::string sid;
		rove::matrix<4,4> matrix;
	};

	using children_type = std::vector<JointWeakPtr>;

	std::size_t index;
	std::string id, name;
	JointWeakPtr parent;
	children_type children;
	/// Applied in order, after the parent's matrix.
	std::vector<Transform> transforms;
	/// The world matrix build_frame_transforms() made of them.
	mutable rove::matrix<4,4> matrix;
	/// The mesh the joint places, if any.
	std::weak_ptr<Mesh> mesh;

	explicit Joint(std::size_t i):
		index(i)
	{
	}

	/// The transform of @p sid; std::logic_error for none.
	rove::matrix<4,4> &transform_by_sid(std::string const &sid)
	{
		for (Transform &t : transforms)
			if (t.sid == sid) return t.matrix;
		throw std::logic_error("transform sid = " + sid + " not found");
	}

	/// The world matrix of this joint and those under it, from their transforms and their parents'.
	void build_frame_transforms()
	{
		matrix.identity();
		for (Transform const &t : transforms)
		{
			matrix *= t.matrix;
		}

		if (JointPtr p = parent.lock()) matrix = p->matrix * matrix;

		for (JointWeakPtr const &child_ptr : children)
		{
			if (JointPtr child = child_ptr.lock()) child->build_frame_transforms();
		}
	}
};

}
