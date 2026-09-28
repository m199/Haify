#pragma once

#include "playback/PlaybackMetadata.h"
#include <cstdint>
#include <string>

class BMessage;

struct PlaybackMessageData {
	bool isPlaying = false;
	int32_t progressMs = 0;
	int32_t durationMs = 0;
	int32_t volumePct = -1;
	bool optimistic = false;
	bool preserveCurrentArtwork = false;
	bool volumeAuthoritative = true;
	bool hasItem = true;
	bool knownItemState = false;
	std::string trackUri;
	std::string repeatState;
	bool shuffleState = false;
	PlaybackMetadata metadata;
	std::string deviceId;
	std::string deviceName;
	std::string deviceType;
};

struct ReplicantPlaybackState {
	PlaybackMetadata metadata;
	std::string trackUri;
	bool isPlaying = false;
	int32_t progressMs = 0;
	int32_t durationMs = 0;
	int32_t volumePct = -1;
	std::string repeatState = "off";
	bool shuffleState = false;
};

// Legacy permissive decoding and defaults are intentional. No new fields or
// message version. See docs/playback-state-contract.md for ordering/provenance.
namespace PlaybackStateMessages {
PlaybackMessageData ReadUpdate(const BMessage& message);
BMessage MakeReplicantState(const ReplicantPlaybackState& state);
ReplicantPlaybackState ReadReplicantState(const BMessage& message);
}
