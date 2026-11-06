// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// edge_shot: a model drawn once, to look at: one picture of it, a contact sheet of several, or a window to turn it round in.
//
//   edge_shot [options] <model> <out.png>                  one shot of the model
//   edge_shot [options] --sheet <out.png> <model>...       a contact sheet: every model in a cell of its own (--views: in several views)
//   edge_shot [options] --gui <model>                      a window (when edge was built with edge_sdl)
//   edge_shot --version                                    what it was built with: edge_shot: gui metal vulkan
//
// A model is a .edge file, a .glb or a .gltf (load_model.h).  It is framed whole, by the middle of its bounds, and lit by the light of
// tools/shaders/lit.frag.  The picture is a PNG of RGB; what is not the model is the background.
//
//   --size WxH             the picture (512x512), or the window (960x720)
//   --yaw DEGREES          the view's turn about the model's up, 0 looking along +z at it (35)
//   --pitch DEGREES        its height above the model, between -90 and 90 (20)
//   --fov DEGREES          the lens, vertical, between 0 and 180 (50)
//   --animation NAME|N     the clip the model is posed by, by name or by number (the first)
//   --time SECONDS         the time in the clip (0); a model without a clip refuses --animation and --time
//   --background R,G,B     what is not the model (40,40,48)
//   --renderer NAME        metal or vulkan (the first this build has)
// of a sheet:
//   --cell WxH             the cell of each view (256x256)
//   --views N              each model in N views, turned 360/N degrees from each other, from --yaw (1)
//   --columns N            cells in a row (the views, when there are several; otherwise as many as the rows are: a square)
// of the window:
//   --frames N             closes after N frames (it stays until closed otherwise)
//
// Exits 0 when the file is written (or the window closed), 1 with a message on stderr when it is not: a file is written whole or not at all.
// The window: drag turns the model, shift + drag or the right button pans, the wheel zooms, F frames it again, space plays the clip, Tab
// takes the next, Escape closes.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "animation.h"
#include "camera.h"
#include "image.h"
#include "load_model.h"
#include "model.h"
#include "pixel.h"
#include "render_target.h"
#include "scheduler.h"
#include "shader_dirs.h"
#include "texture.h"
#ifdef EDGE_USE_VULKAN
#include "vulkan_renderer.h"
#endif
#ifdef EDGE_USE_METAL
#include "metal_renderer.h"
#endif
#ifdef EDGE_SHOT_GUI
#include <SDL3/SDL.h>
#ifdef EDGE_USE_VULKAN
#include "vulkan_sdl_surface.h"
#endif
#ifdef EDGE_USE_METAL
#include "metal_sdl_surface.h"
#endif
#endif

