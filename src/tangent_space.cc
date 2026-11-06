// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "tangent_space.h"

#include <cmath>

namespace edge
{

void generate_tangent_space(std::vector<GenericVertex> &vertices,
	std::vector<unsigned> const &indices,
	GenericVertex::format_type &vertex_format)
{
	if ((vertex_format & GenericVertex::FORMAT_NORMAL) == 0 ||
		(vertex_format & GenericVertex::FORMAT_TEXCOORDS) == 0)
	{
		return;
	}

	if ((vertex_format & GenericVertex::FORMAT_TANGENT) != 0 &&
		(vertex_format & GenericVertex::FORMAT_BINORMAL) != 0)
	{
		return;
	}

	float const epsilon = 1e-8f;
	std::vector<rove::vec<3>> tangent_sum(vertices.size(), rove::vec<3>(0.0f, 0.0f, 0.0f));
	std::vector<rove::vec<3>> bitangent_sum(vertices.size(), rove::vec<3>(0.0f, 0.0f, 0.0f));

	for (std::size_t tri_index = 0; tri_index + 2 < indices.size(); tri_index += 3)
	{
		unsigned const i0 = indices[tri_index + 0];
		unsigned const i1 = indices[tri_index + 1];
		unsigned const i2 = indices[tri_index + 2];
		if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
		{
			continue;
		}

		rove::vec<3> const &p0 = vertices[i0].position;
		rove::vec<3> const &p1 = vertices[i1].position;
		rove::vec<3> const &p2 = vertices[i2].position;
		rove::vec<2> const &uv0 = vertices[i0].texcoords;
		rove::vec<2> const &uv1 = vertices[i1].texcoords;
		rove::vec<2> const &uv2 = vertices[i2].texcoords;

		rove::vec<3> const edge1 = p1 - p0;
		rove::vec<3> const edge2 = p2 - p0;
		rove::vec<2> const duv1 = uv1 - uv0;
		rove::vec<2> const duv2 = uv2 - uv0;

		float const determinant = duv1.x * duv2.y - duv2.x * duv1.y;
		if (std::abs(determinant) <= epsilon)
		{
			continue;
		}

		float const inv_determinant = 1.0f / determinant;
		rove::vec<3> const tangent = (edge1 * duv2.y - edge2 * duv1.y) * inv_determinant;
		rove::vec<3> const bitangent = (edge2 * duv1.x - edge1 * duv2.x) * inv_determinant;

		tangent_sum[i0] += tangent;
		tangent_sum[i1] += tangent;
		tangent_sum[i2] += tangent;
		bitangent_sum[i0] += bitangent;
		bitangent_sum[i1] += bitangent;
		bitangent_sum[i2] += bitangent;
	}

	bool generated_tangent_space = false;
	for (std::size_t vertex_index = 0; vertex_index < vertices.size(); ++vertex_index)
	{
		rove::vec<3> const &normal = vertices[vertex_index].normal;
		if (normal.length_sq() <= epsilon)
		{
			continue;
		}

		rove::vec<3> tangent = tangent_sum[vertex_index];
		tangent -= normal * rove::dot(normal, tangent);

		if (tangent.length_sq() <= epsilon)
		{
			rove::vec<3> const fallback_axis =
				std::abs(normal.z) < 0.999f
					? rove::vec<3>(0.0f, 0.0f, 1.0f)
					: rove::vec<3>(0.0f, 1.0f, 0.0f);

			tangent = rove::cross(fallback_axis, normal);
			tangent -= normal * rove::dot(normal, tangent);

			if (tangent.length_sq() <= epsilon)
			{
				continue;
			}
		}

		tangent.normalize();
		rove::vec<3> bitangent = rove::cross(normal, tangent);
		if (bitangent.length_sq() <= epsilon)
		{
			continue;
		}

		if (bitangent_sum[vertex_index].length_sq() > epsilon &&
			rove::dot(bitangent, bitangent_sum[vertex_index]) < 0.0f)
		{
			bitangent = bitangent * -1.0f;
		}

		bitangent.normalize();

		vertices[vertex_index].tangent = tangent;
		vertices[vertex_index].binormal = bitangent;
		generated_tangent_space = true;
	}

	if (generated_tangent_space)
	{
		vertex_format |= GenericVertex::FORMAT_TANGENT;
		vertex_format |= GenericVertex::FORMAT_BINORMAL;
	}
}

}
