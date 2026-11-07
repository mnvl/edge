// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// ImGuiRenderer is a header, compiled against the program's ImGui: a second file of one program that includes it links with the
// first (test_imgui_renderer.cc), and what it compiles behaves.

#include <memory>
#include <stdexcept>

#include <boost/test/unit_test.hpp>
#include <imgui.h>

#include "imgui_renderer.h"
#include "null_renderer.h"

BOOST_AUTO_TEST_SUITE(imgui_renderer_header)

BOOST_AUTO_TEST_CASE(no_renderer_is_refused)
{
	BOOST_CHECK_THROW(edge::ImGuiRenderer(nullptr), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(no_current_context_is_refused)
{
	ImGuiContext *const before = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(nullptr);
	edge::RendererPtr renderer = std::make_shared<edge::NullRenderer>(64, 64);
	BOOST_CHECK_THROW(edge::ImGuiRenderer{renderer}, std::logic_error);
	ImGui::SetCurrentContext(before);
}

BOOST_AUTO_TEST_CASE(it_draws_through_the_program_s_imgui)
{
	ImGuiContext *const before = ImGui::GetCurrentContext();
	ImGuiContext *const context = ImGui::CreateContext();
	ImGui::SetCurrentContext(context);
	{
		edge::RendererPtr renderer = std::make_shared<edge::NullRenderer>(64, 64);
		edge::ImGuiRenderer ui(renderer);
		BOOST_TEST((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0);
	}
	BOOST_TEST((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) == 0); // (the destructor gave the context back)
	ImGui::DestroyContext(context);
	ImGui::SetCurrentContext(before);
}

BOOST_AUTO_TEST_SUITE_END()
