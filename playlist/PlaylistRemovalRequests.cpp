#include "PlaylistRemovalRequests.h"
#include "PlaylistRemovalMessages.h"
#include "spotify/api/PlaylistApi.h"

#include <Messenger.h>

bool
PlaylistRemovalRequests::Send(PlaylistApi& api, const PlaylistRemovalCommand& command,
	const BMessenger& target)
{
	return DispatchPlaylistRemoval(command,
		[&api](const PlaylistRemovalCommand& request, JsonCallback done) {
			api.RemovePlaylistItemsAtPositions(request.playlistId, request.items,
				request.snapshotId, done);
		},
		[&api](const PlaylistRemovalCommand& request, JsonCallback done) {
			api.RemovePlaylistItemsFromKnownSnapshot(request.playlistId, request.items,
				request.snapshotId, request.knownPlaylistUris, done);
		},
		[target](const PlaylistRemovalResult& result) {
			BMessage message = MakePlaylistRemovalMessage(result);
			target.SendMessage(&message);
		});
}
