#include "playlist/PlaylistWriteMessages.h"
#include "messages/Messages.h"
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Playlist write message tests require assertions; compile without NDEBUG.
#endif

static void Check(bool condition) { assert(condition); }

static void ExpectRejected(const BMessage& message)
{
	PlaylistWriteResult output;
	output.playlistId = "untouched";
	Check(!ReadPlaylistWriteMessage(message, output));
	Check(output.playlistId == "untouched");
}

int main()
{
	for (auto kind : {PlaylistWriteKind::Clear, PlaylistWriteKind::Add}) {
		for (bool ok : {false, true}) {
			PlaylistWriteResult result;
			result.kind = kind;
			result.playlistId = "list";
			result.requestId = (int64_t(1) << 40) + 1;
			result.ok = ok;
			result.status = ok ? 201 : 409;
			result.snapshotId = "after";
			BMessage message = MakePlaylistWriteMessage(result);
			PlaylistWriteResult actual;
			Check(ReadPlaylistWriteMessage(message, actual));
			Check(actual.kind == kind && actual.requestId == result.requestId);
			Check(actual.playlistId == "list" && actual.ok == ok && actual.status == result.status);
			Check(actual.snapshotId == (ok ? "after" : ""));
			for (const char* field : {"request_id", "playlist_id", "ok", "status", "snapshot_id"}) {
				BMessage invalid(message);
				invalid.RemoveName(field);
				ExpectRejected(invalid);
				invalid.AddFloat(field, 1.0f);
				ExpectRejected(invalid);
			}
			message.what = MSG_PLAYLIST_REORDER_RESULT;
			ExpectRejected(message);
			result.requestId = 0;
			ExpectRejected(MakePlaylistWriteMessage(result));
			result.requestId = 1;
			result.playlistId.clear();
			ExpectRejected(MakePlaylistWriteMessage(result));
		}
	}
	std::puts("Playlist clear/add message tests passed.");
}