namespace
{

using Pixel = rove::bgra<uint8_t>;
using Rgb = std::array<uint8_t, 3>;

rove::rgb<uint8_t> rgb_of(Rgb const &colour)
{
	rove::rgb<uint8_t> pixel;
	pixel.r = colour[0];
	pixel.g = colour[1];
	pixel.b = colour[2];
	return pixel;
}

/// What is wrong with how the program was asked, or with what it was asked to draw: the message, after "edge_shot: ".
struct Failure: std::runtime_error
{
	using std::runtime_error::runtime_error;
};

/// A Failure that the usage follows.
struct UsageFailure: Failure
{
	using Failure::Failure;
};

char const *const USAGE =
	"usage: edge_shot [options] <model> <out.png>\n"
	"       edge_shot [options] --sheet <out.png> <model>...\n"
	"       edge_shot [options] --gui <model>\n"
	"       edge_shot --version\n"
	"options: --size WxH --yaw DEG --pitch DEG --fov DEG --animation NAME|N --time SECONDS --background R,G,B --renderer metal|vulkan\n"
	"of a sheet: --cell WxH --views N --columns N; of the window: --frames N";

constexpr double RADIANS = std::numbers::pi / 180.0;

// ---- the command line ---------------------------------------------------------------------------------------------------

struct Options
{
	enum class Mode { SHOT, SHEET, GUI } mode = Mode::SHOT;
	std::optional<std::pair<int, int>> size, cell;
	double yaw = 35.0, pitch = 20.0, fov = 50.0;
	std::optional<std::string> animation;
	std::optional<double> time;
	Rgb background = {40, 40, 48};
	std::optional<std::string> renderer;
	std::optional<int> views, columns, frames;
	std::filesystem::path output;
	std::vector<std::string> models;
};

double number(std::string const &option, std::string const &text)
{
	char *end = nullptr;
	errno = 0;
	double const value = std::strtod(text.c_str(), &end);
	if (text.empty() || *end != '\0' || errno == ERANGE || !std::isfinite(value))
		throw Failure(option + ": '" + text + "' is not a number");
	return value;
}

int whole(std::string const &option, std::string const &text, int least, int most)
{
	char *end = nullptr;
	errno = 0;
	long const value = std::strtol(text.c_str(), &end, 10);
	if (text.empty() || *end != '\0' || errno == ERANGE || value < least || value > most)
		throw Failure(option + ": '" + text + "' is not a whole number from " + std::to_string(least) + " to " + std::to_string(most));
	return int(value);
}

std::pair<int, int> dimensions(std::string const &option, std::string const &text)
{
	size_t const x = text.find('x');
	if (x == std::string::npos || text.find('x', x + 1) != std::string::npos)
		throw Failure(option + ": '" + text + "' is not WIDTHxHEIGHT");
	return {whole(option, text.substr(0, x), 1, 16384), whole(option, text.substr(x + 1), 1, 16384)};
}

Rgb colour(std::string const &option, std::string const &text)
{
	Rgb result;
	size_t from = 0;
	for (int i = 0; i < 3; ++i)
	{
		size_t const comma = text.find(',', from);
		if ((comma == std::string::npos) != (i == 2))
			throw Failure(option + ": '" + text + "' is not R,G,B");
		result[i] = uint8_t(whole(option, text.substr(from, comma == std::string::npos ? comma : comma - from), 0, 255));
		from = comma + 1;
	}
	return result;
}

Options parse(int argc, char **argv)
{
	Options options;
	std::vector<std::string> positional;
	std::vector<std::string> given;          // the options that need a mode, as given
	bool sheet = false, gui = false;
	for (int i = 1; i < argc; ++i)
	{
		std::string const arg = argv[i];
		if (arg.rfind("--", 0) != 0)
		{
			positional.push_back(arg);
			continue;
		}
		if (arg == "--gui")
		{
			gui = true;
			continue;
		}
		if (i + 1 >= argc)
			throw UsageFailure(arg + (arg == "--sheet" ? " needs the file it writes" : " needs a value"));
		std::string const value = argv[++i];
		if (arg == "--sheet")
		{
			sheet = true;
			options.output = value;
		}
		else if (arg == "--size")
			options.size = dimensions(arg, value);
		else if (arg == "--cell")
			options.cell = dimensions(arg, value);
		else if (arg == "--yaw")
			options.yaw = number(arg, value);
		else if (arg == "--pitch")
		{
			options.pitch = number(arg, value);
			if (options.pitch <= -90.0 || options.pitch >= 90.0)
				throw Failure("--pitch: " + value + " is not between -90 and 90 (straight up or down has no up)");
		}
		else if (arg == "--fov")
		{
			options.fov = number(arg, value);
			if (options.fov <= 0.0 || options.fov >= 180.0)
				throw Failure("--fov: " + value + " is not between 0 and 180");
		}
		else if (arg == "--animation")
			options.animation = value;
		else if (arg == "--time")
		{
			options.time = number(arg, value);
			if (*options.time < 0.0)
				throw Failure("--time: " + value + " is before the clip");
		}
		else if (arg == "--background")
			options.background = colour(arg, value);
		else if (arg == "--renderer")
		{
			if (value != "metal" && value != "vulkan")
				throw Failure("--renderer: '" + value + "' is not metal or vulkan");
			options.renderer = value;
		}
		else if (arg == "--views")
			options.views = whole(arg, value, 1, 360);
		else if (arg == "--columns")
			options.columns = whole(arg, value, 1, 1024);
		else if (arg == "--frames")
			options.frames = whole(arg, value, 1, 1 << 30);
		else
			throw UsageFailure("unknown option " + arg);
	}
	if (sheet && gui)
		throw Failure("--sheet and --gui are two things to do");
	options.mode = sheet ? Options::Mode::SHEET : gui ? Options::Mode::GUI : Options::Mode::SHOT;
	bool const sheet_only = options.cell || options.views || options.columns;
	if (sheet_only && options.mode != Options::Mode::SHEET)
		throw Failure(std::string(options.cell ? "--cell" : options.views ? "--views" : "--columns") + " is of a sheet (--sheet)");
	if (options.size && options.mode == Options::Mode::SHEET)
		throw Failure("--size is of a shot and the window; the cell of a sheet is --cell");
	if (options.frames && options.mode != Options::Mode::GUI)
		throw Failure("--frames is of the window (--gui)");
	switch (options.mode)
	{
	case Options::Mode::SHOT:
		if (positional.size() != 2)
			throw UsageFailure("a shot is of one model and one file");
		options.models = {positional[0]};
		options.output = positional[1];
		break;
	case Options::Mode::SHEET:
		if (positional.empty())
			throw UsageFailure("a sheet is of at least one model");
		options.models = positional;
		break;
	case Options::Mode::GUI:
		if (positional.size() != 1)
			throw UsageFailure("the window is of one model");
		options.models = positional;
		break;
	}
	if (options.mode != Options::Mode::GUI)
	{
		std::string extension = options.output.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		if (extension != ".png")
			throw Failure(options.output.string() + ": the picture is a .png");
	}
	return options;
}

// ---- the renderer ------------------------------------------------------------------------------------------------------

std::string renderer_choice(std::optional<std::string> const &asked)
{
	std::string const choice = asked ? *asked :
#ifdef EDGE_USE_METAL
		"metal";
#else
		"vulkan";
#endif
	bool built = false;
#ifdef EDGE_USE_METAL
	built = built || choice == "metal";
#endif
#ifdef EDGE_USE_VULKAN
	built = built || choice == "vulkan";
#endif
	if (!built)
		throw Failure("--renderer " + choice + ": this build has no such renderer");
	return choice;
}

/// A renderer that draws into targets and shows nothing.
edge::RendererPtr headless_renderer(std::string const &choice, int width, int height)
{
#ifdef EDGE_USE_METAL
	if (choice == "metal")
		return std::make_shared<edge::MetalRenderer>(width, height, edge::metal_shader_dir().string(), std::make_shared<edge::MetalContext>());
#endif
#ifdef EDGE_USE_VULKAN
	if (choice == "vulkan")
		return std::make_shared<edge::VulkanRenderer>(width, height);
#endif
	throw Failure("--renderer " + choice + ": this build has no such renderer");
}

// ---- the view ---------------------------------------------------------------------------------------------------------

/// Where the eye is: turned about the target by yaw, raised by pitch, distance away; and the model it frames (its middle and radius),
/// which the clip planes are set about wherever the target is panned to.
struct Orbit
{
	double home[3] = {0, 0, 0};
	double radius = 1.0;
	double target[3] = {0, 0, 0};
	double distance = 5.0;
	double yaw = 0.0, pitch = 0.0, fov = 0.0;          // radians

