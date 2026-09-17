#include "PlaylistPageMessages.h"
#include "Messages.h"

#include <utility>

namespace {
bool
ReadRequest(const BMessage& message, PlaylistPageRequest& request)
{
	int32 source = -1;
	int64 generation = 0;
	int64 requestId = 0;
	const char* id = nullptr;
	if (message.FindInt64("load_generation", &generation) != B_OK
			|| message.FindInt64("page_request_id", &requestId) != B_OK
			|| generation <= 0 || requestId <= 0
			|| message.FindInt32("page_source", &source) != B_OK
			|| source < 0 || source >= static_cast<int32>(PlaylistPageSource::Unsupported)
			|| message.FindString("content_id", &id) != B_OK || !id) {
		return false;
	}
	request.source = static_cast<PlaylistPageSource>(source);
	request.token = {generation, requestId};
	request.id = id;
	request.headRefresh = message.what == MSG_PLAYLIST_PODCAST_HEAD_PAGE;
	return message.FindInt32("offset", &request.offset) == B_OK
		&& message.FindInt32("search_generation", &request.searchGeneration) == B_OK;
}

bool
MatchesCode(const BMessage& message, const PlaylistPageResult& result)
{
	bool podcast = result.request.source == PlaylistPageSource::Podcast;
	switch (message.what) {
		case MSG_PLAYLIST_PODCAST_HEAD_PAGE: return podcast;
		case MSG_PLAYLIST_EPISODE_PAGE: return podcast && result.ok;
		case MSG_PLAYLIST_TRACK_PAGE: return !podcast && result.ok;
		case MSG_PLAYLIST_PAGE_FAILED: return !result.ok;
		default: return false;
	}
}

void
AddRow(BMessage& message, const PlaylistPageRow& row, bool podcast,
	const std::string& unavailableEpisodeTitle)
{
	message.AddInt32("number", row.number);
	message.AddString(MessageFields::Title,
		(row.unavailable ? unavailableEpisodeTitle : row.title).c_str());
	message.AddString(MessageFields::TrackUri, row.uri.c_str());
	message.AddString(MessageFields::Duration, row.duration.c_str());
	if (podcast) {
		message.AddString("description", row.description.c_str());
		message.AddString("date", row.date.c_str());
		return;
	}
	message.AddString(MessageFields::Artist, row.artist.c_str());
	message.AddString("artistUri", row.artistUri.c_str());
	message.AddString("bpm", "");
	message.AddString("key", "");
	message.AddString(MessageFields::Album, row.album.c_str());
	message.AddString(MessageFields::AlbumUri, row.albumUri.c_str());
}
}

BMessage
MakePlaylistPageMessage(const PlaylistPageResult& result,
	const std::string& unavailableEpisodeTitle)
{
	bool podcast = result.request.source == PlaylistPageSource::Podcast;
	uint32 code = result.request.headRefresh ? MSG_PLAYLIST_PODCAST_HEAD_PAGE
		: (!result.ok ? MSG_PLAYLIST_PAGE_FAILED
			: (podcast ? MSG_PLAYLIST_EPISODE_PAGE : MSG_PLAYLIST_TRACK_PAGE));
	BMessage message(code);
	message.AddBool(MessageFields::Ok, result.ok);
	message.AddBool(MessageFields::ResponseValid, result.responseValid);
	message.AddInt32(MessageFields::Status, result.status);
	message.AddInt32("retry_after", result.retryAfter);
	message.AddInt32("search_generation", result.request.searchGeneration);
	message.AddInt64("load_generation", result.request.token.generation);
	message.AddInt64("page_request_id", result.request.token.requestId);
	message.AddInt32("page_source", static_cast<int32>(result.request.source));
	message.AddString("content_id", result.request.id.c_str());
	message.AddInt32("offset", result.request.offset);
	if (!result.ok)
		return message;
	if (!result.request.headRefresh) {
		// Existing track pages use BOOL; episode pages use INT32.
		if (podcast)
			message.AddInt32("append", result.request.offset > 0 ? 1 : 0);
		else
			message.AddBool("append", result.request.offset > 0);
	}
	message.AddInt32("total", result.total);
	message.AddInt32("page_count", result.pageCount);
	message.AddInt32(MessageFields::NextOffset, result.nextOffset);
	for (const auto& row : result.rows)
		AddRow(message, row, podcast, unavailableEpisodeTitle);
	return message;
}

bool
ReadPlaylistPageHeader(const BMessage& message, PlaylistPageResult& result)
{
	PlaylistPageResult parsed;
	if (!ReadRequest(message, parsed.request)
			|| message.FindBool(MessageFields::Ok, &parsed.ok) != B_OK
			|| message.FindBool(MessageFields::ResponseValid, &parsed.responseValid) != B_OK
			|| message.FindInt32(MessageFields::Status, &parsed.status) != B_OK
			|| message.FindInt32("retry_after", &parsed.retryAfter) != B_OK
			|| !MatchesCode(message, parsed)) {
		return false;
	}
	if (parsed.ok && (!parsed.responseValid
			|| message.FindInt32("total", &parsed.total) != B_OK
			|| message.FindInt32("page_count", &parsed.pageCount) != B_OK
			|| message.FindInt32(MessageFields::NextOffset, &parsed.nextOffset) != B_OK)) {
		return false;
	}
	result = std::move(parsed);
	return true;
}

BMessage
MakePlaylistSearchMessage(bool retry, int32_t generation)
{
	BMessage message(retry ? MSG_PLAYLIST_RETRY_SEARCH : MSG_PLAYLIST_APPLY_SEARCH);
	message.AddInt32("search_generation", generation);
	return message;
}

bool
ReadPlaylistSearchGeneration(const BMessage& message, int32_t& generation)
{
	int32 parsed = 0;
	if ((message.what != MSG_PLAYLIST_APPLY_SEARCH && message.what != MSG_PLAYLIST_RETRY_SEARCH)
			|| message.FindInt32("search_generation", &parsed) != B_OK || parsed <= 0)
		return false;
	generation = parsed;
	return true;
}
