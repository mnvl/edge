// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include "camera.h"
#include "draw_call.h"
#include "dynamic_mesh.h"
#include "material.h"
#include "renderer.h"
#include "scheduler.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "debug_lines.h"

namespace edge
{

DebugLines::DebugLines(RendererPtr const &renderer, float width):
	renderer_(renderer),
	width_(width)
{
	if (!renderer_)
		throw std::invalid_argument("debug lines without a renderer");
	if (!(width_ > 0))
		throw std::invalid_argument("debug lines " + std::to_string(width_) + " wide");
}

DebugLines::~DebugLines() = default;

void DebugLines::line(rove::vec<3> const &a, rove::vec<3> const &b, rove::vec<4> const &colour)
{
	if (a == b)
		return;
	colours_[colour].segments.push_back({a, b});
}

void DebugLines::box(rove::aabb<3> const &bounds, rove::vec<4> const &colour)
{
	box(rove::obb<3>(bounds), colour);
}

void DebugLines::box(rove::obb<3> const &bounds, rove::vec<4> const &colour)
{
	// corner k has the box's edge along axis i where bit i of k is set
	rove::vec<3> corners[8];
	for (int k = 0; k < 8; ++k)
		corners[k] = bounds.origin + bounds.tangent * float(k & 1) + bounds.normal * float((k >> 1) & 1) + bounds.binormal * float((k >> 2) & 1);
	for (int k = 0; k < 8; ++k)
		for (int bit = 1; bit < 8; bit <<= 1)
			if (!(k & bit))
				line(corners[k], corners[k | bit], colour);
}

void DebugLines::sphere(rove::vec<3> const &centre, float radius, rove::vec<4> const &colour, int segments)
{
	if (segments < 3)
		throw std::invalid_argument("a debug sphere of " + std::to_string(segments) + " segments a circle");
	rove::vec<3> const axes[3] = {rove::vec<3>(1, 0, 0), rove::vec<3>(0, 1, 0), rove::vec<3>(0, 0, 1)};
	for (int circle = 0; circle < 3; ++circle)
	{
		rove::vec<3> const &u = axes[(circle + 1) % 3], &v = axes[(circle + 2) % 3];
		auto const point = [&](int k)
		{
			float const angle = 2 * std::numbers::pi_v<float> * float(k) / float(segments);
			return centre + (u * std::cos(angle) + v * std::sin(angle)) * radius;
		};
		for (int k = 0; k < segments; ++k)
			line(point(k), point(k + 1), colour);
	}
}

void DebugLines::axes(rove::matrix<4, 4> const &frame, float length)
{
	rove::vec<3> const origin = rove::transform_point(frame, rove::vec<3>(0, 0, 0));
	line(origin, rove::transform_point(frame, rove::vec<3>(length, 0, 0)), rove::vec<4>(1, 0, 0, 1));
	line(origin, rove::transform_point(frame, rove::vec<3>(0, length, 0)), rove::vec<4>(0, 1, 0, 1));
	line(origin, rove::transform_point(frame, rove::vec<3>(0, 0, length)), rove::vec<4>(0, 0, 1, 1));
}

std::size_t DebugLines::segments() const
{
	std::size_t count = 0;
	for (auto const &[colour, entry] : colours_)
		count += entry.segments.size();
	return count;
}

void DebugLines::clear()
{
	for (auto &[colour, entry] : colours_)
		entry.segments.clear();
}

void DebugLines::draw(Scheduler &scheduler, CameraPtr const &camera)
{
	if (!camera)
		throw std::invalid_argument("debug lines drawn without a camera");
	rove::vec<3> const eye = camera->eye_point(), forward = camera->view_dir(), up = camera->up_dir();
	for (auto &[colour, entry] : colours_)
	{
		if (entry.segments.empty())
			continue;
		if (!entry.mesh)
		{
			MaterialPtr const material = renderer_->create_material();
			material->set_cull_mode(Material::CullMode::NONE);
			material->set_base_color_factor(colour);
			if (colour.w < 1)
			{
				material->set_alpha_blend_enabled(true);
				material->set_src_blend_mode(Material::BlendMode::SRC_ALPHA);
				material->set_dest_blend_mode(Material::BlendMode::INV_SRC_ALPHA);
				material->set_zwrite_enabled(false);
			}
			entry.mesh = std::make_shared<DynamicMesh>(renderer_, material, VertexTraits::of<VERTEX_PTBNT>());
		}
		std::vector<VERTEX_PTBNT> vertices;
		std::vector<std::uint32_t> indices;
		vertices.reserve(entry.segments.size() * 4);
		indices.reserve(entry.segments.size() * 6);
		for (Segment const &s : entry.segments)
		{
			// across the segment and square to the line of sight to it (an orthographic camera's sight is its forward)
			rove::vec<3> const along = s.b - s.a, middle = (s.a + s.b) * 0.5f;
			rove::vec<3> sight = middle - eye;
			if (rove::dot(sight, forward) <= 0 || camera->projection_matrix().ij[3][3] != 0)
				sight = forward;          // (ij[3][3] is 1 for an orthographic projection, 0 for a perspective one)
			rove::vec<3> across = rove::cross(along, sight);
			if (across.length_sq() <= rove::scalar_traits<float>::zero_length_sq * along.length_sq() * sight.length_sq())
				across = rove::cross(along, up);              // a segment along the line of sight: any side will do
			if (across.length_sq() == 0)
				across = rove::cross(along, rove::vec<3>(1, 0, 0)).length_sq() > 0 ? rove::cross(along, rove::vec<3>(1, 0, 0))
					: rove::cross(along, rove::vec<3>(0, 1, 0));
			across = rove::direction(across) * (width_ * 0.5f);
			std::uint32_t const first = static_cast<std::uint32_t>(vertices.size());
			for (rove::vec<3> const &p : {s.a - across, s.a + across, s.b + across, s.b - across})
			{
				VERTEX_PTBNT v;
				v.position = p;
				v.tangent = rove::direction(along);
				v.normal = rove::direction(-sight);
				v.binormal = rove::cross(v.normal, v.tangent);
				v.texcoords = rove::vec<2>(0, 0);
				vertices.push_back(v);
			}
			for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
				indices.push_back(first + k);
		}
		entry.mesh->upload(std::span<VERTEX_PTBNT const>(vertices), std::span<std::uint32_t const>(indices));
		entry.mesh->draw_call()->set_priority(priority_);
		entry.mesh->draw(scheduler, camera);
		entry.segments.clear();
	}
}

}
