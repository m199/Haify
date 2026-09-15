#include "discover/DiscoverMessages.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Discover message tests require assertions; compile without NDEBUG.
#endif

static void
TestOffsetPageDelivery(int32_t tab)
{
	DiscoverCacheController cache;
	cache.Reset("account");
	cache.PrepareLoad(tab, false, 100);
	int32_t generation = cache.State(tab).loadGeneration;
	// The response contains 50 source items even when only one row is displayable.
	cache.BeginPage(tab);
	auto page = AdvanceDiscoverPage(0, 50, 75);
	auto message = DiscoverMessages::PageDone({tab, generation, page.hasMore, page.nextOffset, ""});
	auto result = DiscoverMessages::ReadPageDone(message);
	assert(result && result->nextOffset && *result->nextOffset == 50);
	assert(cache.FinishPage(result->tab, result->loadGeneration, result->hasMore,
		*result->nextOffset, result->nextCursor));
	assert(cache.State(tab).pageOffset == 50 && cache.CanLoadNext(tab));
	cache.BeginPage(tab);
	page = AdvanceDiscoverPage(cache.State(tab).pageOffset, 25, 75);
	message = DiscoverMessages::PageDone({tab, generation, page.hasMore, page.nextOffset, ""});
	result = DiscoverMessages::ReadPageDone(message);
	assert(result && result->nextOffset);
	assert(cache.FinishPage(result->tab, result->loadGeneration, result->hasMore,
		*result->nextOffset, result->nextCursor));
	assert(cache.State(tab).pageOffset == 75 && !cache.CanLoadNext(tab));
	cache.Reset("another-account");
	assert(!cache.FinishPage(result->tab, result->loadGeneration, result->hasMore,
		*result->nextOffset, result->nextCursor));
}

static void
TestCursorAndFailureDelivery()
{
	auto cursor = DiscoverMessages::ReadPageDone(DiscoverMessages::PageDone(
		{TAB_FOLLOWED_ARTISTS, 5, true, std::nullopt, "next-artist"}));
	assert(cursor && !cursor->nextOffset && cursor->nextCursor == "next-artist");
	auto failure = DiscoverMessages::ReadPageDone(DiscoverMessages::PageDone(
		{TAB_SAVED_EPISODES, 5, false, std::nullopt, ""}));
	assert(failure && !failure->hasMore && !failure->nextOffset);
	BMessage missing(MSG_DISCOVER_PAGE_DONE);
	assert(!DiscoverMessages::ReadPageDone(missing));
	missing = DiscoverMessages::PageDone({TAB_COUNT, 5, false, std::nullopt, ""});
	assert(!DiscoverMessages::ReadPageDone(missing));
}

static void
TestUnavailableAndEmptyCacheDelivery()
{
	DiscoverCacheReadResult unavailable;
	unavailable.request = {"account", TAB_SAVED_ALBUMS, 7};
	unavailable.ioStatus = B_ENTRY_NOT_FOUND;
	BMessage message = DiscoverMessages::CacheBatch(unavailable, 0, 0);
	assert(!message.GetBool(MessageFields::CacheAvailable, true));
	assert(message.GetInt32(MessageFields::Status, B_OK) == B_ENTRY_NOT_FOUND);
	assert(message.GetBool(MessageFields::CacheLast, false));
	unavailable.status = DiscoverCacheReadStatus::Available;
	unavailable.ioStatus = B_OK;
	message = DiscoverMessages::CacheBatch(unavailable, 0, 0);
	assert(message.GetBool(MessageFields::CacheAvailable, false));
	assert(message.GetBool(MessageFields::CacheFirst, false));
	assert(message.GetBool(MessageFields::CacheLast, false));
	assert(message.GetInt32(MessageFields::CacheGeneration, -1) == 7);
	assert(std::string(message.GetString(MessageFields::AccountId, "")) == "account");
}

static void
TestResolvedRowIdentity()
{
	const std::string uri = "spotify:album:one";
	DiscoverRowData original{{"Album", "Artist"}, {uri, "spotify:artist:one"},
		{"Album", "Artist"}, false, true};
	BMessage message(MSG_DISCOVER_LIBRARY_RESOLVED);
	DiscoverMessages::AppendRows(message, {original}, 0, 1);
	auto row = DiscoverMessages::ReadResolvedLibraryRow(message, TAB_SAVED_ALBUMS, uri);
	assert(row && row->vals == original.vals && row->uris == original.uris);
	assert(!row->writable && row->owned);
	assert(!DiscoverMessages::ReadResolvedLibraryRow(message, TAB_SAVED_ALBUMS, "spotify:album:other"));
	assert(!DiscoverMessages::ReadResolvedLibraryRow(message, TAB_SAVED_EPISODES, uri));
	message.RemoveName(MessageFields::Titles);
	assert(!DiscoverMessages::ReadResolvedLibraryRow(message, TAB_SAVED_ALBUMS, uri));
}