	void eye(double (&out)[3]) const
	{
		double const cp = std::cos(pitch);
		out[0] = target[0] + distance * cp * std::sin(yaw);
		out[1] = target[1] + distance * std::sin(pitch);
		out[2] = target[2] + distance * cp * std::cos(yaw);
	}

	/// The model's middle in the view's middle, all of it in a picture of @p aspect (width over height).
	void frame(edge::Model const &model, double aspect)
	{
		rove::matrix<4, 4> identity;
		rove::aabb<3> const bounds = model.bounds(identity);
		double const lo[3] = {bounds.lo.x, bounds.lo.y, bounds.lo.z}, hi[3] = {bounds.hi.x, bounds.hi.y, bounds.hi.z};
		double span = 0.0;
		for (int i = 0; i < 3; ++i)
		{
			home[i] = target[i] = (lo[i] + hi[i]) / 2.0;
			span += (hi[i] - lo[i]) * (hi[i] - lo[i]);
		}
		if (!std::isfinite(span))
			throw Failure("the model's bounds are not finite");
		radius = std::max(std::sqrt(span) / 2.0, 1e-4);
		double const half = std::min(fov / 2.0, std::atan(std::tan(fov / 2.0) * aspect));
		distance = radius / std::sin(half) * 1.05;
	}
};

Orbit orbit_of(Options const &options)
{
	Orbit orbit;
	orbit.yaw = options.yaw * RADIANS;
	orbit.pitch = options.pitch * RADIANS;
	orbit.fov = options.fov * RADIANS;
	return orbit;
}

/// Draws models into a target of the renderer and reads it back.
class Scene
{
public:
	Scene(edge::RendererPtr const &renderer, Rgb const &background):
		renderer_(renderer),
		scheduler_(std::make_shared<edge::Scheduler>(renderer)),
		camera_(std::make_shared<edge::Camera>()),
		lit_(renderer->create_shader_program("", (edge::view_shader_dir() / "lit.frag.spv").string()))
	{
		clear_.clear_color_r = background[0] / 255.0f;
		clear_.clear_color_g = background[1] / 255.0f;
		clear_.clear_color_b = background[2] / 255.0f;
		clear_.clear_color_a = 1.0f;
	}

