// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <string>

namespace edge
{

class ShaderProgram: public std::enable_shared_from_this<ShaderProgram>
{
public:
	ShaderProgram(std::string const &vertex_shader_filename, std::string const &fragment_shader_filename);
	~ShaderProgram();

	std::string const &vertex_shader_filename() const { return vertex_shader_filename_; }
	std::string const &fragment_shader_filename() const { return fragment_shader_filename_; }

private:
	std::string vertex_shader_filename_;
	std::string fragment_shader_filename_;
};

}
