// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>

namespace edge
{

class Camera;
class DrawCall;
class VulkanRenderer;
class GLESRenderer;
class Font;
class IndexBuffer;
struct Joint;
class Material;
class Mesh;
class Model;
class Renderer;
class RenderTarget;
class Scheduler;
class ShaderProgram;
class Texture;
class UberDrawCall;
class VertexBuffer;
struct Animation;

using AnimationPtr = std::shared_ptr<Animation>;
using CameraPtr = std::shared_ptr<Camera>;
using VulkanRendererPtr = std::shared_ptr<VulkanRenderer>;
using GLESRendererPtr = std::shared_ptr<GLESRenderer>;
using DrawCallPtr = std::shared_ptr<DrawCall>;
using FontPtr = std::shared_ptr<Font>;
using IndexBufferPtr = std::shared_ptr<IndexBuffer>;
using JointPtr = std::shared_ptr<Joint>;
using MaterialPtr = std::shared_ptr<Material>;
using MeshPtr = std::shared_ptr<Mesh>;
using ModelPtr = std::shared_ptr<Model>;
using RendererPtr = std::shared_ptr<Renderer>;
using RenderTargetPtr = std::shared_ptr<RenderTarget>;
using SchedulerPtr = std::shared_ptr<Scheduler>;
using ShaderProgramPtr = std::shared_ptr<ShaderProgram>;
using TexturePtr = std::shared_ptr<Texture>;
using UberDrawCallPtr = std::shared_ptr<UberDrawCall>;
using VertexBufferPtr = std::shared_ptr<VertexBuffer>;
using VulkanRendererWeakPtr = std::weak_ptr<VulkanRenderer>;
using JointWeakPtr = std::weak_ptr<Joint>;
using MeshWeakPtr = std::weak_ptr<Mesh>;
using RendererWeakPtr = std::weak_ptr<Renderer>;

}
