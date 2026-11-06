// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include "material.h"
#include "vertex_array.h"
#include "index_array.h"
#include "uber_draw_call.h"
#include "test_backends.h"
#include "test_vulkan_fixture.h"

BOOST_AUTO_TEST_SUITE(texture_blending)

BOOST_AUTO_TEST_CASE_TEMPLATE(blending_weights_in_vertex, F, edge::Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	edge::RendererPtr const renderer = fixture.renderer;

	std::string const src = edge::TestFixture::graphic_sources_path();


	edge::MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(src + "test/data/test_gray.png");
	material->set_diffuse_texture(src + "test/data/test_stones.jpeg", 3);

	std::shared_ptr<edge::VertexArrayPNTB> vertex_array = renderer->create_vertex_array<edge::VERTEX_PNTB>(4);

	{
		auto vertex_array_mapping = vertex_array->write(edge::Contents::DISCARD);
		edge::VERTEX_PNTB *v = vertex_array_mapping.as<edge::VERTEX_PNTB>().data();

		v->position.set(0, 1, 0);
		v->normal.set(0, 0, 1);
		v->texcoords.set(0, 1);
		v->texture_blend_weights.set(1, 0, 0, 0);
		++v;

		v->position.set(0, 0, 0);
		v->normal.set(0, 0, 1);
		v->texcoords.set(0, 0);
		v->texture_blend_weights.set(0, 1, 0, 0);
		++v;

		v->position.set(1, 0, 0);
		v->normal.set(0, 0, 1);
		v->texcoords.set(1, 0);
		v->texture_blend_weights.set(0, 0, 1, 0);
		++v;

		v->position.set(1, 1, 0);
		v->normal.set(0, 0, 1);
		v->texcoords.set(1, 1);
		v->texture_blend_weights.set(0, 0, 0, 1);
		++v;

		vertex_array_mapping.unmap();
	}

	std::shared_ptr<edge::IndexArray16> index_array = renderer->create_index_array<edge::INDEX_U16>(6);

	{
		auto index_array_mapping = index_array->write(edge::Contents::DISCARD);
		edge::INDEX_U16 *i = index_array_mapping.as<edge::INDEX_U16>().data();

		*i++ = 0;
		*i++ = 1;
		*i++ = 2;
		*i++ = 2;
		*i++ = 3;
		*i++ = 0;

		index_array_mapping.unmap();
	}

	std::shared_ptr<edge::Camera> camera(new edge::Camera());
	camera->set_orthographic_projection(2, 2 * rove::scalar(renderer->screen_height()) / renderer->screen_width(), 0, 2);
	camera->look_at(rove::vec<3>(0.5f, 0.5f, 1), rove::vec<3>(0.5f, 0.5f, 0), rove::vec<3>(0, 1, 0));

	rove::matrix<4,4> world_matrix;
	world_matrix.identity();

	edge::UberDrawCallPtr draw_call = renderer->create_uber_draw_call();

	draw_call->set_always_visible(true);
	draw_call->set_vertex_buffer(vertex_array->vertex_buffer());
	draw_call->set_index_buffer(index_array->index_buffer());
	draw_call->set_material(material);
	draw_call->set_triangles_count(2);
	draw_call->set_camera(camera);
	draw_call->commit();

	for (int i = 0; i < 20; ++i)
	{

			renderer->begin_frame();
			renderer->begin_pass(renderer->default_render_target());

			draw_call->execute();

			renderer->end_pass();
			renderer->finish_frame();
		}
}

BOOST_AUTO_TEST_CASE_TEMPLATE(blending_weights_in_texture, F, edge::Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	edge::RendererPtr const renderer = fixture.renderer;

	std::string const src = edge::TestFixture::graphic_sources_path();



	edge::MaterialPtr material = renderer->create_material();
	material->set_diffuse_texture(src + "test/data/test_gray.png");
	material->set_diffuse_texture(src + "test/data/test_stones.jpeg", 2);
	material->set_texture_blending_weights_texture(src + "test/data/test_texture_blending_weights.png");

	std::shared_ptr<edge::VertexArrayPNT> vertex_array = renderer->create_vertex_array<edge::VERTEX_PNT>(4);

	{
		auto vertex_array_mapping = vertex_array->write(edge::Contents::DISCARD);
		edge::VERTEX_PNT *v = vertex_array_mapping.as<edge::VERTEX_PNT>().data();

		v->position.set(0, 1, 0);
		v->normal.set(0, 0, 1);
		v->texcoords.set(0, 1);
		++v;

		v->position.set(0, 0, 0);
		v->normal.set(0, 0, 1);
		v->texcoords.set(0, 0);
		++v;

		v->position.set(1, 0, 0);
		v->normal.set(0, 0, 1);
		v->texcoords.set(1, 0);
		++v;

		v->position.set(1, 1, 0);
		v->normal.set(0, 0, 1);
		v->texcoords.set(1, 1);
		++v;

		vertex_array_mapping.unmap();
	}

	std::shared_ptr<edge::IndexArray16> index_array = renderer->create_index_array<edge::INDEX_U16>(6);

	{
		auto index_array_mapping = index_array->write(edge::Contents::DISCARD);
		edge::INDEX_U16 *i = index_array_mapping.as<edge::INDEX_U16>().data();

		*i++ = 0;
		*i++ = 1;
		*i++ = 2;
		*i++ = 2;
		*i++ = 3;
		*i++ = 0;

		index_array_mapping.unmap();
	}

	std::shared_ptr<edge::Camera> camera(new edge::Camera());
	camera->set_orthographic_projection(2, 2 * rove::scalar(renderer->screen_height()) / renderer->screen_width(), 0, 2);
	camera->look_at(rove::vec<3>(0.5f, 0.5f, 1), rove::vec<3>(0.5f, 0.5f, 0), rove::vec<3>(0, 1, 0));

	rove::matrix<4,4> world_matrix;
	world_matrix.identity();

	edge::UberDrawCallPtr draw_call = renderer->create_uber_draw_call();

	draw_call->set_always_visible(true);
	draw_call->set_vertex_buffer(vertex_array->vertex_buffer());
	draw_call->set_index_buffer(index_array->index_buffer());
	draw_call->set_material(material);
	draw_call->set_triangles_count(2);
	draw_call->set_camera(camera);
	draw_call->commit();

	for (int i = 0; i < 20; ++i)
	{

			renderer->begin_frame();
			renderer->begin_pass(renderer->default_render_target());

			draw_call->execute();

			renderer->end_pass();
			renderer->finish_frame();
		}
}

BOOST_AUTO_TEST_SUITE_END()
