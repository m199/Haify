#include "DiscoverLibraryRequests.h"
#include "DiscoverMessages.h"
#include "DiscoverRowFactory.h"
#include "spotify/api/SpotifyApi.h"
#include "spotify/api/SpotifyResponse.h"

namespace {
BMessage
ResultMessage(uint32 what, const DiscoverLibraryRequest& request, bool ok, int32 status)
{
	BMessage result(what);
	result.AddInt32(MessageFields::Tab, request.tab);
	result.AddInt32(MessageFields::Generation, request.generation);
	result.AddString(MessageFields::Uri, request.uri.c_str());
	result.AddBool(MessageFields::Ok, ok);
	result.AddInt32(MessageFields::Status, status);
	return result;
}
}

void
DiscoverLibraryRequests::Write(SpotifyApi& api, const DiscoverLibraryWriteRequest& request,
	const DiscoverAsyncToken& token, const BMessenger& target)
{
	auto done = [request, token, target](bool ok, const nlohmann::json& data) {
		bool checking = request.kind == DiscoverLibraryWriteKind::EnsureSaved;
		bool valid = !checking || (data.is_array() && data.size() == 1 && data[0].is_boolean());
		BMessage result = DiscoverMessages::OperationResult(MSG_DISCOVER_LIBRARY_WRITE_RESULT,
			token, ok && valid, SpotifyResponseStatus(data));
		DiscoverMessages::AddLibraryWrite(result, request);
		result.AddBool(MessageFields::ApiOk, ok);
		result.AddBool(MessageFields::ResponseValid, valid);
		result.AddBool(MessageFields::Saved, checking && valid && data[0].get<bool>());
		target.SendMessage(&result);
	};
	bool dispatched = api.DispatchForAccount(token.context.accountId, [&]() {
		if (request.kind == DiscoverLibraryWriteKind::EnsureSaved)
			api.Library().CheckLibraryItems({request.uri}, done);
		else if (request.kind == DiscoverLibraryWriteKind::Save)
			api.Library().SaveLibraryItems({request.uri}, done);
		else if (SpotifyItemKindForUri(request.uri) == kSpotifyItemAudiobook)
			api.Library().RemoveSavedAudiobook(SpotifyItemIdForUri(request.uri), done);
		else
			api.Library().RemoveLibraryItems({request.uri}, done);
	});
	if (!dispatched)
		done(false, {{"status", -1}, {"error", "session_changed"}});
}

void
DiscoverLibraryRequests::CheckMembership(SpotifyApi& api,
	const DiscoverLibraryRequest& request, const BMessenger& target)
{
	api.Library().CheckLibraryItems({request.uri}, [request, target](bool ok,
			const nlohmann::json& data) {
		bool valid = ok && data.is_array() && !data.empty() && data[0].is_boolean();
		BMessage result = ResultMessage(MSG_DISCOVER_MEMBERSHIP_CACHED, request,
			valid, SpotifyResponseStatus(data));
		result.AddBool(MessageFields::ApiOk, ok);
		result.AddBool(MessageFields::ResponseValid, valid);
		result.AddBool(MessageFields::Saved, valid && data[0].get<bool>());
		target.SendMessage(&result);
	});
}

void
DiscoverLibraryRequests::ResolveAddition(SpotifyApi& api,
	const DiscoverLibraryRequest& request, bool showProgress,
	const std::string& doneLabel, const BMessenger& target)
{
	std::string id = SpotifyItemIdForUri(request.uri);
	if (id.empty())
		return;
	JsonCallback done = [request, showProgress, doneLabel, target](bool ok,
			const nlohmann::json& item) {
		DiscoverRowData row;
		bool mapped = ok && item.is_object() && DiscoverRowFactory::BuildResolvedLibraryRow(
			request.tab, request.uri, item, showProgress, doneLabel, row);
		BMessage result = ResultMessage(MSG_DISCOVER_LIBRARY_RESOLVED, request,
			mapped, SpotifyResponseStatus(item));
		result.AddBool(MessageFields::ApiOk, ok);
		result.AddBool(MessageFields::ResponseValid, mapped);
		if (mapped)
			DiscoverMessages::AppendRows(result, {row}, 0, 1);
		target.SendMessage(&result);
	};
	switch (request.tab) {
		case TAB_SAVED_ALBUMS: api.Content().GetAlbum(id, done); break;
		case TAB_PODCASTS: api.Content().GetShow(id, done); break;
		case TAB_FOLLOWED_ARTISTS: api.Artists().GetArtist(id, done); break;
		case TAB_SAVED_EPISODES: api.Content().GetEpisode(id, done); break;
		case TAB_AUDIOBOOKS: api.Content().GetAudiobook(id, done); break;
		default: break;
	}
}
