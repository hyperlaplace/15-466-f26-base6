#include "World.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

//---------- tuning knobs ----------
namespace Tune {
	constexpr float DragCoef = World::Accel / (World::TopSpeed * World::TopSpeed); //terminal speed == TopSpeed
	constexpr float RollDecel = 0.5f;        //rolling resistance
	constexpr float GripGain = 10.0f;        //how hard tyres fight sideways velocity (1/s), until the grip limit
	constexpr float SteerRate = 7.0f;        //how fast the steering state follows the keys (1/s)
	constexpr float TurnRate = 3.0f;         //max heading change rate at low speed (rad/s)
	constexpr float TurnSpeedRef = 16.0f;    //speed at which turning rate halves
	constexpr float ReverseAccel = 10.0f;
	constexpr float MaxReverse = 7.0f;
	constexpr float DraftRange = 16.0f;
	constexpr float DraftWidth = 1.6f;
	constexpr float DraftStrength = 0.38f;   //max reduction of air drag
	constexpr float BallRestitution = 0.35f;
	constexpr float WallRestitution = 0.25f;
	constexpr float WallScrub = 0.998f;      //speed kept per contact pass (sliding along a barrier scrubs speed)
	constexpr int SolverIterations = 3;
}

static float wrap_angle(float a) {
	while (a > float(M_PI)) a -= 2.0f * float(M_PI);
	while (a < -float(M_PI)) a += 2.0f * float(M_PI);
	return a;
}

//------------------------------------------------------------------ track

void TrackModel::load(std::string const &filename) {
	std::ifstream in(filename);
	if (!in) throw std::runtime_error("Couldn't open track file '" + filename + "'.");
	std::string word;
	std::string header;
	std::getline(in, header);
	if (header.rfind("silverstone", 0) != 0) throw std::runtime_error("Bad track file header in '" + filename + "'.");
	while (in >> word) {
		if (word == "half_width") in >> half_width;
		else if (word == "wall_dist") in >> wall_dist;
		else if (word == "length") in >> length;
		else if (word == "count") in >> n;
		else if (word == "points") {
			pts.resize(n);
			for (int i = 0; i < n; ++i) in >> pts[i].x >> pts[i].y;
		}
	}
	if (n < 8 || int(pts.size()) != n || length <= 0.0f) throw std::runtime_error("Incomplete track file '" + filename + "'.");
	ds = length / float(n);
	tangent.resize(n);
	for (int i = 0; i < n; ++i) {
		glm::vec2 d = pts[(i + 1) % n] - pts[(i + n - 1) % n];
		float l = glm::length(d);
		tangent[i] = (l > 1e-6f) ? d / l : glm::vec2(1.0f, 0.0f);
	}
	build_speed_profile(0.92f * World::GripMax, 0.9f * World::BrakeDecel, World::TopSpeed);
}

void TrackModel::build_speed_profile(float lateral_accel, float brake_decel, float top_speed) {
	speed_limit.assign(n, top_speed);
	for (int i = 0; i < n; ++i) {
		float a0 = std::atan2(tangent[(i + n - 2) % n].y, tangent[(i + n - 2) % n].x);
		float a1 = std::atan2(tangent[(i + 2) % n].y, tangent[(i + 2) % n].x);
		float curvature = std::abs(wrap_angle(a1 - a0)) / (4.0f * ds);
		if (curvature > 1e-4f) speed_limit[i] = std::min(top_speed, std::sqrt(lateral_accel / curvature));
	}
	//braking distance: you can't arrive at a slow corner at full speed (two passes so it wraps around the lap)
	for (int pass = 0; pass < 2; ++pass) {
		for (int i = n - 1; i >= 0; --i) {
			float next = speed_limit[(i + 1) % n];
			float allowed = std::sqrt(next * next + 2.0f * brake_decel * ds);
			speed_limit[i] = std::min(speed_limit[i], allowed);
		}
	}
}

