// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "forward.h"

struct ImDrawData;
struct ImGuiContext;

namespace edge
{

/// Dear ImGui drawn through edge, on any renderer: the renderer backend ImGui leaves to the application (edge_imgui, with the
/// parent's imgui target).  An ImGui frame's draw data is queue()d while the renderer is idle (its buffers are written then), and
/// draw()n inside a pass of a target of the size it was queued for.  The draw data is in the display's logical pixels; it is scaled
/// to the target's.  edge's built-in ui_vertex.spv and ui_fragment.spv; the colours blend with straight alpha.
class ImGuiRenderer
{
public:
	/// The renderer backend of the current ImGui context: builds its font atlas into a texture.  std::invalid_argument for no
	/// renderer; std::logic_error without a current context or for one that has a renderer backend already.
	explicit ImGuiRenderer(RendererPtr const &renderer);
	/// Gives the context its backend back (when it is current), so that another can take it.
	~ImGuiRenderer();
	ImGuiRenderer(ImGuiRenderer const &) = delete;
	ImGuiRenderer &operator=(ImGuiRenderer const &) = delete;

	/// Makes @p texture something ImGui draws (ImGui::Image, AddImage): the id to pass, another than the font's and each other's.
	/// Its texels are multiplied by the vertex colour, so a white tint draws them as they are.  std::invalid_argument for none.
	std::uint64_t add_texture(TexturePtr const &texture);

	/// Takes the draw data of the frame just rendered (ImGui::Render()) for a target of @p target_width x @p target_height pixels.
	/// std::logic_error inside a frame or with a frame queued and not drawn; std::invalid_argument, nothing queued, for no draw
	/// data, a display or a target of no area, a draw callback (nothing here makes one) or a texture that was not added.
	void queue(ImDrawData const *data, int target_width, int target_height);
	/// Draws what was queued, in the pass that is open; nothing queued, nothing drawn.  std::logic_error outside a pass, or in a
	/// pass of a target of another size than the frame was queued for (it stays queued).
	void draw();

private:
	/// One ImGui draw command: the target's rectangle it draws in, which of the queued indices, with which texture.
	struct Command
	{
		int x, y, width, height;
		std::size_t first_index, indices;
		std::uint64_t texture;
	};

	/// An atlas texture ImGui asked for: made, and the id its draws carry.
	struct Atlas
	{
		TexturePtr texture;
		std::uint64_t id;
	};

	/// ImGui's requests of the backend (ImDrawData::Textures): the atlas's textures made, sent again when they changed, let go.
	void update_textures(ImDrawData const *data);

	RendererPtr renderer_;
	ImGuiContext *context_;
	ShaderProgramPtr program_;
	std::map<std::uint64_t, MaterialPtr> materials_;      // by the texture id ImGui draws with
	std::map<int, Atlas> atlas_;                          // by ImTextureData::UniqueID
	std::uint64_t next_texture_ = 1;
	CameraPtr camera_;
	VertexBufferPtr vertices_;
	IndexBufferPtr indices_;
	std::size_t vertex_capacity_ = 0, index_capacity_ = 0;
	std::vector<DrawCallPtr> calls_;                      // one a command, kept for the next frames
	std::vector<Command> commands_;
	int target_width_ = 0, target_height_ = 0;
	bool queued_ = false;

	MaterialPtr make_material(TexturePtr const &texture) const;
};

}
