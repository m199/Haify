#include "DiscoverMessages.h"
#include "spotify/SpotifyUri.h"

void
DiscoverMessages::AddContext(BMessage& message, const DiscoverAsyncContext& context)
{
	message.AddString(MessageFields::AccountId, context.accountId.c_str());
	message.AddInt64(MessageFields::ContextEpoch, context.epoch);
}

std::optional<DiscoverAsyncContext>
DiscoverMessages::ReadContext(const BMessage& message)
{
	BMessage context;
	if (message.FindMessage(MessageFields::CommandContext, &context) == B_OK)
		return ReadContext(context);
	const char* account;
	int64 epoch;
	if (message.FindString(MessageFields::AccountId, &account) != B_OK
			|| message.FindInt64(MessageFields::ContextEpoch, &epoch) != B_OK)
		return std::nullopt;
	return DiscoverAsyncContext{account, epoch};
}

void
DiscoverMessages::AddLibraryWrite(BMessage& message, const DiscoverLibraryWriteRequest& request)
{
	message.AddString(MessageFields::Uri, request.uri.c_str());
	message.AddInt32(MessageFields::Generation, request.generation);
	message.AddInt32(MessageFields::WriteKind, static_cast<int32>(request.kind));
}

BMessage
DiscoverMessages::LibraryChanged(const DiscoverLibraryChange& change, const std::string& account)
{
	BMessage message(MSG_LIBRARY_CHANGED);
	message.AddString(MessageFields::AccountId, account.c_str());
	message.AddString(MessageFields::Operation, DiscoverOperationName(change.operation));
	message.AddString(MessageFields::Uri, change.uri.c_str());
	return message;
}

BMessage
DiscoverMessages::PlaylistChanged(const DiscoverPlaylistChange& change, const std::string& account)
{
	BMessage message(MSG_PLAYLISTS_CHANGED);
	message.AddString(MessageFields::AccountId, account.c_str());
	if (change.operation == DiscoverChangeOperation::Invalid)
		return message;
	message.AddString(MessageFields::Operation, DiscoverOperationName(change.operation));
	message.AddString(MessageFields::Uri, change.uri.c_str());
	message.AddString(MessageFields::Id, SpotifyItemIdForUri(change.uri).c_str());
	if (!change.name.empty())
		message.AddString(MessageFields::Name, change.name.c_str());
	if (!change.owner.empty())
		message.AddString(MessageFields::Owner, change.owner.c_str());
	message.AddBool(MessageFields::Writable, change.writable);
	if (change.owned)
		message.AddBool(MessageFields::Owned, *change.owned);
	return message;
}

std::optional<DiscoverLibraryWriteRequest>
DiscoverMessages::ReadLibraryWrite(const BMessage& message)
{
	DiscoverLibraryWriteRequest request;
	const char* uri;
	int32 kind, generation;
	if (message.FindString(MessageFields::Uri, &uri) != B_OK
			|| message.FindInt32(MessageFields::WriteKind, &kind) != B_OK
			|| message.FindInt32(MessageFields::Generation, &generation) != B_OK
			|| kind < 0 || kind > static_cast<int32>(DiscoverLibraryWriteKind::Remove))
		return std::nullopt;
	request.uri = uri;
	request.kind = static_cast<DiscoverLibraryWriteKind>(kind);
	request.generation = generation;
	return request;
}

std::optional<DiscoverLibraryWriteRequest>
DiscoverMessages::ReadLibraryCommand(const BMessage& message)
{
	DiscoverLibraryWriteRequest request;
	request.uri = message.GetString(MessageFields::Uri,
		message.GetString(MessageFields::TrackUri, ""));
	auto kind = SpotifyItemKindForUri(request.uri);
	switch (message.what) {
		case 'savA': case 'remA':
			if (kind != kSpotifyItemAlbum) return std::nullopt;
			break;
		case 'remI':
			if (kind != kSpotifyItemShow && kind != kSpotifyItemArtist
					&& kind != kSpotifyItemAudiobook) return std::nullopt;
			break;
		case 'likT': case 'remL':
			if (!SpotifyItemIsPlayable(kind)) return std::nullopt;
			break;
		default: return std::nullopt;
	}
	request.kind = message.what == 'savA' || message.what == 'likT'
		? DiscoverLibraryWriteKind::Save : DiscoverLibraryWriteKind::Remove;
	return request;
}

