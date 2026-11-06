// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <concepts>
#include <cstring>
#include <type_traits>

#include <cstdint>
#include <rove/vec.h>

#include "generic_vertex.h"

namespace edge
{

#pragma pack(push, 1)
struct VERTEX_P
{
	static GenericVertex::format_type const FORMAT = GenericVertex::FORMAT_POSITION;

	rove::vec<3> position;

	VERTEX_P &operator =(GenericVertex const &rhs)
	{
		position = rhs.position;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
	}
};

struct VERTEX_PT
{
	static GenericVertex::format_type const FORMAT = GenericVertex::FORMAT_POSITION | GenericVertex::FORMAT_TEXCOORDS;

	rove::vec<3> position;
	rove::vec<2> texcoords;

	VERTEX_PT &operator =(GenericVertex const &rhs)
	{
		position  = rhs.position;
		texcoords = rhs.texcoords;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
		if (format & GenericVertex::FORMAT_TEXCOORDS) texcoords = v.texcoords;
	}
};

struct VERTEX_PNT
{
	static GenericVertex::format_type const FORMAT = GenericVertex::FORMAT_POSITION |
		GenericVertex::FORMAT_NORMAL |
		GenericVertex::FORMAT_TEXCOORDS;

	rove::vec<3> position;
	rove::vec<3> normal;
	rove::vec<2> texcoords;

	VERTEX_PNT &operator =(GenericVertex const &rhs)
	{
		position  = rhs.position;
		normal    = rhs.normal;
		texcoords = rhs.texcoords;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
		if (format & GenericVertex::FORMAT_NORMAL) normal = v.normal;
		if (format & GenericVertex::FORMAT_TEXCOORDS) texcoords = v.texcoords;
	}
};

struct VERTEX_PNTB
{
	static GenericVertex::format_type const FORMAT = GenericVertex::FORMAT_POSITION |
		GenericVertex::FORMAT_NORMAL |
		GenericVertex::FORMAT_TEXCOORDS |
		GenericVertex::FORMAT_TEXTURE_BLEND_WEIGHTS;

	rove::vec<3> position;
	rove::vec<3> normal;
	rove::vec<2> texcoords;
	rove::vec<4> texture_blend_weights;

	VERTEX_PNTB &operator =(GenericVertex const &rhs)
	{
		position = rhs.position;
		normal = rhs.normal;
		texcoords = rhs.texcoords;
		texture_blend_weights = rhs.texture_blend_weights;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
		if (format & GenericVertex::FORMAT_NORMAL) normal = v.normal;
		if (format & GenericVertex::FORMAT_TEXCOORDS) texcoords = v.texcoords;
		if (format & GenericVertex::FORMAT_TEXTURE_BLEND_WEIGHTS) texture_blend_weights = v.texture_blend_weights;
	}
};

struct VERTEX_PTBNT
{
	static GenericVertex::format_type const FORMAT = GenericVertex::FORMAT_POSITION |
		GenericVertex::FORMAT_TANGENT |
		GenericVertex::FORMAT_BINORMAL |
		GenericVertex::FORMAT_NORMAL |
		GenericVertex::FORMAT_TEXCOORDS;

	rove::vec<3> position;
	rove::vec<3> tangent;
	rove::vec<3> binormal;
	rove::vec<3> normal;
	rove::vec<2> texcoords;

	VERTEX_PTBNT &operator =(GenericVertex const &rhs)
	{
		position  = rhs.position;
		tangent   = rhs.tangent;
		binormal  = rhs.binormal;
		normal    = rhs.normal;
		texcoords = rhs.texcoords;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
		if (format & GenericVertex::FORMAT_TANGENT) tangent = v.tangent;
		if (format & GenericVertex::FORMAT_BINORMAL) binormal = v.binormal;
		if (format & GenericVertex::FORMAT_NORMAL) normal = v.normal;
		if (format & GenericVertex::FORMAT_TEXCOORDS) texcoords = v.texcoords;
	}
};

/// VERTEX_PTBNT and a color (glTF COLOR_0) a shader may read: location 10, a vec4.
struct VERTEX_PTBNTC
{
	static constexpr GenericVertex::format_type FORMAT = VERTEX_PTBNT::FORMAT | GenericVertex::FORMAT_COLOR;

