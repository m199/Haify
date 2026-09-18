#include "PlaybackDeviceResolver.h"

#include <Catalog.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaybackDeviceResolver"

static bool
IsLikelyHexIdentifier(const std::string& value)
{
	if (value.size() < 16)
		return false;

	for (unsigned char character : value) {
		if (!std::isxdigit(character))
			return false;
	}
	return true;
}


std::string
PlaybackDeviceDisplayName(const std::string& id, const std::string& name,
	const std::string& type)
{
	if (!name.empty() && name != id && !IsLikelyHexIdentifier(name))
		return name;

	std::string display = type.empty() ? B_TRANSLATE("Device") : type;
	std::string shortId = id.empty() ? name : id;
	if (!shortId.empty()) {
		const size_t idLength = std::min<size_t>(8, shortId.size());
		display += " (";
		display += shortId.substr(0, idLength);
		display += ")";
	}
	return display;
}


void
AddPlaybackDeviceChoicesFromJson(BMessage& message, const nlohmann::json& data,
	const std::string& activeDeviceName)
{
	if (!data.contains("devices") || !data["devices"].is_array())
		return;

	for (const auto& device : data["devices"]) {
		if (!device.is_object())
			continue;
		std::string id = device.value("id", "");
		if (id.empty())
			continue;
		std::string name = device.value("name", "");
		message.AddString("id", id.c_str());
		message.AddString("name", name.c_str());
		message.AddString("type",
			device.value("type", std::string()).c_str());
		message.AddBool("active", device.value("is_active", false)
			&& (activeDeviceName.empty() || name == activeDeviceName));
	}
}


std::vector<PlaybackDeviceChoice>
PlaybackDeviceChoicesFromMessage(BMessage* message)
{
	std::vector<PlaybackDeviceChoice> devices;
	if (!message)
		return devices;

	int32 index = 0;
	const char* id = nullptr;
	while (message->FindString("id", index, &id) == B_OK) {
		PlaybackDeviceChoice device;
		device.id = id ? id : "";
		const char* name = message->FindString("name", index);
		const char* type = message->FindString("type", index);
		device.name = name ? name : "";
		device.type = type ? type : "";
		message->FindBool("active", index, &device.active);
		if (!device.id.empty())
			devices.push_back(device);
		index++;
	}
	return devices;
}


bool
FindActivePlaybackDeviceId(BMessage* message, std::string& deviceId)
{
	if (!message)
		return false;

	int32 index = 0;
	const char* id = nullptr;
	while (message->FindString("id", index, &id) == B_OK) {
		bool active = false;
		message->FindBool("active", index, &active);
		if (active && id && id[0]) {
			deviceId = id;
			return true;
		}
		index++;
	}
	return false;
}
