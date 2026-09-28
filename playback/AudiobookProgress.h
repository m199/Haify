#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

enum class AudiobookProgressSource {
	ResumePoint,
	PlaybackSnapshot
};

struct AudiobookChapterProgress {
	bool known = false;
	bool fullyPlayed = false;
	int32_t positionMs = 0;
	AudiobookProgressSource source = AudiobookProgressSource::ResumePoint;
};

struct AudiobookPlaybackPosition {
	std::string itemUri;
	int32_t positionMs = -1;
	int32_t durationMs = 0;
};

// A view-local overlay, never a write to Spotify's resume point. Exact chapter
// identity is required, including when book metadata is missing or retained.
inline std::optional<AudiobookChapterProgress>
ResolveAudiobookChapterProgress(const AudiobookChapterProgress& current,
	const std::string& chapterUri, int32_t chapterDurationMs,
	const AudiobookPlaybackPosition& playback)
{
	if (chapterUri.empty() || chapterUri != playback.itemUri
			|| playback.positionMs < 0)
		return std::nullopt;
	int32_t duration = playback.durationMs > 0
		? playback.durationMs : chapterDurationMs;
	int32_t position = duration > 0
		? std::min(playback.positionMs, duration) : playback.positionMs;
	// "Done" describes reaching this chapter's end, not a write to Spotify's
	// cloud resume point. With no duration, keep an existing completion flag.
	bool fullyPlayed = duration > 0 ? position >= duration
		: current.known && current.fullyPlayed;
	return AudiobookChapterProgress{true, fullyPlayed, position,
		AudiobookProgressSource::PlaybackSnapshot};
}
