#include "PlaylistPageRequests.h"
#include "PlaylistPageMessages.h"
#include "spotify/api/SpotifyApi.h"

#include <Catalog.h>
#include <Messenger.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaylistWindow"

bool
PlaylistPageRequests::Load(SpotifyApi& api, const PlaylistPageRequest& request,
	const BMessenger& target)
{
	PlaylistPageController controller(
		[&api](int32_t offset, int32_t limit, JsonCallback done) {
			api.Library().GetSavedTracks(offset, limit, done);
		},
		[&api](const std::string& id, int32_t offset, int32_t limit, JsonCallback done) {
			api.Playlists().GetPlaylistTracks(id, offset, limit, done);
		},
		[&api](const std::string& id, int32_t offset, int32_t limit, JsonCallback done) {
			api.Content().GetAlbumTracks(id, offset, limit, done);
		},
		[&api](const std::string& id, int32_t offset, int32_t limit, JsonCallback done) {
			api.Content().GetShowEpisodes(id, offset, limit, done);
		});
	// Keep the existing translation catalog context after moving the producer.
	std::string unavailable = B_TRANSLATE("Unavailable episode");
	return controller.Load(request, [target, unavailable](const PlaylistPageResult& result) {
		BMessage message = MakePlaylistPageMessage(result, unavailable);
		target.SendMessage(&message);
	});
}
