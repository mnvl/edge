// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <stdexcept>
#include <string>
#include "mesh.h"
#include "renderer.h"
#include "uber_draw_call.h"

namespace edge
{

UberDrawCall::UberDrawCall(RendererPtr const &renderer):
	DrawCall(renderer)
{
}

UberDrawCall::~UberDrawCall() = default;

void UberDrawCall::set_joint_matrix_array(std::vector<rove::matrix<4,4>> const &joint_matrix_array)
{
	if (joint_matrix_array.size() > MAX_JOINT_MATRICES)
	{
		throw std::length_error(std::to_string(joint_matrix_array.size()) + " joint matrices, a draw has room for " +
			std::to_string(MAX_JOINT_MATRICES));
	}
	joint_matrix_array_ = std::make_shared<std::vector<rove::matrix<4,4>> const>(joint_matrix_array);
}

void UberDrawCall::set_joint_matrix_array(Placement::Matrices const &joint_matrix_array)
{
	if (joint_matrix_array && joint_matrix_array->size() > MAX_JOINT_MATRICES)
	{
		throw std::length_error(std::to_string(joint_matrix_array->size()) + " joint matrices, a draw has room for " +
			std::to_string(MAX_JOINT_MATRICES));
	}
	joint_matrix_array_ = joint_matrix_array;
}

void UberDrawCall::set_previous_joint_matrix_array(std::vector<rove::matrix<4,4>> const &joint_matrix_array)
{
	if (joint_matrix_array.size() > MAX_JOINT_MATRICES)
	{
		throw std::length_error(std::to_string(joint_matrix_array.size()) + " previous joint matrices, a draw has room for " +
			std::to_string(MAX_JOINT_MATRICES));
	}
	give_previous_joint_matrices(joint_matrix_array);
}

std::vector<rove::matrix<4,4>> const *UberDrawCall::joint_matrices() const
{
	return joint_matrix_array_ && !joint_matrix_array_->empty() ? joint_matrix_array_.get() : nullptr;
}

Placement::Matrices UberDrawCall::shared_joint_matrices() const
{
	return joint_matrix_array_ && !joint_matrix_array_->empty() ? joint_matrix_array_ : nullptr;
}

}
