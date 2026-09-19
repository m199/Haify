#include "playback/LibrespotArguments.h"
#include "settings/SettingsController.h"
#include "app/Config.h"

#include <cassert>
#include <iostream>

#ifdef NDEBUG
#error Argument tests require assertions; compile without NDEBUG.
#endif

static void
TestDefaultsAndPrefix()
{
	HaifySettings settings;
	std::vector<std::string> args{"/boot/home/custom player", "--cache",
		"/boot/home/cache folder", "--onevent=/boot/home/hook 123"};
	AppendLibrespotPlaybackArguments(args, settings, false);
	const std::vector<std::string> expected{
		"/boot/home/custom player", "--cache", "/boot/home/cache folder",
		"--onevent=/boot/home/hook 123", "--backend", "sdl", "--bitrate", "320",
		"--initial-volume", "100", "--autoplay", "on", "--name", LIBRESPOT_DEVICE_NAME};
	assert(args == expected);
}

static void
TestConfiguredOptions()
{
	HaifySettings settings;
	settings.librespotBackend = "custom-backend";
	settings.librespotBitrate = 160;
	settings.librespotVolume = 42;
	settings.librespotAutoplay = false;
	settings.librespotNormalization = true;
	settings.librespotDeviceName = "Living room";
	settings.librespotDeviceType = "speaker";
	settings.librespotDisableDiscovery = true;
	settings.librespotAdditionalArgs = "--verbose --bitrate 96";
	std::vector<std::string> args;
	AppendLibrespotPlaybackArguments(args, settings, false);
	const std::vector<std::string> expected{
		"--backend", "custom-backend", "--bitrate", "160", "--initial-volume", "42",
		"--enable-volume-normalisation", "--name", "Living room", "--device-type",
		"speaker", "--disable-discovery", "--verbose", "--bitrate", "96"};
	assert(args == expected);
}

static void
TestOAuthDeduplication()
{
	for (bool registering : {false, true}) {
		for (const std::string first : {"-j", "--enable-oauth"}) {
			HaifySettings settings;
			settings.librespotAdditionalArgs = first + "\t--verbose\n-j --enable-oauth";
			std::vector<std::string> args;
			if (registering)
				args.push_back("--enable-oauth");
			std::vector<std::string> expected = args;
			expected.insert(expected.end(), {"--backend", "sdl", "--bitrate", "320",
				"--initial-volume", "100", "--autoplay", "on", "--name", LIBRESPOT_DEVICE_NAME});
			if (!registering)
				expected.push_back(first);
			expected.push_back("--verbose");
			AppendLibrespotPlaybackArguments(args, settings, registering);
			assert(args == expected);
		}
	}
}

static void
TestLegacyTokenization()
{
	HaifySettings settings;
	std::vector<std::string> expected;
	AppendLibrespotPlaybackArguments(expected, settings, false);
	// This extraction preserves the existing parser, including literal quotes.
	settings.librespotAdditionalArgs = "  --name \"Two Words\"  --flag=a=b\t$HOME  ";
	expected.insert(expected.end(), {"--name", "\"Two", "Words\"", "--flag=a=b", "$HOME"});
	std::vector<std::string> args;
	AppendLibrespotPlaybackArguments(args, settings, false);
	assert(args == expected);
}

int
main()
{
	TestDefaultsAndPrefix();
	TestConfiguredOptions();
	TestOAuthDeduplication();
	TestLegacyTokenization();
	std::cout << "Librespot argument tests passed.\n";
}
