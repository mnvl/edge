// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vector>
#include <rove/matrix.h>
#include "draw_call.h"

namespace edge
{

/// A draw call of the built-in shader's make-up: texture blending by a weights texture or the vertex's second set of coordinates, and a
/// skin by its joint matrices.
class UberDrawCall: public DrawCall {
public:
	UberDrawCall(std::shared_ptr<Renderer> const &renderer);
	~UberDrawCall() override;

	/// The skin's joint matrices; std::length_error for more than MAX_JOINT_MATRICES.
	void set_joint_matrix_array(std::vector<rove::matrix<4,4>> const &joint_matrix_array);
	/// The same of a list shared, not copied (a model's mesh hands one to each call it makes); none: none set.
	void set_joint_matrix_array(SharedMatrices const &joint_matrix_array);

	bool is_uber() const override { return true; }
	/// The joint matrices set; none (nullptr) for none set.
	std::vector<rove::matrix<4,4>> const *joint_matrices() const override;

protected:
	SharedMatrices joint_matrix_array_;            // none, or empty: none set
};

}
