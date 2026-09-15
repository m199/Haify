#include "DiscoverPlaylistRequests.h"
#include "DiscoverMessages.h"
#include "DiscoverRowFactory.h"
#include "spotify/api/SpotifyApi.h"
#include "spotify/api/SpotifyResponse.h"

void
DiscoverPlaylistRequests::Mutate(SpotifyApi& api, const DiscoverPlaylistMutationRequest& request,
	const DiscoverAsyncToken& token, const BMessenger& target)
{
	auto done = [request, token, target](bool ok, const nlohmann::json& data) {
		BMessage result = DiscoverMessages::OperationResult(MSG_DISCOVER_PLAYLIST_MUTATION_RESULT,
			token, ok, SpotifyResponseStatus(data));
		result.AddString(MessageFields::Id, request.id.c_str());
		result.AddInt32(MessageFields::Generation, request.generation);
		result.AddString(MessageFields::Operation, DiscoverOperationName(request.operation));
		target.SendMessage(&result);
	};
	bool dispatched = api.DispatchForAccount(token.context.accountId, [&]() {
		if (request.operation == DiscoverChangeOperation::Rename)
			api.Playlists().RenamePlaylist(request.id, request.name, done);
		else if (request.operation == DiscoverChangeOperation::Remove)
			api.Playlists().UnfollowPlaylist(request.id, done);
	});
	if (!dispatched)
		done(false, {{"status", -1}, {"error", "session_changed"}});
}

void
DiscoverPlaylistRequests::Create(SpotifyApi& api, const std::string& name,
	const DiscoverAsyncToken& token, const BMessenger& target)
{
	auto done = [name, token, target](bool ok, const nlohmann::json& data) {
		BMessage result = DiscoverMessages::OperationResult(MSG_DISCOVER_PLAYLIST_CREATE_RESULT,
			token, ok, SpotifyResponseStatus(data));
		if (ok) {
			auto created = DiscoverRowFactory::CreatedPlaylist(data, name);
			if (created) {
				result.AddString(MessageFields::Id, SpotifyItemIdForUri(created->uris[0]).c_str());
				result.AddString(MessageFields::Name, created->vals[0].c_str());
				result.AddString(MessageFields::Owner, created->vals[1].c_str());
			}
		}
		target.SendMessage(&result);
	};
	bool dispatched = api.DispatchForAccount(token.context.accountId, [&]() {
		api.Playlists().CreatePlaylist(name, done);
	});
	if (!dispatched)
		done(false, {{"status", -1}, {"error", "session_changed"}});
}

void
DiscoverPlaylistRequests::AddItem(SpotifyApi& api, const std::string& playlistId,
	const std::string& itemUri, const DiscoverAsyncToken& token, const BMessenger& target)
{
	auto done = [token, target](bool ok, const nlohmann::json& data) {
		BMessage result = DiscoverMessages::OperationResult(MSG_DISCOVER_PLAYLIST_DROP_RESULT,
			token, ok, SpotifyResponseStatus(data));
		target.SendMessage(&result);
	};
	bool dispatched = api.DispatchForAccount(token.context.accountId, [&]() {
		api.Playlists().AddTrackToPlaylist(playlistId, itemUri, done);
	});
	if (!dispatched)
		done(false, {{"status", -1}, {"error", "session_changed"}});
}