TrackModel::Proj TrackModel::project(glm::vec2 p, int hint) const {
	Proj best;
	float best_d2 = 1e30f;
	int count = (hint < 0) ? n : std::min(n, 80);
	int first = (hint < 0) ? 0 : hint - 30;
	for (int k = 0; k < count; ++k) {
		int i = ((first + k) % n + n) % n;
		glm::vec2 a = pts[i], b = pts[(i + 1) % n];
		glm::vec2 ab = b - a;
		float t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
		glm::vec2 c = a + ab * t;
		float d2 = glm::dot(p - c, p - c);
		if (d2 < best_d2) {
			best_d2 = d2;
			best.index = i;
			best.s = (float(i) + t) * ds;
			best.closest = c;
			float len = glm::length(ab);
			best.tangent = ab / len;
		}
	}
	best.normal = glm::vec2(-best.tangent.y, best.tangent.x);
	best.lateral = glm::dot(p - best.closest, best.normal);
	if (best.s >= length) best.s -= length;
	return best;
}

glm::vec2 TrackModel::point_at(float s, float lateral) const {
	float f = std::fmod(s / ds, float(n));
	if (f < 0.0f) f += float(n);
	int i = int(f);
	float t = f - float(i);
	glm::vec2 a = pts[i % n], b = pts[(i + 1) % n];
	glm::vec2 ta = tangent[i % n], tb = tangent[(i + 1) % n];
	glm::vec2 tg = glm::normalize(glm::mix(ta, tb, t));
	return glm::mix(a, b, t) + glm::vec2(-tg.y, tg.x) * lateral;
}

glm::vec2 TrackModel::tangent_at(float s) const {
	float f = std::fmod(s / ds, float(n));
	if (f < 0.0f) f += float(n);
	int i = int(f);
	float t = f - float(i);
	return glm::normalize(glm::mix(tangent[i % n], tangent[(i + 1) % n], t));
}

float TrackModel::speed_limit_at(float s) const {
	float f = std::fmod(s / ds, float(n));
	if (f < 0.0f) f += float(n);
	int i = int(f);
	return speed_limit[i % n];
}

//------------------------------------------------------------------ world

World::World(std::string const &track_file, uint32_t seed_) : seed(seed_) {
	track.load(track_file);
	reset();
}

void World::slot_coords(int i, float *s, float *lateral) {
	int row = i / 2, col = i % 2;
	*s = -(8.0f + 6.0f * float(row)) - 3.0f * float(col);
	*lateral = (col == 0) ? -1.2f : 1.2f;
}

void World::reset() {
	rng.state = seed ? seed : 1;
	for (int i = 0; i < 4; ++i) rng.next(); //warm up
	countdown = CountdownSeconds;
	race_time = 0.0f;
	num_finished = 0;

	racers.assign(NumRacers, Racer());
	for (int i = 0; i < NumRacers; ++i) {
		Racer &r = racers[i];
		float s, lat;
		slot_coords(i, &s, &lat);
		r.pos = track.point_at(s, lat);
		glm::vec2 t = track.tangent_at(s);
		r.heading = std::atan2(t.y, t.x);
		r.is_player = (i == PlayerIndex);
		r.skill = rng.range(0.975f, 1.0f);
		r.lane_pref = rng.range(-1.2f, 1.2f);
		r.lane = lat;
		update_tracking(r, true);
		r.progress = s; //(negative: behind the line)
		r.lap_s = track.project(r.pos, r.hint).s;
	}
}

//keep track of where each racer is on the circuit (progress, lap):
void World::update_tracking(Racer &r, bool global) {
	TrackModel::Proj p = track.project(r.pos, global ? -1 : r.hint);
	r.hint = p.index;
	r.lateral = p.lateral;
	if (global) {
		r.lap_s = p.s;
		return;
	}
	float delta = p.s - r.lap_s;
	if (delta > 0.5f * track.length) delta -= track.length;
	if (delta < -0.5f * track.length) delta += track.length;
	r.progress += delta;
	r.lap_s = p.s;
}

//----------------------------------------------------------------- AI

