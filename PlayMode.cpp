#include "PlayMode.hpp"

#include "ColorProgram.hpp"
#include "ColorTextureProgram.hpp"
#include "Font.hpp"
#include "TextRenderer.hpp"
#include "DrawLines.hpp"
#include "LitColorTextureProgram.hpp"
#include "Load.hpp"
#include "Mesh.hpp"
#include "data_path.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

//------------------------------------------------------------------
// The circuit and balls are modelled in Blender and exported to dist/silverstone.{pnct,scene}.

GLuint silverstone_meshes_for_lit_color_texture_program = 0;
GLuint silverstone_meshes_for_color_texture_program = 0;
Load< MeshBuffer > silverstone_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("silverstone.pnct"));
	silverstone_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	silverstone_meshes_for_color_texture_program = ret->make_vao_for_program(color_texture_program->program);
	return ret;
});

Load< Scene > silverstone_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("silverstone.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = silverstone_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = silverstone_meshes_for_lit_color_texture_program;
		if (mesh_name.rfind("Ball.", 0) == 0) {
			//the balls are drawn like the target in game2: flat, self-illuminating (unlit) solid-color spheres
			drawable.pipeline.program = color_texture_program->program;
			drawable.pipeline.CLIP_FROM_OBJECT_mat4 = color_texture_program->CLIP_FROM_OBJECT_mat4;
			drawable.pipeline.LIGHT_FROM_OBJECT_mat4x3 = -1U;
			drawable.pipeline.LIGHT_FROM_NORMAL_mat3 = -1U;
			drawable.pipeline.vao = silverstone_meshes_for_color_texture_program;
		}
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
	});
});

//fonts (same as game5): prefer the font we ship next to the executable; fall back on common system fonts so the game still runs
static Font *make_font(unsigned int pixel_height) {
	std::string candidates[] = {
		data_path("font.ttf"),
		"C:/Windows/Fonts/arialbd.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		"/System/Library/Fonts/Supplemental/Arial Bold.ttf"
	};
	for (auto const &path : candidates) {
		if (std::ifstream(path).good()) return new Font(path, pixel_height);
	}
	throw std::runtime_error("Could not find a font: put a .ttf at dist/font.ttf");
}
Load< Font > ui_font(LoadTagDefault, []() -> Font const * { return make_font(44); });
Load< Font > big_font(LoadTagDefault, []() -> Font const * { return make_font(120); });

//------------------------------------------------------------------
// A tiny immediate-mode triangle batch for the 2D HUD, drawn with the color_program.

static GLuint batch_vbo = 0;
static GLuint batch_vao = 0;

static Load< void > setup_batch(LoadTagDefault, [](){
	glGenBuffers(1, &batch_vbo);
	glGenVertexArrays(1, &batch_vao);
	glBindVertexArray(batch_vao);
	glBindBuffer(GL_ARRAY_BUFFER, batch_vbo);
	glVertexAttribPointer(color_program->Position_vec4, 3, GL_FLOAT, GL_FALSE,
		sizeof(DrawLines::Vertex), (GLbyte *)0 + offsetof(DrawLines::Vertex, Position));
	glEnableVertexAttribArray(color_program->Position_vec4);
	glVertexAttribPointer(color_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE,
		sizeof(DrawLines::Vertex), (GLbyte *)0 + offsetof(DrawLines::Vertex, Color));
	glEnableVertexAttribArray(color_program->Color_vec4);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	GL_ERRORS();
});

//the framebuffer is sRGB, so colors are specified in "what it should look like" terms and converted:
static glm::u8vec4 rgb(float r, float g, float b, float a = 1.0f) {
	auto enc = [](float x) { return uint8_t(std::lround(255.0f * glm::clamp(std::pow(glm::clamp(x, 0.0f, 1.0f), 2.2f), 0.0f, 1.0f))); };
	return glm::u8vec4(enc(r), enc(g), enc(b), uint8_t(std::lround(255.0f * a)));
}

struct Batch {
	std::vector< DrawLines::Vertex > v;

