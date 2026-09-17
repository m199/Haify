#include "PlaylistMetadataRequests.h"
#include "PlaylistMetadataMessages.h"
#include "spotify/api/SpotifyApi.h"

#include <Messenger.h>

bool
PlaylistMetadataRequests::RefreshSnapshot(SpotifyApi& api,
	const std::string& playlistId, const BMessenger& target)
{
	if (playlistId.empty())
		return false;
	api.Playlists().InvalidatePlaylist(playlistId);
	PlaylistMetadataController controller(
		[&api](const std::string& id, JsonCallback done) {
			api.Playlists().GetPlaylist(id, done);
		}, {}, {}, {});
	return controller.Load({PlaylistMetadataKind::Playlist, playlistId},
		[target](const PlaylistMetadataResult& result) {
			BMessage message = MakePlaylistSnapshotMessage(result);
			target.SendMessage(&message);
		});
}

bool
PlaylistMetadataRequests::Load(SpotifyApi& api,
	const PlaylistMetadataRequest& request, const BMessenger& target)
{
	PlaylistMetadataController controller(
		[&api](const std::string& id, JsonCallback done) {
			api.Playlists().GetPlaylist(id, done);
		},
		[&api](const std::string& id, JsonCallback done) {
			api.Content().GetAlbum(id, done);
		},
		[&api](const std::string& id, JsonCallback done) {
			api.Content().GetShow(id, done);
		},
		[&api](JsonCallback done) { api.Profile().GetCurrentUserProfile(done); });
	return controller.Load(request, [target](const PlaylistMetadataResult& result) {
		BMessage message = MakePlaylistMetadataMessage(result);
		target.SendMessage(&message);
	});
}
