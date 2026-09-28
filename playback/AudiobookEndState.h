#pragma once

#include "playback/PlaybackTimeline.h"
#include <string>

struct AudiobookEndReport {
	std::string itemUri;
	PlaybackPosition position;
	bool optimistic = false;
	bool knownItemState = false;
	bool hasItem = true;
};

// Inspect a final report before it overwrites the playing clock or clears the
// chapter context/queue. Pausing before the end and other items cannot finish it.
inline bool
AudiobookReportReachesEnd(const std::string& currentUri,
	const PlaybackTimeline& timeline, const AudiobookEndReport& report, int64_t nowUs)
{
	if (currentUri.empty() || report.optimistic || report.position.isPlaying)
		return false;
	bool sameItem = report.itemUri == currentUri;
	bool emptyItem = report.itemUri.empty() && report.knownItemState && !report.hasItem;
	if (!sameItem && !emptyItem)
		return false;
	if (sameItem) {
		int32_t duration = report.position.durationMs > 0
			? report.position.durationMs : timeline.DurationMs();
		return duration > 0 && report.position.progressMs >= duration;
	}
	auto remaining = timeline.RemainingMs(nowUs);
	return remaining && *remaining == 0;
}

enum class AudiobookEndAction { NotAudiobook, AlreadyHandled, Complete };

// Player-looper state: one completion per chapter, shared by tick, final poll
// and local EndOfTrack. Explicit play/seek rearms replay of the same URI.
class AudiobookEndState {
public:
	void Reset() { fCompletedUri.clear(); }
	AudiobookEndAction Claim(const std::string& itemUri, const std::string& parentKind)
	{
		if (itemUri.empty() || parentKind != "audiobook")
			return AudiobookEndAction::NotAudiobook;
		if (fCompletedUri == itemUri)
			return AudiobookEndAction::AlreadyHandled;
		fCompletedUri = itemUri;
		return AudiobookEndAction::Complete;
	}

private:
	std::string fCompletedUri;
};
