#include "PlaylistWriteRequests.h"
#include "PlaylistWriteMessages.h"
#include "spotify/api/PlaylistApi.h"
#include <Messenger.h>

bool
PlaylistWriteRequests::Send(PlaylistApi& api, const PlaylistWriteCommand& command,
	const BMessenger& target)
{
	return DispatchPlaylistWrite(command,
		[&api](const PlaylistWriteCommand& request, JsonCallback done) {
			if (request.kind == PlaylistWriteKind::Clear)
				api.ReplacePlaylistItems(request.playlistId, {}, done);
			else
				api.AddTrackToPlaylist(request.playlistId, request.uri, done);
		}, [target](const PlaylistWriteResult& result) {
			BMessage message = MakePlaylistWriteMessage(result);
			target.SendMessage(&message);
		});
}