	void tri(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::u8vec4 col) {
		v.emplace_back(glm::vec3(a, 0.0f), col);
		v.emplace_back(glm::vec3(b, 0.0f), col);
		v.emplace_back(glm::vec3(c, 0.0f), col);
	}
	void quad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, glm::u8vec4 col) {
		tri(a, b, c, col);
		tri(a, c, d, col);
	}
	void rect(glm::vec2 lo, glm::vec2 hi, glm::u8vec4 col) {
		quad(lo, glm::vec2(hi.x, lo.y), hi, glm::vec2(lo.x, hi.y), col);
	}

	void flush(glm::mat4 const &to_clip) {
		if (v.empty()) return;
		glBindBuffer(GL_ARRAY_BUFFER, batch_vbo);
		glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(v[0]), v.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(color_program->program);
		glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(to_clip));
		glBindVertexArray(batch_vao);
		glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size()));
		glBindVertexArray(0);
		glUseProgram(0);
		v.clear();
	}
};

static std::string ordinal(int n) {
	int m100 = n % 100;
	if (m100 >= 11 && m100 <= 13) return std::to_string(n) + "TH";
	switch (n % 10) {
		case 1: return std::to_string(n) + "ST";
		case 2: return std::to_string(n) + "ND";
		case 3: return std::to_string(n) + "RD";
		default: return std::to_string(n) + "TH";
	}
}

static std::string time_string(float t) {
	int minutes = int(t / 60.0f);
	float seconds = t - 60.0f * float(minutes);
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%d:%05.2f", minutes, seconds);
	return buf;
}

//------------------------------------------------------------------

PlayMode::PlayMode() : world(data_path("silverstone.track"), 20260), scene(*silverstone_scene) {
	world.player_autopilot = (SDL_getenv("BALLRACE_AUTOPILOT") != nullptr); //for testing without a human

	//look up the things we move around:
	auto find = [&](std::string const &name) -> Scene::Transform * {
		for (auto &t : scene.transforms) if (t.name == name) return &t;
		throw std::runtime_error("Scene is missing '" + name + "'.");
	};
	char buf[32];
	for (int i = 0; i < World::NumRacers; ++i) {
		std::snprintf(buf, sizeof(buf), "Ball.%02d", i);
		ball_xf[i] = find(buf);
	}
	marker_xf = find("Marker");
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	restart();
}

PlayMode::~PlayMode() {
}

void PlayMode::restart() {
	world.reset();
	accumulator = 0.0f;
	for (auto &q : ball_rot) q = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	Racer const &p = world.racers[World::PlayerIndex];
	cam_yaw = p.heading;
	cam_focus = glm::vec3(p.pos, 0.0f);

	//keys that are still physically held should count:
	throttle = brake = left = right = Button();
	int numkeys = 0;
	bool const *keys = SDL_GetKeyboardState(&numkeys);
	if (keys) {
		left.pressed = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
		right.pressed = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
		throttle.pressed = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
		brake.pressed = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
	}
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if (evt.type == SDL_EVENT_KEY_DOWN || evt.type == SDL_EVENT_KEY_UP) {
		bool is_down = (evt.type == SDL_EVENT_KEY_DOWN);
		Button *button = nullptr;
		switch (evt.key.key) {
			case SDLK_A: case SDLK_LEFT: button = &left; break;
			case SDLK_D: case SDLK_RIGHT: button = &right; break;
			case SDLK_W: case SDLK_UP: button = &throttle; break;
			case SDLK_S: case SDLK_DOWN: button = &brake; break;
			default: break;
		}
		if (button) {
			button->pressed = is_down;
			return true;
		}
		if (is_down && evt.key.key == SDLK_R) {
			restart();
			return true;
		}
		if (is_down && evt.key.key == SDLK_ESCAPE) {
			Mode::set_current(nullptr);
			return true;
		}
	}
	return false;
}

