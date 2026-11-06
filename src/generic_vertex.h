// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <string>

#include <rove/vec.h>

namespace edge
{

struct GenericVertex {
	using format_type = unsigned int;

	static format_type const FORMAT_POSITION = 0x0001u;
	static format_type const FORMAT_TANGENT = 0x0002u;
	static format_type const FORMAT_BINORMAL = 0x0004u;
	static format_type const FORMAT_NORMAL = 0x0008u;
	static format_type const FORMAT_JOINT_INDICES = 0x0010u;
	static format_type const FORMAT_JOINT_WEIGHTS = 0x0020u;
	static format_type const FORMAT_TEXCOORDS = 0x0040u;
	static format_type const FORMAT_TEXTURE_BLEND_WEIGHTS = 0x0080u;
	static format_type const FORMAT_COLOR = 0x0100u;

	rove::vec<3> position;
	rove::vec<3> tangent;
	rove::vec<3> binormal;
	rove::vec<3> normal;
	rove::vec<8, unsigned> joints;
	rove::vec<8> weights;
	rove::vec<2> texcoords;
	rove::vec<4> texture_blend_weights;
	rove::vec<4> color;     // glTF COLOR_0: linear RGB and an alpha, which may be above 1 (it is the application's to read)
};

inline std::string format_components_string(unsigned format)
{
	std::string s;
	auto append = [&](unsigned bit, char const *name) {
		if (format & bit) { if (!s.empty()) s += '|'; s += name; }
	};
	append(GenericVertex::FORMAT_POSITION, "position");
	append(GenericVertex::FORMAT_TANGENT, "tangent");
	append(GenericVertex::FORMAT_BINORMAL, "binormal");
	append(GenericVertex::FORMAT_NORMAL, "normal");
	append(GenericVertex::FORMAT_JOINT_INDICES, "joint_indices");
	append(GenericVertex::FORMAT_JOINT_WEIGHTS, "joint_weights");
	append(GenericVertex::FORMAT_TEXCOORDS, "texcoords");
	append(GenericVertex::FORMAT_TEXTURE_BLEND_WEIGHTS, "texture_blend_weights");
	append(GenericVertex::FORMAT_COLOR, "color");
	return s.empty() ? "none" : s;
}

}

