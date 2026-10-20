#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <fstream>
#include <unordered_map>

using namespace geode::prelude;

struct MacroInput {
	uint32_t frame;
	uint8_t button;   // 1 = jump, 2 = left, 3 = right
	bool down;
	bool player1;
};

class MacroManager {
public:
	enum class Mode { Idle, Recording, Replaying };

	static MacroManager& get() {
		static MacroManager m;
		return m;
	}

	Mode mode = Mode::Idle;
	std::vector<MacroInput> inputs;
	uint32_t frame = 0;
	size_t cursor = 0;
	bool ready = false;
	bool injecting = false;
	bool checkpointLoaded = false;
	bool forcedTestMode = false;
	bool invalid = false;
	std::string levelKey;

	bool held[2][4] = {};
	struct CP {
		uint32_t frame;
		bool held[2][4];
	};
	std::unordered_map<CheckpointObject*, CP> cps;

	static bool opt(char const* key, bool def) {
		return Mod::get()->getSavedValue<bool>(key, def);
	}

	static std::string keyFor(GJGameLevel* level) {
		int id = level->m_levelID.value();
		if (id != 0) return std::to_string(id);
		return "local_" + std::to_string(std::hash<std::string>{}(std::string(level->m_levelName)));
	}

	std::filesystem::path pathFor(std::string const& key) const {
		return Mod::get()->getSaveDir() / (key + ".gdm");
	}

	bool macroFileExists() const {
		std::error_code ec;
		return !levelKey.empty() && std::filesystem::exists(pathFor(levelKey), ec);
	}

	bool save() {
		std::ofstream f(pathFor(levelKey), std::ios::binary);
		if (!f) return false;
		uint32_t n = static_cast<uint32_t>(inputs.size());
		f.write("GDM1", 4);
		f.write(reinterpret_cast<char const*>(&n), 4);
		for (auto const& in : inputs) {
			uint8_t d = in.down ? 1 : 0;
			uint8_t p = in.player1 ? 1 : 0;
			f.write(reinterpret_cast<char const*>(&in.frame), 4);
			f.write(reinterpret_cast<char const*>(&in.button), 1);
			f.write(reinterpret_cast<char const*>(&d), 1);
			f.write(reinterpret_cast<char const*>(&p), 1);
		}
		return static_cast<bool>(f);
	}

	bool load() {
		inputs.clear();
		std::ifstream f(pathFor(levelKey), std::ios::binary);
		if (!f) return false;
		char magic[4];
		uint32_t n = 0;
		f.read(magic, 4);
		f.read(reinterpret_cast<char*>(&n), 4);
		if (!f || memcmp(magic, "GDM1", 4) != 0 || n > 20'000'000) return false;
		inputs.reserve(n);
		for (uint32_t i = 0; i < n; i++) {
			MacroInput in{};
			uint8_t d = 0, p = 0;
			f.read(reinterpret_cast<char*>(&in.frame), 4);
			f.read(reinterpret_cast<char*>(&in.button), 1);
			f.read(reinterpret_cast<char*>(&d), 1);
			f.read(reinterpret_cast<char*>(&p), 1);
			if (!f) { inputs.clear(); return false; }
			in.down = d != 0;
			in.player1 = p != 0;
			inputs.push_back(in);
		}
		return true;
	}

	void clearSaved() {
		std::error_code ec;
		if (!levelKey.empty()) std::filesystem::remove(pathFor(levelKey), ec);
		inputs.clear();
	}

	void startLevel(GJGameLevel* level) {
		levelKey = keyFor(level);
		inputs.clear();
		cps.clear();
		frame = 0;
		cursor = 0;
		invalid = false;
		forcedTestMode = false;
		checkpointLoaded = false;
		mode = Mode::Idle;
		std::memset(held, 0, sizeof(held));
		ready = true;
	}

	// Decide what we're doing based on practice/normal mode and the toggles.
	void updateMode(PlayLayer* pl) {
		Mode prev = mode;
		bool practice = pl->m_isPracticeMode;

		if (practice && opt("record-macro", true)) mode = Mode::Recording;
		else if (!practice && opt("auto-replay", true) && macroFileExists()) mode = Mode::Replaying;
		else mode = Mode::Idle;

		if (mode == Mode::Replaying && prev != Mode::Replaying) {
			if (!load() || inputs.empty()) mode = Mode::Idle;
		}

		// A replayed run must never count as a legit completion.
		if (mode == Mode::Replaying) {
			pl->m_isTestMode = true;
			forcedTestMode = true;
		} else if (forcedTestMode) {
			pl->m_isTestMode = false;
			forcedTestMode = false;
		}
	}

	// Level restarted from the very start (no checkpoint).
	void fullReset() {
		frame = 0;
		cursor = 0;
		invalid = false;
		cps.clear();
		std::memset(held, 0, sizeof(held));
		if (mode == Mode::Recording) inputs.clear();
	}

	void record(bool down, int button, bool player1) {
		if (button < 0 || button > 3) return;
		inputs.push_back({frame, static_cast<uint8_t>(button), down, player1});
		held[player1 ? 0 : 1][button] = down;
	}

	void onCheckpointStored(CheckpointObject* cp) {
		if (mode != Mode::Recording) return;
		CP entry{};
		entry.frame = frame;
		std::memcpy(entry.held, held, sizeof(held));
		cps[cp] = entry;
	}

	void onCheckpointLoaded(CheckpointObject* cp) {
		checkpointLoaded = true;
		if (mode != Mode::Recording) return;

		auto it = cps.find(cp);
		if (it == cps.end()) {
			// we never saw this checkpoint being made, so we can't line the macro up with it
			invalid = true;
			return;
		}
		uint32_t f = it->second.frame;

		// throw away everything that happened after this checkpoint (the failed attempt)
		while (!inputs.empty() && inputs.back().frame >= f) inputs.pop_back();
		frame = f;

		// the game releases held buttons on respawn, so mirror that in the macro
		for (int p = 0; p < 2; p++) {
			for (int b = 1; b <= 3; b++) {
				if (it->second.held[p][b]) inputs.push_back({f, static_cast<uint8_t>(b), false, p == 0});
			}
		}
		std::memset(held, 0, sizeof(held));
	}

	std::string statusText() const {
		switch (mode) {
			case Mode::Recording:
				return fmt::format("MACRO REC  {} inputs{}", inputs.size(), invalid ? "  (INVALID)" : "");
			case Mode::Replaying:
				return fmt::format("REPLAY  {}/{}", cursor, inputs.size());
			default:
				return "";
		}
	}
};
