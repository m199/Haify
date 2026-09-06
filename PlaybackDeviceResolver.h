#pragma once

#include <Message.h>

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

struct PlaybackDeviceChoice {
	std::string id;
	std::string name;
	std::string type;
	bool active = false;
};

std::string PlaybackDeviceDisplayName(const std::string& id,
	const std::string& name, const std::string& type);
void AddPlaybackDeviceChoicesFromJson(BMessage& message,
	const nlohmann::json& data, const std::string& activeDeviceName = "");
std::vector<PlaybackDeviceChoice> PlaybackDeviceChoicesFromMessage(
	BMessage* message);
bool FindActivePlaybackDeviceId(BMessage* message, std::string& deviceId);
