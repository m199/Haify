#include "LibrespotArguments.h"
#include "Config.h"
#include "settings/SettingsController.h"

#include <sstream>

namespace {
void
AppendAdditionalArguments(std::vector<std::string>& args,
	const std::string& additionalArgs, bool hasEnableOAuthArgument)
{
	std::istringstream iss(additionalArgs);
	std::string token;
	while (iss >> token) {
		if (token == "-j" || token == "--enable-oauth") {
			if (hasEnableOAuthArgument)
				continue;
			hasEnableOAuthArgument = true;
		}
		args.push_back(token);
	}
}
}

void
AppendLibrespotPlaybackArguments(std::vector<std::string>& args,
	const HaifySettings& settings, bool hasEnableOAuthArgument)
{
	args.push_back("--backend");
	args.push_back(settings.librespotBackend.empty()
		? "sdl" : settings.librespotBackend);
	args.push_back("--bitrate");
	args.push_back(std::to_string(settings.librespotBitrate));
	args.push_back("--initial-volume");
	args.push_back(std::to_string(settings.librespotVolume));
	if (settings.librespotAutoplay) {
		args.push_back("--autoplay");
		args.push_back("on");
	}
	if (settings.librespotNormalization)
		args.push_back("--enable-volume-normalisation");
	args.push_back("--name");
	args.push_back(settings.librespotDeviceName.empty()
		? LIBRESPOT_DEVICE_NAME : settings.librespotDeviceName);
	if (!settings.librespotDeviceType.empty()) {
		args.push_back("--device-type");
		args.push_back(settings.librespotDeviceType);
	}
	if (settings.librespotDisableDiscovery)
		args.push_back("--disable-discovery");
	AppendAdditionalArguments(args, settings.librespotAdditionalArgs,
		hasEnableOAuthArgument);
}
