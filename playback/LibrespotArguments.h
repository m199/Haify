#pragma once

#include <string>
#include <vector>

struct HaifySettings;

// Pure mapping of a settings snapshot. The caller owns executable/cache/event
// arguments and process lifecycle; this appends playback and additional options.
// Additional options retain whitespace tokenization (no shell interpretation).
void AppendLibrespotPlaybackArguments(std::vector<std::string>& args,
	const HaifySettings& settings, bool hasEnableOAuthArgument);
