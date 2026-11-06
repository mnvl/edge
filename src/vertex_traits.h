// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include "generic_vertex.h"

namespace edge
{

/// One attribute of a vertex as the GPU reads it: where it lies in the vertex, what it is made of, and what it is.
struct VertexElement {
	/// What the attribute is made of.
	enum class Type : std::uint8_t {
		FLOAT2 = 1,
		FLOAT3 = 2,
		FLOAT4 = 3,
		UBYTE4 = 5,      // four unsigned bytes read as integers (a shader's uvec4: the joints)
	};

	/// What the attribute is; with its usage index it says where a shader reads it (shader::attribute_location(): position 0,
	/// tangent 1, binormal 2, normal 3, joints 4-5, weights 6-7, texture coordinates 8-9, colour 10).
	enum class Usage : std::uint8_t {
		POSITION = 0,
		BLEND_WEIGHT = 1,
		BLEND_INDICES = 2,
		NORMAL = 3,
		TEXCOORD = 5,
		TANGENT = 6,
		BINORMAL = 7,
		COLOR = 10,
	};

	std::uint16_t offset = 0;
	Type type = Type::FLOAT3;
	Usage usage = Usage::POSITION;
	std::uint8_t usage_index = 0;

	bool operator==(VertexElement const &) const = default;
};

/// What a vertex type is to a renderer: its format bits, its size and its elements.  One for each vertex type (of<V>()), never made
/// otherwise.
class VertexTraits {
public:
	virtual ~VertexTraits() = default;

	virtual GenericVertex::format_type format() const = 0;
	virtual std::size_t size() const = 0;
	/// The vertex's elements, in the order of its members.
	virtual std::span<VertexElement const> elements() const = 0;
	/// Whether the vertex has the element of @p usage and @p usage_index.
	bool has(VertexElement::Usage usage, std::uint8_t usage_index) const;

	/// The traits of vertex type @p V (one of vertex.h's).
	template<class V>
	static VertexTraits const *of();
};

}
