#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>

using namespace geode::prelude;

struct AudioDevices {
	std::vector<std::string> all;
	std::string mic;
	std::string internal;
};

// The screen recorder only works on Windows, so on Android this is a stub.
class ScreenRecorder {
public:
	static ScreenRecorder& get() {
		static ScreenRecorder r;
		return r;
	}
	bool isRecording() const { return false; }
	std::filesystem::path outputPath() const { return {}; }
	std::filesystem::path recordingsDir() const { return Mod::get()->getSaveDir() / "recordings"; }
	static AudioDevices listAudioDevices(std::string const&) { return {}; }
	std::string start(bool, bool) { return "The screen recorder is only available on Windows."; }
	void stop() {}
	void captureFrame() {}
};
