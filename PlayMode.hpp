#pragma once

#include "Mode.hpp"
#include "Scene.hpp"
#include "World.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <vector>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		bool pressed = false;
	} throttle, brake, left, right;

	//the simulation (advances in fixed steps; see World.hpp):
	World world;
	float accumulator = 0.0f;

	//the circuit and balls, modelled in Blender (scenes/make-silverstone.py):
	Scene scene;
	Scene::Camera *camera = nullptr;
	std::array< Scene::Transform *, World::NumRacers > ball_xf{};
	std::array< glm::quat, World::NumRacers > ball_rot;
	Scene::Transform *marker_xf = nullptr;

	//camera follow state (rendering only; never feeds back into the simulation):
	float cam_yaw = 0.0f;
	glm::vec3 cam_focus = glm::vec3(0.0f);

	//wall-clock time for animations that don't matter to gameplay:
	float anim_time = 0.0f;

	void restart();
};
