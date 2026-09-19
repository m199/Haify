#include "playlist/PlaylistCoverMessages.h"
#include "messages/Messages.h"
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Playlist cover message tests require assertions; compile without NDEBUG.
#endif
static void Check(bool condition) { assert(condition); }
static void ExpectRejected(const BMessage& message)
{
	PlaylistCoverResult result;
	result.command.playlistId = "untouched";
	Check(!ReadPlaylistCoverResultMessage(message, result));
	Check(result.command.playlistId == "untouched");
}

int main()
{
	for (int error = 0; error <= static_cast<int>(PlaylistCoverError::RefreshFailed); error++) {
		PlaylistCoverResult result;
		result.command = {(int64_t(1) << 40) + 7, "list"};
		result.error = static_cast<PlaylistCoverError>(error);
		result.status = 429;
		result.coverUrl = "https://image/cover";
		BMessage message = MakePlaylistCoverResultMessage(result);
		PlaylistCoverResult actual;
		Check(ReadPlaylistCoverResultMessage(message, actual));
		Check(actual.command.requestId == result.command.requestId);
		Check(actual.command.playlistId == "list" && actual.error == result.error && actual.status == 429);
		Check(actual.coverUrl == (error == 0 ? result.coverUrl : ""));
		for (const char* field : {"request_id", "playlist_id", "error", "status", "cover_url"}) {
			BMessage invalid(message);
			invalid.RemoveName(field);
			ExpectRejected(invalid);
			invalid.AddFloat(field, 1.0f);
			ExpectRejected(invalid);
		}
		message.what = MSG_PLAYLIST_COVER_UPDATE;
		ExpectRejected(message);
		result.command.requestId = 0;
		ExpectRejected(MakePlaylistCoverResultMessage(result));
		result.command.requestId = 1;
		result.command.playlistId.clear();
		ExpectRejected(MakePlaylistCoverResultMessage(result));
		result.command.playlistId = "list";
		result.error = static_cast<PlaylistCoverError>(99);
		ExpectRejected(MakePlaylistCoverResultMessage(result));
		result.error = PlaylistCoverError::None;
		result.coverUrl.clear();
		ExpectRejected(MakePlaylistCoverResultMessage(result));
	}
	std::puts("Playlist cover message tests passed.");
}
