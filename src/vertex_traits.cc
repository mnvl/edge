// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "vertex_traits.h"
#include "vertex.h"
#include <cstddef>
#include <type_traits>
#include <vector>

namespace edge
{

bool VertexTraits::has(VertexElement::Usage usage, std::uint8_t usage_index) const
{
	for (VertexElement const &element : elements())
	{
		if (element.usage == usage && element.usage_index == usage_index)
		{
			return true;
		}
	}
	return false;
}

namespace
{

/// The elements a member of type M is: a vector of 2 to 4 floats one, of 4 bytes one, and one of 8 two of 4 (usage indices 0, 1).
template <class M> struct member_elements;

template <int N, class T, class S> struct member_elements<rove::vec<N, T, S>>
{
	static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::uint8_t>, "a vertex element is floats or bytes");
	static_assert(std::is_same_v<T, float> ? N >= 2 && N <= 4 || N == 8 : N == 4 || N == 8, "no vertex element is of this width");

	static constexpr int count = N == 8 ? 2 : 1;
	static constexpr std::size_t stride = N == 8 ? 4 * sizeof(T) : 0;
	static constexpr VertexElement::Type type = std::is_same_v<T, std::uint8_t> ? VertexElement::Type::UBYTE4
		: N == 2 ? VertexElement::Type::FLOAT2 : N == 3 ? VertexElement::Type::FLOAT3 : VertexElement::Type::FLOAT4;
};

template <class M>
void add(std::vector<VertexElement> &elements, std::size_t offset, VertexElement::Usage usage, std::uint8_t first_index = 0)
{
	using E = member_elements<M>;
	for (int i = 0; i < E::count; ++i)
	{
		elements.push_back({static_cast<std::uint16_t>(offset + i * E::stride), E::type, usage,
			static_cast<std::uint8_t>(first_index + i)});
	}
}

/// The elements of vertex type V, from the members it has, in the order vertex.h declares them.
template <class V>
std::vector<VertexElement> elements_of()
{
	using Usage = VertexElement::Usage;
	std::vector<VertexElement> elements;
	if constexpr (requires { V::position; }) add<decltype(V::position)>(elements, offsetof(V, position), Usage::POSITION);
	if constexpr (requires { V::tangent; }) add<decltype(V::tangent)>(elements, offsetof(V, tangent), Usage::TANGENT);
	if constexpr (requires { V::binormal; }) add<decltype(V::binormal)>(elements, offsetof(V, binormal), Usage::BINORMAL);
	if constexpr (requires { V::normal; }) add<decltype(V::normal)>(elements, offsetof(V, normal), Usage::NORMAL);
	if constexpr (requires { V::joints; }) add<decltype(V::joints)>(elements, offsetof(V, joints), Usage::BLEND_INDICES);
	if constexpr (requires { V::weights; }) add<decltype(V::weights)>(elements, offsetof(V, weights), Usage::BLEND_WEIGHT);
	if constexpr (requires { V::texcoords; }) add<decltype(V::texcoords)>(elements, offsetof(V, texcoords), Usage::TEXCOORD);
	if constexpr (requires { V::texture_blend_weights; })
		add<decltype(V::texture_blend_weights)>(elements, offsetof(V, texture_blend_weights), Usage::TEXCOORD, 1);
	if constexpr (requires { V::color; }) add<decltype(V::color)>(elements, offsetof(V, color), Usage::COLOR);
	return elements;
}

template <class V>
class SpecificVertexTraits: public VertexTraits {
public:
	GenericVertex::format_type format() const override { return V::FORMAT; }
	std::size_t size() const override { return sizeof(V); }
	std::span<VertexElement const> elements() const override { return elements_; }

	static SpecificVertexTraits const &instance()
	{
		static SpecificVertexTraits const traits;
		return traits;
	}

private:
	std::vector<VertexElement> const elements_ = elements_of<V>();
};

}

template<class V>
VertexTraits const *VertexTraits::of()
{
	return &SpecificVertexTraits<V>::instance();
}

template VertexTraits const *VertexTraits::of<VERTEX_P>();
template VertexTraits const *VertexTraits::of<VERTEX_PT>();
template VertexTraits const *VertexTraits::of<VERTEX_PNT>();
template VertexTraits const *VertexTraits::of<VERTEX_PNTB>();
template VertexTraits const *VertexTraits::of<VERTEX_PTBNT>();
template VertexTraits const *VertexTraits::of<VERTEX_PTBNTC>();
template VertexTraits const *VertexTraits::of<VERTEX_PNJWT>();
template VertexTraits const *VertexTraits::of<VERTEX_PTBNJWT>();
template VertexTraits const *VertexTraits::of<VERTEX_PTBNJWTC>();

}
