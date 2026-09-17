#include "PlaylistReorderRequests.h"
#include "PlaylistReorderMessages.h"
#include "spotify/api/PlaylistApi.h"

#include <Messenger.h>

bool
PlaylistReorderRequests::Send(PlaylistApi& api, const PlaylistReorderCommand& command,
	const BMessenger& target)
{
	return DispatchPlaylistReorder(command,
		[&api](const PlaylistReorderCommand& request, JsonCallback done) {
			api.ReorderPlaylistItems(request.playlistId, request.sourceIndex,
				request.insertBefore, request.rangeLength, request.snapshotId, done);
		},
		[target](const PlaylistReorderResult& result) {
			BMessage message = MakePlaylistReorderMessage(result);
			target.SendMessage(&message);
		});
}