	/// The model of @p file, lit, in the pose of the clip @p animation names (a name or a number; the first when not given) at @p time.
	edge::ModelPtr load(std::string const &file, std::optional<std::string> const &animation, std::optional<double> const &time)
	{
		edge::ModelPtr model;
		try
		{
			model = edge::load_model(renderer_, scheduler_, file);
			model->check();
		}
		catch (std::exception const &e)
		{
			throw Failure(file + ": " + e.what());
		}
		model->set_materials_shader_program(lit_);
		pose(*model, file, animation, time);
		return model;
	}

	/// @p model seen from @p orbit, @p width x @p height pixels, in the target (which holds it until the next draw).
	edge::RenderTargetPtr const &draw(edge::Model &model, Orbit const &orbit, int width, int height)
	{
		if (!target_ || target_->color_texture()->width() != width || target_->color_texture()->height() != height)
			target_ = renderer_->create_render_target(width, height, edge::PixelFormat::A8R8G8B8);
		double eye[3];
		orbit.eye(eye);
		double const to_model = std::sqrt((eye[0] - orbit.home[0]) * (eye[0] - orbit.home[0]) + (eye[1] - orbit.home[1]) * (eye[1] - orbit.home[1]) +
			(eye[2] - orbit.home[2]) * (eye[2] - orbit.home[2]));
		// clip planes about the model wherever the target was panned to; the margin leaves room for a clip to carry it past its framed box
		double const near_plane = std::max(to_model - 4.0 * orbit.radius, 1e-3 * std::max(to_model, orbit.radius));
		camera_->set_perspective_projection(float(orbit.fov), float(width) / float(height), float(near_plane), float(to_model + 4.0 * orbit.radius));
		camera_->look_at(rove::vec<3>(float(eye[0]), float(eye[1]), float(eye[2])),
			rove::vec<3>(float(orbit.target[0]), float(orbit.target[1]), float(orbit.target[2])), rove::vec<3>(0.0f, 1.0f, 0.0f));
		rove::matrix<4, 4> identity;
		renderer_->begin_frame();
		model.draw(camera_, identity);
		renderer_->begin_pass(target_, clear_);
		scheduler_->flush();
		renderer_->end_pass();
		renderer_->finish_frame();
		return target_;
	}

	/// What draw() drew.
	edge::Image<Pixel> read() { return edge::read_image<Pixel>(*target_->color_texture()); }

	/// The names of the model's clips, comma by comma.
	static std::string clips(edge::Model const &model)
	{
		std::string names;
		for (size_t i = 0; i < model.animations().size(); ++i)
			names += (i ? ", " : "") + model.animations()[i]->name;
		return names;
	}

