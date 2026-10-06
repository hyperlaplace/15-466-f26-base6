#pragma once

//World: the (rendering-free) physics simulation for the ball race.
// Everything advances in fixed DT steps, so a given seed + input sequence always plays out the same way.

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

//tiny deterministic RNG (so results don't depend on the standard library's distributions):
struct Rng {
	uint32_t state = 1;
	uint32_t next() { //xorshift32
		state ^= state << 13; state ^= state >> 17; state ^= state << 5;
		return state;
	}
	float uniform() { return float(next() >> 8) / float(1 << 24); } //[0,1)
	float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
};

//The circuit: a closed centre line (made in Blender by scenes/make-silverstone.py) plus derived data.
struct TrackModel {
	std::vector< glm::vec2 > pts;     //centre line, evenly spaced, closed
	std::vector< glm::vec2 > tangent; //unit tangent at each point
	std::vector< float > speed_limit; //how fast the AI should be going at each point (from curvature + braking distance)
	float half_width = 7.0f;          //asphalt half-width
	float wall_dist = 3.0f;           //invisible wall distance from the centre line
	float length = 0.0f;              //lap length
	float ds = 2.0f;                  //point spacing
	int n = 0;

	//throws if the file is missing/bad:
	void load(std::string const &filename);
	//(re)compute the AI speed profile for a car with the given grip/brake/top speed:
	void build_speed_profile(float lateral_accel, float brake_decel, float top_speed);

	struct Proj {
		int index = 0;                   //segment index (segment i goes from point i to i+1)
		float s = 0.0f;                  //distance along the lap [0,length)
		float lateral = 0.0f;            //signed distance from centre line (left is positive)
		glm::vec2 closest = glm::vec2(0.0f);
		glm::vec2 tangent = glm::vec2(1.0f, 0.0f);
		glm::vec2 normal = glm::vec2(0.0f, 1.0f); //points left
	};
	//closest point on the centre line; searches only near `hint` (a segment index) unless hint < 0:
	Proj project(glm::vec2 p, int hint) const;

	glm::vec2 point_at(float s, float lateral = 0.0f) const;
	glm::vec2 tangent_at(float s) const;
	float speed_limit_at(float s) const;
};

//one racing ball:
struct Racer {
	glm::vec2 pos = glm::vec2(0.0f);
	glm::vec2 vel = glm::vec2(0.0f);
	float radius = 0.8f;
	float invMass = 1.0f;

	float heading = 0.0f; //direction the ball is steered (radians); velocity can differ (that's sliding)
	float steer = 0.0f;   //smoothed steering state in [-1,1]; + is left

	//control inputs (set by the player's keys or by the AI each tick):
	float throttle = 0.0f; //[0,1]
	float brake = 0.0f;    //[0,1]
	float steer_in = 0.0f; //[-1,1]

	bool is_player = false;

	//where we are on the circuit:
	int hint = 0;
	float lap_s = 0.0f;    //distance along the lap (for unwrapping)
	float progress = 0.0f; //total distance from the start line (negative on the grid)
	float lateral = 0.0f;
	float draft = 0.0f;    //[0,1] how much slipstream we are getting

	//AI personality:
	float skill = 1.0f;
	float lane_pref = 0.0f;
	float lane = 0.0f;     //current lateral target

	bool finished = false;
	float finish_time = 0.0f;
	int place = 0;
};

struct World {
	static constexpr float DT = 1.0f / 120.0f;
	static constexpr int NumRacers = 12;
	static constexpr int PlayerIndex = 11; //starts last on the grid
	static constexpr int Laps = 1;
	static constexpr float CountdownSeconds = 3.0f;

	//car tuning (units, seconds):
	static constexpr float TopSpeed = 46.0f;
	static constexpr float Accel = 24.0f;
	static constexpr float BrakeDecel = 40.0f;
	static constexpr float GripMax = 36.0f; //max sideways acceleration on asphalt

	World(std::string const &track_file, uint32_t seed = 1);

	//restart the race:
	void reset();

	//advance exactly one DT:
	void step();

	TrackModel track;
	std::vector< Racer > racers;

	uint32_t seed = 1;
	Rng rng;
	float countdown = CountdownSeconds; //controls are locked until this reaches 0
	float race_time = 0.0f;
	int num_finished = 0;
	bool player_autopilot = false; //debug: let the AI drive the player ball too
	bool player_digital = false;   //debug: ...but only with on/off "keyboard" inputs

	//grid slot i: (arc length relative to the line, lateral offset); must match scenes/make-silverstone.py
	static void slot_coords(int i, float *s, float *lateral);

	//racers ordered best-to-worst (finishers by place, then everybody else by distance covered):
	std::vector< int > ranking() const;

	//ensures the same sequence of calls -> the same state:
	uint64_t hash() const;

private:
	void run_ai(int index);
	void update_tracking(Racer &r, bool global);
};