void World::run_ai(int index) {
	Racer &r = racers[index];
	float speed = glm::length(r.vel);

	//pick a lane: stay on our preferred line unless someone slower is right ahead
	float want_lane = r.lane_pref;
	bool close_ahead = false;
	for (int j = 0; j < int(racers.size()); ++j) {
		if (j == index || racers[j].finished) continue;
		Racer const &o = racers[j];
		float gap = o.progress - r.progress;
		if (gap > 0.0f && gap < 11.0f && std::abs(o.lateral - r.lateral) < 1.9f) {
			float side = (r.lateral >= o.lateral) ? 1.0f : -1.0f;
			float limit = track.half_width - 0.9f;
			want_lane = glm::clamp(o.lateral + side * 1.9f, -limit, limit);
			if (std::abs(want_lane) >= limit - 0.01f) want_lane = glm::clamp(o.lateral - side * 1.9f, -limit, limit);
			if (gap < 4.0f) close_ahead = true;
			break;
		}
	}
	float lane_limit = track.half_width - 0.9f;
	want_lane = glm::clamp(want_lane, -lane_limit, lane_limit);
	float lane_step = 5.0f * DT;
	r.lane += glm::clamp(want_lane - r.lane, -lane_step, lane_step);

	//steer toward a point ahead on the line:
	TrackModel::Proj p = track.project(r.pos, r.hint);
	float look = 6.0f + speed * 0.32f;
	glm::vec2 target = track.point_at(p.s + look, r.lane);
	glm::vec2 want = target - r.pos;
	float desired = std::atan2(want.y, want.x);
	float err = wrap_angle(desired - r.heading);
	//also correct for sliding (velocity pointing somewhere other than the nose):
	float err_v = 0.0f;
	if (speed > 4.0f) err_v = wrap_angle(desired - std::atan2(r.vel.y, r.vel.x));
	r.steer_in = glm::clamp(2.4f * (0.65f * err + 0.35f * err_v), -1.0f, 1.0f);

	//speed: look at the corner we are heading into
	float v_target = track.speed_limit_at(p.s + 8.0f + speed * 0.9f) * r.skill;
	if (speed > v_target + 0.5f) {
		r.throttle = 0.0f;
		r.brake = glm::clamp((speed - v_target) / 5.0f, 0.0f, 1.0f);
	} else {
		r.brake = 0.0f;
		r.throttle = glm::clamp((v_target - speed) / 3.0f + 0.35f, 0.0f, 1.0f);
	}
	if (close_ahead) r.throttle *= 0.4f;
	//don't mash the throttle while pointing the wrong way:
	if (std::abs(err) > 1.0f) r.throttle *= 0.3f;
}

//----------------------------------------------------------------- stepping