void PlayMode::update(float elapsed) {
	anim_time += elapsed;
	accumulator += elapsed;

	Racer &player = world.racers[World::PlayerIndex];

	int steps = 0;
	while (accumulator >= World::DT) {
		if (!world.player_autopilot) {
			player.throttle = throttle.pressed ? 1.0f : 0.0f;
			player.brake = brake.pressed ? 1.0f : 0.0f;
			player.steer_in = (left.pressed ? 1.0f : 0.0f) - (right.pressed ? 1.0f : 0.0f);
		}
		world.step();
		accumulator -= World::DT;
		if (++steps >= 16) { accumulator = 0.0f; break; } //don't spiral if the machine is slow
	}

	//balls roll (visuals only):
	for (int i = 0; i < World::NumRacers; ++i) {
		Racer const &r = world.racers[i];
		float speed = glm::length(r.vel);
		if (speed > 1e-3f) {
			glm::vec3 dir(r.vel / speed, 0.0f);
			glm::vec3 axis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), dir);
			ball_rot[i] = glm::normalize(glm::angleAxis(speed * elapsed / r.radius, axis) * ball_rot[i]);
		}
	}

	//chase camera follows the player's heading:
	{
		float speed = glm::length(player.vel);
		float target_yaw = player.heading;
		if (speed > 6.0f) { //lean toward where we're actually going when sliding
			float vel_yaw = std::atan2(player.vel.y, player.vel.x);
			float d = std::atan2(std::sin(vel_yaw - player.heading), std::cos(vel_yaw - player.heading));
			if (std::abs(d) < 1.2f) target_yaw = player.heading + 0.4f * d;
		}
		float dy = std::atan2(std::sin(target_yaw - cam_yaw), std::cos(target_yaw - cam_yaw));
		cam_yaw += dy * (1.0f - std::exp(-4.0f * elapsed));
		glm::vec3 target(player.pos, 0.0f);
		cam_focus += (target - cam_focus) * (1.0f - std::exp(-14.0f * elapsed));
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	const float aspect = float(drawable_size.x) / float(drawable_size.y);
	using glm::vec2; using glm::vec3;

	Racer const &player = world.racers[World::PlayerIndex];
	TrackModel const &track = world.track;

	//------------------------------------------------ place everything in the Blender scene
	for (int i = 0; i < World::NumRacers; ++i) {
		Racer const &r = world.racers[i];
		ball_xf[i]->position = vec3(r.pos, r.radius);
		ball_xf[i]->scale = vec3(r.radius);
		ball_xf[i]->rotation = ball_rot[i];
	}
	{ //small marker over the player
		marker_xf->position = vec3(player.pos, 2.3f + 0.12f * std::sin(anim_time * 5.0f));
		marker_xf->rotation = glm::angleAxis(anim_time * 2.0f, vec3(0.0f, 0.0f, 1.0f));
		marker_xf->scale = vec3(0.5f);
	}
	{ //chase camera
		vec2 f(std::cos(cam_yaw), std::sin(cam_yaw));
		float speed = glm::length(player.vel);
		float back = 12.0f + 0.12f * speed;
		vec3 eye = cam_focus - vec3(f, 0.0f) * back + vec3(0.0f, 0.0f, 6.5f);
		vec3 look = cam_focus + vec3(f, 0.0f) * 9.0f + vec3(0.0f, 0.0f, 0.6f);
		glm::mat4 view = glm::lookAt(eye, look, vec3(0.0f, 0.0f, 1.0f));
		glm::mat4 world_from_cam = glm::inverse(view);
		camera->transform->position = eye;
		camera->transform->rotation = glm::normalize(glm::quat_cast(glm::mat3(world_from_cam)));
		camera->transform->scale = vec3(1.0f);
		camera->fovy = glm::radians(52.0f + 16.0f * glm::clamp(speed / World::TopSpeed, 0.0f, 1.0f)); //a little speed feeling
		camera->aspect = aspect;
	}

	//------------------------------------------------ draw the 3D scene
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::normalize(vec3(-0.25f, -0.35f, -0.9f))));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(vec3(1.15f, 1.15f, 1.1f)));
	glUseProgram(0);

	glm::u8vec4 clear = rgb(0.10f, 0.11f, 0.15f);
	glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	scene.draw(*camera);

	//------------------------------------------------ HUD (text is rendered like game5: FreeType + HarfBuzz, Roboto)
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND); //(glyph quads carry their letterform in alpha)
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	const float W = float(drawable_size.x), H = float(drawable_size.y);
	const float S = H / 1080.0f; //text scale: fonts are rasterized for a 1080p window
	glm::mat4 pixels_to_clip = glm::ortho(0.0f, W, 0.0f, H);
	auto order = world.ranking();
	float speed = glm::length(player.vel);

	if (player.finished) { //results panel background
		Batch b;
		b.rect(vec2(30.0f * S, H - 880.0f * S), vec2(690.0f * S, H - 110.0f * S), rgb(0.05f, 0.06f, 0.12f));
		b.rect(vec2(30.0f * S, H - 880.0f * S), vec2(34.0f * S, H - 110.0f * S), rgb(0.7f, 0.75f, 0.95f));
		b.flush(pixels_to_clip);
	}

	{
		TextRenderer text(pixels_to_clip);
		const vec3 X(S, 0.0f, 0.0f), Y(0.0f, S, 0.0f);

		//draws text with a drop shadow; (x,y) is the baseline start in pixels from the bottom-left
		auto put = [&](Font const &font, std::string const &str, float x, float y, glm::u8vec4 col, float align) {
			float width = 0.0f;
			font.layout(str, &width);
			x -= align * width * S;
			text.draw_text(font, str, vec3(x + 2.0f * S, y - 2.0f * S, 0.0f), X, Y, glm::u8vec4(0, 0, 0, 0xb0));
			text.draw_text(font, str, vec3(x, y, 0.0f), X, Y, col);
		};

		//race time, top left:
		put(*ui_font, "TIME " + time_string(player.finished ? player.finish_time : world.race_time), 40.0f * S, H - 70.0f * S, rgb(1.0f, 1.0f, 1.0f), 0.0f);

		//wrong way?
		{
			vec2 t = track.tangent_at(player.lap_s);
			vec2 f(std::cos(player.heading), std::sin(player.heading));
			if (world.countdown <= 0.0f && glm::dot(t, f) < -0.3f && speed > 2.0f && !player.finished) {
				put(*ui_font, "WRONG WAY", W * 0.5f, H * 0.62f, rgb(1.0f, 0.3f, 0.3f), 0.5f);
			}
		}

		//countdown:
		if (world.countdown > 0.0f) {
			put(*big_font, std::to_string(int(std::ceil(world.countdown))), W * 0.5f, H * 0.5f, rgb(1.0f, 0.9f, 0.3f), 0.5f);
		} else if (world.race_time < 1.0f) {
			put(*big_font, "GO!", W * 0.5f, H * 0.5f, rgb(0.4f, 1.0f, 0.5f), 0.5f);
		}

		if (player.finished) {
			put(*ui_font, ordinal(player.place) + " PLACE", 60.0f * S, H - 170.0f * S, rgb(1.0f, 0.9f, 0.3f), 0.0f);
			for (size_t i = 0; i < order.size(); ++i) {
				Racer const &r = world.racers[order[i]];
				float y = H - 230.0f * S - 52.0f * S * float(i);
				glm::u8vec4 col = r.is_player ? rgb(1.0f, 0.95f, 0.3f) : rgb(0.85f, 0.9f, 1.0f);
				std::string name = r.is_player ? "You" : "Ball " + std::to_string(order[i] + 1);
				put(*ui_font, std::to_string(i + 1), 60.0f * S, y, col, 0.0f);
				put(*ui_font, name, 130.0f * S, y, col, 0.0f);
				put(*ui_font, r.finished ? time_string(r.finish_time) : std::string("..."), 660.0f * S, y, col, 1.0f);
			}
		}
	}

	glDisable(GL_BLEND);

	GL_ERRORS();
}