void
DiscoverMessages::AddAsyncToken(BMessage& message, const DiscoverAsyncToken& token)
{
	AddContext(message, token.context);
	message.AddInt64(MessageFields::RequestId, token.request);
}

std::optional<DiscoverAsyncToken>
DiscoverMessages::ReadAsyncToken(const BMessage& message)
{
	auto context = ReadContext(message);
	int64 request;
	if (!context || message.FindInt64(MessageFields::RequestId, &request) != B_OK)
		return std::nullopt;
	return DiscoverAsyncToken{*context, request};
}

namespace {
std::vector<std::string>
ReadStrings(const BMessage& message, const char* field)
{
	std::vector<std::string> result;
	const char* value;
	for (int32 index = 0; message.FindString(field, index, &value) == B_OK; index++)
		result.emplace_back(value);
	return result;
}
}

BMessage
DiscoverMessages::OperationResult(uint32 code, const DiscoverAsyncToken& token, bool ok, int32 status)
{
	BMessage message(code);
	AddAsyncToken(message, token);
	message.AddBool(MessageFields::Ok, ok);
	message.AddInt32(MessageFields::Status, status);
	return message;
}

bool
DiscoverMessages::ReadRowColumns(const BMessage& message, int32_t tab, DiscoverRowData& columns)
{
	int32 count = DiscoverTabColumnCount(tab);
	if (count == 0 || message.GetInt32(MessageFields::Columns, 0) != count)
		return false;
	columns = {ReadStrings(message, MessageFields::Values),
		ReadStrings(message, MessageFields::Uris), ReadStrings(message, MessageFields::Titles)};
	return columns.vals.size() % count == 0 && columns.uris.size() == columns.vals.size()
		&& columns.ttls.size() == columns.vals.size();
}

std::optional<DiscoverRowData>
DiscoverMessages::ReadResolvedLibraryRow(const BMessage& message, int32_t tab,
	const std::string& uri)
{
	DiscoverRowData row{ReadStrings(message, MessageFields::Values),
		ReadStrings(message, MessageFields::Uris), ReadStrings(message, MessageFields::Titles)};
	int32_t columns = DiscoverTabColumnCount(tab);
	if (columns == 0 || row.vals.size() != size_t(columns)
			|| row.uris.size() != row.vals.size() || row.ttls.size() != row.vals.size())
		return std::nullopt;
	if (!PrimaryUriMatchesTab(tab, uri) || row.uris[0] != uri)
		return std::nullopt;
	row.writable = message.GetBool(MessageFields::Writable, true);
	row.owned = message.GetBool(MessageFields::Owned, false);
	return row;
}

BMessage
DiscoverMessages::PageDone(const DiscoverPageResult& result)
{
	BMessage message(MSG_DISCOVER_PAGE_DONE);
	message.AddInt32(MessageFields::Tab, result.tab);
	message.AddInt32(MessageFields::LoadGeneration, result.loadGeneration);
	message.AddBool(MessageFields::HasMore, result.hasMore);
	if (result.nextOffset)
		message.AddInt32(MessageFields::NextOffset, *result.nextOffset);
	if (!result.nextCursor.empty())
		message.AddString(MessageFields::NextCursor, result.nextCursor.c_str());
	return message;
}

std::optional<DiscoverPageResult>
DiscoverMessages::ReadPageDone(const BMessage& message)
{
	DiscoverPageResult result;
	int32 tab;
	int32 generation;
	if (message.FindInt32(MessageFields::Tab, &tab) != B_OK
			|| tab < 0 || tab >= TAB_COUNT
			|| message.FindInt32(MessageFields::LoadGeneration, &generation) != B_OK
			|| message.FindBool(MessageFields::HasMore, &result.hasMore) != B_OK)
		return std::nullopt;
	result.tab = tab;
	result.loadGeneration = generation;
	int32 offset;
	if (message.FindInt32(MessageFields::NextOffset, &offset) == B_OK)
		result.nextOffset = offset;
	result.nextCursor = message.GetString(MessageFields::NextCursor, "");
	return result;
}

