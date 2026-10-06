//Headless check of the physics: determinism + "can the AI actually drive the circuit?"
#include "World.hpp"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
	std::string track = (argc > 1) ? argv[1] : "dist/silverstone.track";
	try {
		//determinism: two worlds, same seed, same (empty) inputs -> same hash every tick
		{
			World a(track, 7), b(track, 7);
			bool same = true;
			for (int i = 0; i < 120 * 60 && same; ++i) {
				a.step(); b.step();
				if (a.hash() != b.hash()) { same = false; std::printf("DIVERGED at tick %d\n", i); }
			}
			std::printf("determinism: %s (hash %016llx)\n", same ? "OK" : "FAIL", (unsigned long long)a.hash());
		}
		//reset reproduces the same run:
		{
			World a(track, 7);
			for (int i = 0; i < 120 * 20; ++i) a.step();
			uint64_t h1 = a.hash();
			a.reset();
			for (int i = 0; i < 120 * 20; ++i) a.step();
			std::printf("reset replay: %s\n", (h1 == a.hash()) ? "OK" : "FAIL");
		}
		//a ball driven with keyboard-style (on/off) inputs only:
		{
			World w(track, 3);
			w.player_autopilot = true;
			w.player_digital = true;
			int ticks = 0, off = 0;
			while (ticks < 120 * 400 && !w.racers[World::PlayerIndex].finished) {
				w.step(); ++ticks;
				if (std::abs(w.racers[World::PlayerIndex].lateral) > w.track.half_width) ++off;
			}
			Racer const &p = w.racers[World::PlayerIndex];
			std::printf("keyboard-style player: %s %d laps in %.1fs (place %d), off-track %.1f%% of the time\n", p.finished ? "finished" : "DID NOT finish", World::Laps, w.race_time, p.place, 100.0f * float(off) / float(std::max(1, ticks)));
		}
		//AI-only races (the player ball is driven by the AI too):
		for (uint32_t seed = 1; seed <= 4; ++seed) {
			World w(track, seed);
			w.player_autopilot = true;
			int ticks = 0;
			float max_lat = 0.0f; int offtrack_ticks = 0;
			while (ticks < 120 * 400 && w.num_finished < World::NumRacers) {
				w.step(); ++ticks;
				for (auto const &r : w.racers) {
					if (std::abs(r.lateral) > w.track.half_width) ++offtrack_ticks;
					if (std::abs(r.lateral) > max_lat) max_lat = std::abs(r.lateral);
				}
			}
			std::printf("seed %u: %d/%d finished %d laps after %.1fs (lap length %.0f); off-track %.1f%% of car-time, max lateral %.1f\n",
				seed, w.num_finished, World::NumRacers, World::Laps, w.race_time, w.track.length,
				100.0f * float(offtrack_ticks) / float(std::max(1, ticks * World::NumRacers)), max_lat);
			float first = 1e9f, last = 0.0f;
			for (auto const &r : w.racers) if (r.finished) { first = std::min(first, r.finish_time); last = std::max(last, r.finish_time); }
			std::printf("   first %.1fs last %.1fs; unfinished progress:", first, last);
			for (auto const &r : w.racers) if (!r.finished) std::printf(" %.0f(lat %.1f)", r.progress, r.lateral);
			std::printf("\n");
		}
	} catch (std::exception const &e) {
		std::printf("ERROR: %s\n", e.what());
		return 1;
	}
	return 0;
}