	/// The clip @p animation names (a name, or a number), the first when not given; Failure for one the model has not.
	static size_t clip_index(edge::Model const &model, std::string const &file, std::optional<std::string> const &animation)
	{
		if (!animation)
			return 0;
		for (size_t i = 0; i < model.animations().size(); ++i)
			if (model.animations()[i]->name == *animation)
				return i;
		bool const digits = !animation->empty() && animation->find_first_not_of("0123456789") == std::string::npos;
		if (!digits || animation->size() > 9 || std::stoul(*animation) >= model.animations().size())
			throw Failure(file + ": no animation '" + *animation + "' (it has " + clips(model) + ")");
		return std::stoul(*animation);
	}

private:
	static void pose(edge::Model &model, std::string const &file, std::optional<std::string> const &animation, std::optional<double> const &time)
	{
		if (model.animations().empty())
		{
			if (animation || time)
				throw Failure(file + ": has no animations (" + (animation ? "--animation " + *animation : "--time") + ")");
			return;
		}
		model.animations()[clip_index(model, file, animation)]->pose(float(time.value_or(0.0)));
	}

	edge::RendererPtr renderer_;
	edge::SchedulerPtr scheduler_;
	edge::CameraPtr camera_;
	edge::ShaderProgramPtr lit_;
	edge::RenderTargetPtr target_;
	edge::RenderPassClearState clear_;
};

void blit(edge::Image<rove::rgb<uint8_t>> &sheet, int left, int top, edge::Image<Pixel> const &cell)
{
	for (int y = 0; y < cell.height(); ++y)
		for (int x = 0; x < cell.width(); ++x)
		{
			Pixel const &from = cell(x, y);
			sheet(left + x, top + y) = rgb_of({from.r, from.g, from.b});
		}
}

void write(std::filesystem::path const &path, edge::Image<rove::rgb<uint8_t>> const &picture)
{
	try
	{
		edge::save_image<rove::rgb<uint8_t>>(path, edge::ImageView<rove::rgb<uint8_t> const>(picture));
	}
	catch (std::exception const &e)
	{
		throw Failure(path.string() + ": " + e.what());
	}
}

// ---- a shot, a sheet ----------------------------------------------------------------------------------------------------

/// The models' views drawn into one picture: a shot (one cell, of --size) or a sheet (--cell each, --columns across).
void sheet(Options const &options, bool single_shot)
{
	int const views = options.views.value_or(1);
	auto const [width, height] = single_shot ? options.size.value_or(std::pair<int, int>(512, 512)) : options.cell.value_or(std::pair<int, int>(256, 256));
	int const count = int(options.models.size()) * views;
	int const columns = single_shot ? 1 : options.columns ? *options.columns : views > 1 ? views : int(std::ceil(std::sqrt(double(count))));
	int const rows = (count + columns - 1) / columns;
	edge::RendererPtr const renderer = headless_renderer(renderer_choice(options.renderer), width, height);
	Scene scene(renderer, options.background);
	edge::Image<rove::rgb<uint8_t>> picture(columns * width, rows * height, rgb_of(options.background));
	int cell = 0;
	for (std::string const &file : options.models)
	{
		edge::ModelPtr const model = scene.load(file, options.animation, options.time);
		for (int view = 0; view < views; ++view, ++cell)
		{
			Orbit orbit = orbit_of(options);
			orbit.yaw += 2.0 * std::numbers::pi * view / views;
			orbit.frame(*model, double(width) / height);
			scene.draw(*model, orbit, width, height);
			blit(picture, cell % columns * width, cell / columns * height, scene.read());
		}
	}
	write(options.output, picture);
}

// ---- the window ---------------------------------------------------------------------------------------------------------

#ifdef EDGE_SHOT_GUI

void sdl_check(bool ok, char const *what)
{
	if (!ok)
		throw Failure(std::string(what) + ": " + SDL_GetError());
}

/// The renderer that shows its frames in the window, and the showing.
class Screen
{
public:
	virtual ~Screen() = default;
	edge::RendererPtr renderer;
	virtual void show(edge::TexturePtr const &frame, int window_width, int window_height) = 0;
};

#ifdef EDGE_USE_VULKAN
class VulkanScreen: public Screen
{
public:
	VulkanScreen(SDL_Window *window, int width, int height)
	{
		vulkan_ = std::make_shared<edge::VulkanRenderer>(width, height, std::string(), edge::sdl_vulkan_instance_extensions(), 2);
		renderer = vulkan_;
		edge::attach_sdl_window(*vulkan_, window, true);
	}
	~VulkanScreen() override
	{
		vulkan_->detach_surface();          // (before the window it is the surface of)
	}
	void show(edge::TexturePtr const &frame, int window_width, int window_height) override { vulkan_->present(frame, window_width, window_height); }

private:
	std::shared_ptr<edge::VulkanRenderer> vulkan_;
};
#endif

#ifdef EDGE_USE_METAL
class MetalScreen: public Screen
{
public:
	MetalScreen(SDL_Window *window, int width, int height): surface_(window)
	{
		metal_ = std::make_shared<edge::MetalRenderer>(width, height, edge::metal_shader_dir().string(), surface_.make_context());
		renderer = metal_;
	}
	~MetalScreen() override
	{
		renderer.reset();
		metal_.reset();          // (before the layer it shows in)
	}
	void show(edge::TexturePtr const &frame, int window_width, int window_height) override
	{
		metal_->show_frame(frame, window_width, window_height);
		metal_->swap_window();
	}

private:
	edge::MetalSDLSurface surface_;
	std::shared_ptr<edge::MetalRenderer> metal_;
};
#endif

void gui(Options const &options)
{
	std::string const choice = renderer_choice(options.renderer);
	auto const [width, height] = options.size.value_or(std::pair<int, int>(960, 720));
	sdl_check(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	flags |= choice == "metal" ? SDL_WINDOW_METAL : SDL_WINDOW_VULKAN;
	SDL_Window *window = SDL_CreateWindow(("edge_shot " + options.models[0]).c_str(), width, height, flags);
	struct Quit
	{
		SDL_Window *&window;
		std::unique_ptr<Screen> &screen;
		~Quit()
		{
			screen.reset();          // (the renderer, then the window)
			if (window)
				SDL_DestroyWindow(window);
			SDL_Quit();
		}
	};
	std::unique_ptr<Screen> screen;
	Quit quit{window, screen};
	sdl_check(window != nullptr, "SDL_CreateWindow");
#ifdef EDGE_USE_METAL
	if (choice == "metal")
		screen = std::make_unique<MetalScreen>(window, width, height);
#endif
#ifdef EDGE_USE_VULKAN
	if (choice == "vulkan")
		screen = std::make_unique<VulkanScreen>(window, width, height);
#endif
	if (!screen)
		throw Failure("--renderer " + choice + ": this build has no such renderer");

	Scene scene(screen->renderer, options.background);
	edge::ModelPtr const model = scene.load(options.models[0], options.animation, options.time);
	auto pixels = [&] { int w, h; sdl_check(SDL_GetWindowSizeInPixels(window, &w, &h), "SDL_GetWindowSizeInPixels"); return std::pair<int, int>(w, h); };
	Orbit orbit = orbit_of(options);
	orbit.frame(*model, double(pixels().first) / pixels().second);
	Orbit const home = orbit;
	size_t clip = model->animations().empty() ? 0 : Scene::clip_index(*model, options.models[0], options.animation);
	double clip_time = options.time.value_or(0.0);
	bool playing = false;
	Uint64 last = SDL_GetTicks();
	std::printf("drag turns, shift + drag or the right button pans, the wheel zooms, F frames, space plays, Tab takes the next clip, Escape closes\n");
	int frames = 0;
	for (bool open = true; open;)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
				open = false;
			else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
			{
				if (event.key.key == SDLK_ESCAPE)
					open = false;
				else if (event.key.key == SDLK_F)
					orbit = home;
				else if (event.key.key == SDLK_SPACE)
					playing = !playing;
				else if (event.key.key == SDLK_TAB && !model->animations().empty())
				{
					clip = (clip + 1) % model->animations().size();
					clip_time = 0.0;
					model->animations()[clip]->pose(0.0f);
					SDL_SetWindowTitle(window, ("edge_shot " + options.models[0] + " " + model->animations()[clip]->name).c_str());
				}
			}
			else if (event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK | SDL_BUTTON_RMASK)))
			{
				bool const pan = (event.motion.state & (SDL_BUTTON_MMASK | SDL_BUTTON_RMASK)) || (SDL_GetModState() & SDL_KMOD_SHIFT);
				if (!pan)
				{
					orbit.yaw -= event.motion.xrel * 0.008;
					orbit.pitch = std::clamp(orbit.pitch + event.motion.yrel * 0.008, -89.0 * RADIANS, 89.0 * RADIANS);
				}
				else
				{
					// the eye's right and up, and a pixel's worth of the world at the target
					double eye[3];
					orbit.eye(eye);
					double forward[3], length = 0.0;
					for (int i = 0; i < 3; ++i)
						length += (forward[i] = orbit.target[i] - eye[i]) * forward[i];
					length = std::sqrt(length);
					for (double &f : forward)
						f /= length;
					double right[3] = {-forward[2], 0.0, forward[0]};          // forward x up
					double const rl = std::sqrt(right[0] * right[0] + right[2] * right[2]);
					for (double &r : right)
						r /= rl;
					double const up[3] = {right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2], right[0] * forward[1] - right[1] * forward[0]};
					int w, h;
					SDL_GetWindowSize(window, &w, &h);
					double const per_pixel = 2.0 * orbit.distance * std::tan(orbit.fov / 2.0) / std::max(1, h);
					for (int i = 0; i < 3; ++i)
						orbit.target[i] += (-right[i] * event.motion.xrel - up[i] * event.motion.yrel) * per_pixel;
				}
			}
			else if (event.type == SDL_EVENT_MOUSE_WHEEL)
				orbit.distance *= std::pow(0.85, event.wheel.y);
		}
		Uint64 const now = SDL_GetTicks();
		if (playing && !model->animations().empty())
		{
			double const duration = model->animations()[clip]->duration();
			clip_time += (now - last) / 1000.0;
			if (duration > 0.0)
				clip_time = std::fmod(clip_time, duration);
			model->animations()[clip]->pose(float(clip_time));
		}
		last = now;
		auto const [w, h] = pixels();
		if (w > 0 && h > 0)
		{
			screen->show(scene.draw(*model, orbit, w, h)->color_texture(), w, h);
		}
		if (options.frames && ++frames >= *options.frames)
			open = false;
	}
}