	rove::vec<3> position;
	rove::vec<3> tangent;
	rove::vec<3> binormal;
	rove::vec<3> normal;
	rove::vec<2> texcoords;
	rove::vec<4> color;

	VERTEX_PTBNTC &operator =(GenericVertex const &rhs)
	{
		position  = rhs.position;
		tangent   = rhs.tangent;
		binormal  = rhs.binormal;
		normal    = rhs.normal;
		texcoords = rhs.texcoords;
		color     = rhs.color;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
		if (format & GenericVertex::FORMAT_TANGENT) tangent = v.tangent;
		if (format & GenericVertex::FORMAT_BINORMAL) binormal = v.binormal;
		if (format & GenericVertex::FORMAT_NORMAL) normal = v.normal;
		if (format & GenericVertex::FORMAT_TEXCOORDS) texcoords = v.texcoords;
		if (format & GenericVertex::FORMAT_COLOR) color = v.color;
	}
};

struct VERTEX_PNJWT
{
	static GenericVertex::format_type const FORMAT = GenericVertex::FORMAT_POSITION |
		GenericVertex::FORMAT_NORMAL |
		GenericVertex::FORMAT_JOINT_INDICES |
		GenericVertex::FORMAT_JOINT_WEIGHTS |
		GenericVertex::FORMAT_TEXCOORDS;

	rove::vec<3> position;
	rove::vec<3> normal;
	rove::vec<8, std::uint8_t> joints;
	rove::vec<8> weights;
	rove::vec<2> texcoords;

	VERTEX_PNJWT &operator =(GenericVertex const &rhs)
	{
		position  = rhs.position;
		normal    = rhs.normal;
		joints    = rhs.joints;
		weights   = rhs.weights;
		texcoords = rhs.texcoords;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
		if (format & GenericVertex::FORMAT_NORMAL) normal = v.normal;
		if (format & GenericVertex::FORMAT_JOINT_INDICES) joints = v.joints;
		if (format & GenericVertex::FORMAT_JOINT_WEIGHTS) weights = v.weights;
		if (format & GenericVertex::FORMAT_TEXCOORDS) texcoords = v.texcoords;
	}
};

struct VERTEX_PTBNJWT
{
	static GenericVertex::format_type const FORMAT = GenericVertex::FORMAT_POSITION |
		GenericVertex::FORMAT_TANGENT |
		GenericVertex::FORMAT_BINORMAL |
		GenericVertex::FORMAT_NORMAL |
		GenericVertex::FORMAT_JOINT_INDICES |
		GenericVertex::FORMAT_JOINT_WEIGHTS |
		GenericVertex::FORMAT_TEXCOORDS;

	rove::vec<3> position;
	rove::vec<3> tangent;
	rove::vec<3> binormal;
	rove::vec<3> normal;
	rove::vec<8, std::uint8_t> joints;
	rove::vec<8> weights;
	rove::vec<2> texcoords;

	VERTEX_PTBNJWT &operator =(GenericVertex const &v)
	{
		position  = v.position;
		tangent   = v.tangent;
		binormal  = v.binormal;
		normal    = v.normal;
		joints    = v.joints;
		weights   = v.weights;
		texcoords = v.texcoords;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
		if (format & GenericVertex::FORMAT_TANGENT) tangent = v.tangent;
		if (format & GenericVertex::FORMAT_BINORMAL) binormal = v.binormal;
		if (format & GenericVertex::FORMAT_NORMAL) normal = v.normal;
		if (format & GenericVertex::FORMAT_JOINT_INDICES) joints = v.joints;
		if (format & GenericVertex::FORMAT_JOINT_WEIGHTS) weights = v.weights;
		if (format & GenericVertex::FORMAT_TEXCOORDS) texcoords = v.texcoords;
	}
};

/// VERTEX_PTBNJWT and a color (glTF COLOR_0) a shader may read: location 10, a vec4 -- a skinned VERTEX_PTBNTC.
struct VERTEX_PTBNJWTC
{
	static GenericVertex::format_type const FORMAT = VERTEX_PTBNJWT::FORMAT | GenericVertex::FORMAT_COLOR;

