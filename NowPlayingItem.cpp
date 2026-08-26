#include "NowPlayingItem.h"

#include <Message.h>

namespace {

void
AddString(BMessage& message, const char* field, const std::string& value)
{
	message.AddString(field, value.c_str());
}

}


void
NowPlayingItem::AddToMessage(BMessage& message) const
{
	AddString(message, "title", displayTitle);
	message.AddInt32("duration_ms", durationMs);
	AddString(message, "track_uri", itemUri);
	AddString(message, "artist", displaySubtitle);
	AddString(message, "album_id", albumId);
	AddString(message, "artist_id", artistId);
	AddString(message, "artwork_url", imageUrl);
	AddString(message, kNowPlayingItemKindField, itemKind);
	AddString(message, kNowPlayingPrimaryOpenUriField, primaryOpenUri);
	AddString(message, kNowPlayingParentUriField, parentUri);
	AddString(message, kNowPlayingParentKindField, parentKind);
	AddString(message, kNowPlayingShowIdField, showId);
	AddString(message, kNowPlayingAudiobookIdField, audiobookId);
}