#endif

}

int main(int argc, char **argv)
{
	try
	{
		if (argc == 2 && std::string(argv[1]) == "--version")
		{
			// what this build has: the window, and the renderers it draws with
			std::string features = "edge_shot:";
#ifdef EDGE_SHOT_GUI
			features += " gui";
#endif
#ifdef EDGE_USE_METAL
			features += " metal";
#endif
#ifdef EDGE_USE_VULKAN
			features += " vulkan";
#endif
			std::puts(features.c_str());
			return 0;
		}
		Options const options = parse(argc, argv);
		switch (options.mode)
		{
		case Options::Mode::SHOT:
			sheet(options, true);
			break;
		case Options::Mode::SHEET:
			sheet(options, false);
			break;
		case Options::Mode::GUI:
#ifdef EDGE_SHOT_GUI
			gui(options);
#else
			throw Failure("--gui: this edge_shot was built without edge_sdl (EDGE_SDL)");
#endif
			break;
		}
		return 0;
	}
	catch (UsageFailure const &e)
	{
		std::fprintf(stderr, "edge_shot: %s\n%s\n", e.what(), USAGE);
		return 1;
	}
	catch (std::exception const &e)
	{
		std::fprintf(stderr, "edge_shot: %s\n", e.what());
		return 1;
	}
}
