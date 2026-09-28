#include "messages/PlaybackStateMessages.h"
#include "messages/Messages.h"

#include <Message.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void
CheckCondition(bool condition, const char* expression, const char* function, int line)
{
	if (condition)
		return;
	std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, line, function, expression);
	std::abort();
}

#define Check(condition) CheckCondition((condition), #condition, __func__, __LINE__)


static void
CheckLegacyDefaults()
{
	BMessage message(MSG_REPLICANT_STATE);
	auto update = PlaybackStateMessages::ReadUpdate(message);
	Check(!update.isPlaying && update.progressMs == 0 && update.durationMs == 0);
	Check(update.volumePct == -1 && update.volumeAuthoritative);
	Check(!update.knownItemState && update.hasItem);
	Check(!update.optimistic && !update.preserveCurrentArtwork);
	Check(update.repeatState == "off" && !update.shuffleState);
	message.AddString("track_uri", "spotify:track:legacy");
	message.AddString("primary_open_uri", "");
	message.AddBool("has_item", false);
	message.AddBool("optimistic", true);
	message.AddBool("preserve_current_artwork", true);
	message.AddBool("volume_authoritative", false);
	update = PlaybackStateMessages::ReadUpdate(message);
	Check(update.knownItemState && !update.hasItem);
	Check(update.optimistic && update.preserveCurrentArtwork && !update.volumeAuthoritative);
	Check(update.metadata.openUri.value.empty());
	auto state = PlaybackStateMessages::ReadReplicantState(message);
	Check(state.metadata.openUri.value == "spotify:track:legacy");
	Check(state.metadata.openUri.source == PlaybackMetadataSource::ItemUriFallback);
	// Preserve permissive defaults for wrong types and first-value duplicate reads.
	BMessage malformed;
	malformed.AddString("is_playing", "yes");
	malformed.AddInt32("volume_percent", 23);
	malformed.AddInt32("volume_percent", 42);
	state = PlaybackStateMessages::ReadReplicantState(malformed);
	Check(!state.isPlaying && state.volumePct == 23);
}


static void
CheckItemStatePresence()
{
	BMessage missing;
	auto update = PlaybackStateMessages::ReadUpdate(missing);
	Check(!update.knownItemState && update.hasItem);

	BMessage wrongType;
	wrongType.AddString("has_item", "false");
	update = PlaybackStateMessages::ReadUpdate(wrongType);
	Check(!update.knownItemState && update.hasItem);

	BMessage known;
	known.AddBool("has_item", false);
	update = PlaybackStateMessages::ReadUpdate(known);
	Check(update.knownItemState && !update.hasItem);
	known.ReplaceBool("has_item", true);
	update = PlaybackStateMessages::ReadUpdate(known);
	Check(update.knownItemState && update.hasItem);
}


static void
CheckWireFields()
{
	ReplicantPlaybackState state;
	state.trackUri = "track";
	state.isPlaying = true;
	state.progressMs = 1234;
	state.durationMs = 90000;
	state.volumePct = 44;
	state.repeatState = "context";
	state.shuffleState = true;
	struct Field {
		const char* wire;
		PlaybackMetadataField PlaybackMetadata::* member;
	};
	const Field fields[] = {
		{"title", &PlaybackMetadata::title}, {"artist", &PlaybackMetadata::artist},
		{"album_id", &PlaybackMetadata::albumId}, {"artist_id", &PlaybackMetadata::artistId},
		{"item_kind", &PlaybackMetadata::itemKind}, {"primary_open_uri", &PlaybackMetadata::openUri},
		{"parent_uri", &PlaybackMetadata::parentUri}, {"parent_kind", &PlaybackMetadata::parentKind},
		{"show_id", &PlaybackMetadata::showId}, {"audiobook_id", &PlaybackMetadata::audiobookId},
		{"artwork_url", &PlaybackMetadata::artworkUrl}
	};
	for (const auto& field : fields)
		state.metadata.*(field.member) = {field.wire, PlaybackMetadataSource::Retained};
	BMessage message = PlaybackStateMessages::MakeReplicantState(state);
	Check(message.what == MSG_REPLICANT_STATE && message.CountNames(B_ANY_TYPE) == 18);
	for (const auto& field : fields) {
		const char* value = nullptr;
		Check(message.FindString(field.wire, &value) == B_OK);
		Check(std::strcmp(value, field.wire) == 0);
	}
	Check(message.GetBool("is_playing", false) && message.GetBool("shuffle_state", false));
	Check(message.GetInt32("progress_ms", 0) == 1234);
	Check(message.GetInt32("duration_ms", 0) == 90000);
	Check(message.GetInt32("volume_percent", -1) == 44);
	Check(std::strcmp(message.GetString("track_uri", ""), "track") == 0);
	Check(std::strcmp(message.GetString("repeat_state", ""), "context") == 0);
	auto read = PlaybackStateMessages::ReadReplicantState(message);
	for (const auto& field : fields) {
		Check((read.metadata.*(field.member)).value == field.wire);
		Check((read.metadata.*(field.member)).source == PlaybackMetadataSource::Reported);
	}
	state.volumePct = -1;
	message = PlaybackStateMessages::MakeReplicantState(state);
	Check(message.CountNames(B_ANY_TYPE) == 17 && !message.HasInt32("volume_percent"));
}


int
main()
{
	CheckLegacyDefaults();
	CheckItemStatePresence();
	CheckWireFields();
	std::puts("Playback state message tests passed");
	return 0;
}
