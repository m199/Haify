#pragma once

#include <string>

enum class PlaybackMetadataSource {
	Reported,
	PlaybackCommand,
	Retained,
	ItemUriFallback,
	ClearedForItem
};

struct PlaybackMetadataField {
	std::string value;
	PlaybackMetadataSource source = PlaybackMetadataSource::Reported;
};

struct PlaybackMetadata {
	PlaybackMetadataField title;
	PlaybackMetadataField artist;
	PlaybackMetadataField albumId;
	PlaybackMetadataField artistId;
	PlaybackMetadataField itemKind;
	PlaybackMetadataField openUri;
	PlaybackMetadataField parentUri;
	PlaybackMetadataField parentKind;
	PlaybackMetadataField showId;
	PlaybackMetadataField audiobookId;
	PlaybackMetadataField artworkUrl;
};

struct PlaybackMetadataContext {
	std::string trackUri;
	bool optimistic = false;
	bool trackChanged = false;
	bool preserveCurrentArtwork = false;
};

// Pure resolution against the player-looper's current metadata. Reported means
// supplied by the caller, not confirmed by the remote service. Retained fields
// remain distinguishable from reported values until legacy wire serialization.
PlaybackMetadata ResolvePlaybackMetadata(const PlaybackMetadata& reported,
	const PlaybackMetadata& current, const PlaybackMetadataContext& context);

// Optimistic empty fields do not erase stored values. Artwork is committed
// separately by the owner, at the existing point just before publication.
void StorePlaybackMetadata(PlaybackMetadata& current,
	const PlaybackMetadata& resolved, bool optimistic);