static void
TestPlaylistSnapshotColumns()
{
	DiscoverRowData original{{"Playlist", "Owner"}, {"spotify:playlist:one", ""},
		{"Playlist", ""}, false, true};
	auto message = DiscoverMessages::PlaylistSnapshot({original}, 12);
	assert(message.what == MSG_DISCOVER_PLAYLIST_SNAPSHOT);
	assert(message.GetInt32(MessageFields::Generation, -1) == 12);
	auto rows = DiscoverMessages::ReadPlaylistSnapshot(message);
	assert(rows.size() == 1 && rows[0].vals == original.vals && rows[0].uris == original.uris);
	assert(rows[0].ttls == original.ttls && rows[0].owned && !rows[0].writable);
}

static void
TestMutationContextDelivery()
{
	DiscoverAsyncScope scope;
	scope.Reset("account", true);
	auto token = scope.Begin(TAB_SAVED_ALBUMS);
	auto message = DiscoverMessages::OperationResult(MSG_DISCOVER_LIBRARY_WRITE_RESULT, token, false, 429);
	DiscoverLibraryWriteRequest request{"spotify:album:one", DiscoverLibraryWriteKind::Remove, 12};
	DiscoverMessages::AddLibraryWrite(message, request);
	auto delivered = DiscoverMessages::ReadAsyncToken(message);
	auto command = DiscoverMessages::ReadLibraryWrite(message);
	assert(delivered && scope.Accepts(delivered->context) && delivered->request == token.request);
	assert(command && command->uri == request.uri && command->generation == 12);
	assert(command->kind == DiscoverLibraryWriteKind::Remove);
	assert(!message.GetBool(MessageFields::Ok, true) && message.GetInt32(MessageFields::Status, 0) == 429);
	message.RemoveName(MessageFields::WriteKind);
	assert(!DiscoverMessages::ReadLibraryWrite(message));
	BMessage menu('savA');
	BMessage context;
	DiscoverMessages::AddContext(context, token.context);
	menu.AddMessage(MessageFields::CommandContext, &context);
	menu.AddString(MessageFields::Uri, request.uri.c_str());
	assert(DiscoverMessages::ReadLibraryCommand(menu)->kind == DiscoverLibraryWriteKind::Save);
	scope.Reset("other", true);
	assert(!scope.Accepts(*DiscoverMessages::ReadContext(menu)));
}

static void
TestMalformedRowColumns()
{
	DiscoverRowData columns;
	BMessage rows(MSG_DISCOVER_ROWS);
	rows.AddInt32(MessageFields::Columns, 2);
	assert(DiscoverMessages::ReadRowColumns(rows, TAB_SAVED_ALBUMS, columns));
	rows.AddString(MessageFields::Values, "Album");
	assert(!DiscoverMessages::ReadRowColumns(rows, TAB_SAVED_ALBUMS, columns));
	rows.AddString(MessageFields::Values, "Artist");
	assert(!DiscoverMessages::ReadRowColumns(rows, TAB_SAVED_ALBUMS, columns));
	rows.AddString(MessageFields::Uris, "spotify:album:one");
	rows.AddString(MessageFields::Uris, "");
	rows.AddString(MessageFields::Titles, "Album");
	rows.AddString(MessageFields::Titles, "Artist");
	assert(DiscoverMessages::ReadRowColumns(rows, TAB_SAVED_ALBUMS, columns));
	assert(!DiscoverMessages::ReadRowColumns(rows, TAB_SAVED_EPISODES, columns));
}

int
main()
{
	TestOffsetPageDelivery(TAB_SAVED_EPISODES);
	TestOffsetPageDelivery(TAB_AUDIOBOOKS);
	TestCursorAndFailureDelivery();
	TestUnavailableAndEmptyCacheDelivery();
	TestResolvedRowIdentity();
	TestPlaylistSnapshotColumns();
	TestMutationContextDelivery();
	TestMalformedRowColumns();
	std::puts("Discover message tests passed.");
}
