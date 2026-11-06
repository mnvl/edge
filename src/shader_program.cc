// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "shader_program.h"

namespace edge
{

ShaderProgram::ShaderProgram(std::string const &vertex_shader_filename,
	std::string const &fragment_shader_filename):
	vertex_shader_filename_(vertex_shader_filename),
	fragment_shader_filename_(fragment_shader_filename)
{
}

ShaderProgram::~ShaderProgram() = default;

}