void
DiscoverMessages::AppendRows(BMessage& message, const std::vector<DiscoverRowData>& rows,
	size_t begin, size_t end)
{
	for (size_t index = begin; index < end; index++) {
		const auto& row = rows[index];
		for (const auto& value : row.vals)
			message.AddString(MessageFields::Values, value.c_str());
		for (const auto& uri : row.uris)
			message.AddString(MessageFields::Uris, uri.c_str());
		for (const auto& title : row.ttls)
			message.AddString(MessageFields::Titles, title.c_str());
		message.AddBool(MessageFields::Writable, row.writable);
		message.AddBool(MessageFields::Owned, row.owned);
	}
}

BMessage
DiscoverMessages::CacheBatch(const DiscoverCacheReadResult& result, size_t begin, size_t end)
{
	BMessage message(MSG_DISCOVER_CACHE_LOADED);
	message.AddInt32(MessageFields::Tab, result.request.tab);
	message.AddInt32(MessageFields::Columns, DiscoverTabColumnCount(result.request.tab));
	message.AddInt32(MessageFields::CacheGeneration, result.request.generation);
	message.AddString(MessageFields::AccountId, result.request.accountId.c_str());
	message.AddBool(MessageFields::FromCache, true);
	message.AddBool(MessageFields::CacheAvailable, result.status == DiscoverCacheReadStatus::Available);
	message.AddBool(MessageFields::CacheFirst, begin == 0);
	message.AddBool(MessageFields::CacheLast, end >= result.rows.size());
	message.AddInt32(MessageFields::Status, result.ioStatus);
	if (begin == 0 && result.audiobookIds) {
		message.AddBool(MessageFields::AudiobookIdsSnapshot, true);
		for (const auto& id : *result.audiobookIds)
			message.AddString(MessageFields::AudiobookId, id.c_str());
	}
	AppendRows(message, result.rows, begin, end);
	return message;
}

std::optional<DiscoverPlaylistChange>
DiscoverMessages::ReadPlaylistChange(const BMessage& message)
{
	DiscoverPlaylistChange change;
	change.operation = DiscoverOperation(message.GetString(MessageFields::Operation, ""));
	change.uri = message.GetString(MessageFields::Uri, "");
	if (change.uri.empty())
		change.uri = SpotifyUriForItemKind(kSpotifyItemPlaylist, message.GetString(MessageFields::Id, ""));
	if (change.operation == DiscoverChangeOperation::Invalid || change.uri.empty())
		return std::nullopt;
	change.name = message.GetString(MessageFields::Name, "");
	change.owner = message.GetString(MessageFields::Owner, "");
	change.writable = message.GetBool(MessageFields::Writable, true);
	bool owned;
	if (message.FindBool(MessageFields::Owned, &owned) == B_OK)
		change.owned = owned;
	return change;
}

std::vector<DiscoverRowData>
DiscoverMessages::ReadPlaylistSnapshot(const BMessage& message)
{
	std::vector<DiscoverRowData> rows;
	const char* uri = nullptr;
	for (int32 index = 0; message.FindString(MessageFields::Uri, index, &uri) == B_OK; index++) {
		const char* name = "Unknown";
		const char* owner = "Spotify";
		message.FindString(MessageFields::Name, index, &name);
		message.FindString(MessageFields::Owner, index, &owner);
		bool writable = true;
		bool owned = false;
		message.FindBool(MessageFields::Writable, index, &writable);
		message.FindBool(MessageFields::Owned, index, &owned);
		rows.push_back({{name, owner}, {uri ? uri : "", ""}, {name, ""}, writable, owned});
	}
	return rows;
}

BMessage
DiscoverMessages::PlaylistSnapshot(const std::vector<DiscoverRowData>& rows, int32_t generation)
{
	BMessage message(MSG_DISCOVER_PLAYLIST_SNAPSHOT);
	message.AddInt32(MessageFields::Generation, generation);
	for (const auto& row : rows) {
		message.AddString(MessageFields::Uri, row.uris[0].c_str());
		message.AddString(MessageFields::Name, row.vals[0].c_str());
		message.AddString(MessageFields::Owner, row.vals[1].c_str());
		message.AddBool(MessageFields::Owned, row.owned);
		message.AddBool(MessageFields::Writable, row.writable);
	}
	return message;
}