	rove::vec<3> position;
	rove::vec<3> tangent;
	rove::vec<3> binormal;
	rove::vec<3> normal;
	rove::vec<8, std::uint8_t> joints;
	rove::vec<8> weights;
	rove::vec<2> texcoords;
	rove::vec<4> color;

	VERTEX_PTBNJWTC &operator =(GenericVertex const &v)
	{
		position  = v.position;
		tangent   = v.tangent;
		binormal  = v.binormal;
		normal    = v.normal;
		joints    = v.joints;
		weights   = v.weights;
		texcoords = v.texcoords;
		color     = v.color;
		return *this;
	}

	void assign(GenericVertex const &v, GenericVertex::format_type format)
	{
		if (format & GenericVertex::FORMAT_POSITION) position = v.position;
		if (format & GenericVertex::FORMAT_TANGENT) tangent = v.tangent;
		if (format & GenericVertex::FORMAT_BINORMAL) binormal = v.binormal;
		if (format & GenericVertex::FORMAT_NORMAL) normal = v.normal;
		if (format & GenericVertex::FORMAT_JOINT_INDICES) joints = v.joints;
		if (format & GenericVertex::FORMAT_JOINT_WEIGHTS) weights = v.weights;
		if (format & GenericVertex::FORMAT_TEXCOORDS) texcoords = v.texcoords;
		if (format & GenericVertex::FORMAT_COLOR) color = v.color;
	}
};
#pragma pack(pop)

/// A vertex as the GPU reads it (one of the VERTEX_* above): its FORMAT bits name its components, which are GenericVertex's members of
/// the same names, and it is made of a GenericVertex whole (=) or of the components a format names (assign()).
template <class V>
concept vertex_layout = std::is_trivially_copyable_v<V> && requires(V v, GenericVertex const &g, GenericVertex::format_type f) {
	{ V::FORMAT } -> std::convertible_to<GenericVertex::format_type>;
	v.position;
	v = g;
	v.assign(g, f);
};

/// A vertex a skin moves: one with joint indices and weights.
template <class V>
concept skinned_vertex_layout = vertex_layout<V> && requires(V v) {
	v.joints;
	v.weights;
};

/// The GenericVertex of @p vertex: its components, and zero for those its type has not.
template <class V>
inline GenericVertex to_generic_vertex(V const &vertex)
{
	GenericVertex result;
	std::memset(&result, 0, sizeof(result));

	if constexpr ((V::FORMAT & GenericVertex::FORMAT_POSITION) != 0)
	{
		result.position = vertex.position;
	}
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_TANGENT) != 0)
	{
		result.tangent = vertex.tangent;
	}
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_BINORMAL) != 0)
	{
		result.binormal = vertex.binormal;
	}
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_NORMAL) != 0)
	{
		result.normal = vertex.normal;
	}
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_JOINT_INDICES) != 0)
	{
		result.joints = vertex.joints;
	}
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_JOINT_WEIGHTS) != 0)
	{
		result.weights = vertex.weights;
	}
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_TEXCOORDS) != 0)
	{
		result.texcoords = vertex.texcoords;
	}
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_TEXTURE_BLEND_WEIGHTS) != 0)
	{
		result.texture_blend_weights = vertex.texture_blend_weights;
	}
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_COLOR) != 0)
	{
		result.color = vertex.color;
	}

	return result;
}

}