void World::step() {
	const float dt = DT;

	bool racing = (countdown <= 0.0f);
	if (!racing) {
		countdown -= dt;
		if (countdown <= 0.0f) { countdown = 0.0f; racing = true; }
	} else {
		race_time += dt;
	}

	//AI decisions:
	for (int i = 0; i < int(racers.size()); ++i) {
		Racer &r = racers[i];
		if (r.is_player && !player_autopilot) continue;
		if (!racing || r.finished) { r.throttle = 0.0f; r.brake = r.finished ? 0.6f : 0.0f; r.steer_in = 0.0f; continue; }
		run_ai(i);
		if (r.is_player && player_digital) { //quantize like a keyboard would
			r.steer_in = (std::abs(r.steer_in) < 0.3f) ? 0.0f : (r.steer_in > 0.0f ? 1.0f : -1.0f);
			r.throttle = (r.throttle > 0.5f) ? 1.0f : 0.0f;
			r.brake = (r.brake > 0.3f) ? 1.0f : 0.0f;
		}
	}

	//slipstream: being close behind someone cuts air drag
	for (Racer &r : racers) {
		r.draft = 0.0f;
		if (r.finished) continue;
		glm::vec2 f(std::cos(r.heading), std::sin(r.heading));
		glm::vec2 l(-f.y, f.x);
		for (Racer const &o : racers) {
			if (&o == &r || o.finished) continue;
			glm::vec2 d = o.pos - r.pos;
			float along = glm::dot(d, f);
			float side = std::abs(glm::dot(d, l));
			if (along > 1.5f && along < Tune::DraftRange && side < Tune::DraftWidth) {
				r.draft = std::max(r.draft, 1.0f - along / Tune::DraftRange);
			}
		}
	}

	//forces -> velocities, steering:
	for (Racer &r : racers) {
		float thr = r.throttle, brk = r.brake, str = r.steer_in;
		if (!racing) { thr = 0.0f; brk = 0.0f; str = 0.0f; }
		thr = glm::clamp(thr, 0.0f, 1.0f); brk = glm::clamp(brk, 0.0f, 1.0f); str = glm::clamp(str, -1.0f, 1.0f);

		glm::vec2 f(std::cos(r.heading), std::sin(r.heading));
		glm::vec2 l(-f.y, f.x);
		float vf = glm::dot(r.vel, f);
		float vl = glm::dot(r.vel, l);

		//longitudinal:
		float a_f = World::Accel * thr;
		a_f -= Tune::DragCoef * (1.0f - Tune::DraftStrength * r.draft) * vf * std::abs(vf);
		a_f -= Tune::RollDecel * glm::clamp(vf, -1.0f, 1.0f);
		if (brk > 0.0f) {
			if (vf > 0.4f) a_f -= World::BrakeDecel * brk;
			else if (vf > -Tune::MaxReverse) a_f -= Tune::ReverseAccel * brk;
		}
		if (thr > 0.0f && vf < 0.0f) a_f += 25.0f * thr; //turn around quickly when pulling away from reverse

		//lateral: tyres cancel sideways velocity, up to the grip limit (braking eats some of it):
		float G = World::GripMax * (1.0f - 0.3f * brk);
		float a_l = glm::clamp(-vl * Tune::GripGain, -G, G);

		r.vel += (f * a_f + l * a_l) * dt;

		//steering:
		float step = Tune::SteerRate * dt;
		r.steer += glm::clamp(str - r.steer, -step, step);
		float speed = glm::length(r.vel);
		float turn = r.steer * Tune::TurnRate / (1.0f + speed / Tune::TurnSpeedRef);
		turn *= glm::clamp(speed / 2.5f, 0.0f, 1.0f);
		if (vf < 0.0f) turn = -turn;
		r.heading = wrap_angle(r.heading + turn * dt);

		r.pos += r.vel * dt;
	}

	//contacts (a few passes; always in the same order):
	for (int iter = 0; iter < Tune::SolverIterations; ++iter) {
		const int nr = int(racers.size());
		for (int i = 0; i < nr; ++i) {
			if (racers[i].finished) continue;
			for (int j = i + 1; j < nr; ++j) {
				if (racers[j].finished) continue;
				Racer &a = racers[i], &b = racers[j];
				glm::vec2 d = b.pos - a.pos;
				float rsum = a.radius + b.radius;
				float dist2 = glm::dot(d, d);
				if (dist2 >= rsum * rsum) continue;
				float dist = std::sqrt(dist2);
				glm::vec2 n = (dist > 1e-6f) ? d / dist : glm::vec2(1.0f, 0.0f);
				float invSum = a.invMass + b.invMass;
				float pen = rsum - dist;
				a.pos -= n * (pen * a.invMass / invSum);
				b.pos += n * (pen * b.invMass / invSum);
				float vn = glm::dot(b.vel - a.vel, n);
				if (vn < 0.0f) {
					float jimp = -(1.0f + Tune::BallRestitution) * vn / invSum;
					a.vel -= n * (jimp * a.invMass);
					b.vel += n * (jimp * b.invMass);
				}
			}
		}
		//barriers:
		for (Racer &r : racers) {
			TrackModel::Proj p = track.project(r.pos, r.hint);
			r.hint = p.index;
			float limit = track.wall_dist - r.radius;
			float a = std::abs(p.lateral);
			if (a > limit) {
				glm::vec2 dir = (p.lateral > 0.0f) ? p.normal : -p.normal; //points into the wall
				r.pos -= dir * (a - limit);
				float vn = glm::dot(r.vel, dir);
				if (vn > 0.0f) r.vel -= dir * ((1.0f + Tune::WallRestitution) * vn);
				r.vel *= Tune::WallScrub;
			}
		}
	}

	//laps and finish:
	for (Racer &r : racers) {
		update_tracking(r, false);
		if (!r.finished && r.progress >= float(Laps) * track.length) {
			r.finished = true;
			r.finish_time = race_time;
			r.place = ++num_finished;
		}
	}
}

//----------------------------------------------------------------- queries

std::vector< int > World::ranking() const {
	std::vector< int > order(racers.size());
	for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		Racer const &ra = racers[a], &rb = racers[b];
		if (ra.finished != rb.finished) return ra.finished;
		if (ra.finished) return ra.place < rb.place;
		return ra.progress > rb.progress;
	});
	return order;
}

uint64_t World::hash() const {
	uint64_t h = 1469598103934665603ull;
	auto mix = [&](float f) {
		uint32_t u; std::memcpy(&u, &f, 4);
		h = (h ^ u) * 1099511628211ull;
	};
	for (Racer const &r : racers) { mix(r.pos.x); mix(r.pos.y); mix(r.vel.x); mix(r.vel.y); mix(r.heading); }
	return h;
}
