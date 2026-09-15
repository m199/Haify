#include "discover/DiscoverCacheController.h"
#include "discover/DiscoverAsyncScope.h"
#include "discover/DiscoverLibraryWriteController.h"
#include "discover/DiscoverCacheWriteOrder.h"
#include "discover/DiscoverLibraryChangeController.h"
#include "discover/DiscoverPlaylistMutationController.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Discover state tests require assertions; compile without NDEBUG.
#endif

static void
TestCacheExpiryAndReset()
{
	DiscoverCacheController cache;
	cache.Reset("account-a");
	cache.PrepareLoad(TAB_SAVED_EPISODES, false, 100);
	assert(!cache.Expired(TAB_SAVED_EPISODES, 100 + DiscoverCacheController::Expiry));
	assert(cache.Expired(TAB_SAVED_EPISODES, 101 + DiscoverCacheController::Expiry));
	cache.CompleteRows(TAB_SAVED_EPISODES, true, false);
	assert(cache.Expired(TAB_SAVED_EPISODES, 100));
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		auto request = cache.BeginRead(tab);
		int32_t load = cache.State(tab).loadGeneration;
		cache.Reset("account-b");
		assert(!cache.AcceptCacheBatch(request, true, true, tab));
		assert(!cache.AcceptFreshRows(tab, load));
		assert(cache.CanRead(tab) && !cache.State(tab).loaded);
		assert(cache.State(tab).pageOffset == 0 && cache.State(tab).pageCursor.empty());
	}
}

static void
TestCacheAndFreshRaces()
{
	DiscoverCacheController cache;
	cache.Reset("account");
	auto request = cache.BeginRead(TAB_PODCASTS);
	assert(!cache.CanRead(TAB_PODCASTS));
	assert(cache.AcceptCacheBatch(request, true, false, TAB_PODCASTS));
	assert(cache.State(TAB_PODCASTS).cachePending);
	cache.CompleteRows(TAB_PODCASTS, false, true);
	assert(!cache.AcceptCacheBatch(request, true, true, TAB_PODCASTS));
	assert(!cache.State(TAB_PODCASTS).cachePending && !cache.State(TAB_PODCASTS).cacheBacked);
	assert(!cache.CanRead(TAB_PODCASTS));
	cache.Reset("account");
	auto newRequest = cache.BeginRead(TAB_PODCASTS);
	assert(!cache.AcceptCacheBatch(request, true, true, TAB_PODCASTS));
	assert(cache.State(TAB_PODCASTS).cachePending);
	assert(!cache.AcceptCacheBatch(newRequest, false, true, TAB_PODCASTS));
	assert(cache.CanRead(TAB_PODCASTS));
	newRequest = cache.BeginRead(TAB_PODCASTS);
	assert(!cache.AcceptCacheBatch(newRequest, true, true, TAB_SAVED_ALBUMS));
	assert(!cache.State(TAB_PODCASTS).cachePending);
	assert(!cache.AcceptCacheBatch(newRequest, true, true, TAB_PODCASTS));
	newRequest = cache.BeginRead(TAB_PODCASTS);
	assert(cache.AcceptCacheBatch(newRequest, true, true, TAB_PODCASTS));
	assert(!cache.AcceptCacheBatch(newRequest, true, true, TAB_PODCASTS));
	assert(!cache.AcceptFreshRows(TAB_PODCASTS, std::nullopt));
}

static void
TestPaging()
{
	DiscoverCacheController cache;
	cache.Reset("account");
	cache.PrepareLoad(TAB_SAVED_EPISODES, false, 100);
	int32_t generation = cache.State(TAB_SAVED_EPISODES).loadGeneration;
	assert(cache.CanLoadNext(TAB_SAVED_EPISODES));
	cache.BeginPage(TAB_SAVED_EPISODES);
	assert(!cache.CanLoadNext(TAB_SAVED_EPISODES));
	auto page = AdvanceDiscoverPage(0, 50, 75);
	assert(page.nextOffset == 50 && page.hasMore);
	assert(!cache.FinishPage(TAB_SAVED_EPISODES, generation - 1, false, 0, ""));
	assert(cache.State(TAB_SAVED_EPISODES).pageLoading);
	assert(cache.FinishPage(TAB_SAVED_EPISODES, generation, page.hasMore, page.nextOffset, ""));
	assert(cache.State(TAB_SAVED_EPISODES).pageOffset == 50);
	cache.PrepareLoad(TAB_SAVED_EPISODES, true, 200);
	assert(cache.State(TAB_SAVED_EPISODES).pageOffset == 50);
	page = AdvanceDiscoverPage(50, 25, 75);
	assert(page.nextOffset == 75 && !page.hasMore);
	assert(!AdvanceDiscoverPage(50, 0, 75).hasMore);
	assert(!cache.CanLoadNext(TAB_TOP_TRACKS));
}

static void
TestWriteOrdering()
{
	DiscoverCacheWriteOrder writes;
	auto first = writes.Begin("account-a");
	auto second = writes.Begin("account-a");
	auto other = writes.Begin("account-b");
	assert(!writes.IsCurrent("account-a", first));
	writes.Complete("account-a", first);
	assert(writes.IsCurrent("account-a", second));
	assert(writes.IsCurrent("account-b", other));
	writes.Complete("account-a", second);
	assert(!writes.IsCurrent("account-a", first) && !writes.IsCurrent("account-a", second));
	assert(writes.IsCurrent("account-b", other));
}

static void
TestLibraryRequestLifetimes()
{
	DiscoverLibraryChangeController library;
	const std::string uri = "spotify:album:one";
	auto first = library.ApplyChange({DiscoverChangeOperation::Add, uri}, true, false, false);
	assert(first.resolveAddition && library.AcceptsAddition(first.request));
	assert(!library.ApplyChange({DiscoverChangeOperation::Add, uri}, false, false, false).resolveAddition);
	assert(!library.AcceptsAddition(first.request));
	auto second = library.ApplyChange({DiscoverChangeOperation::Add, uri}, true, false, false);
	assert(!library.ApplyChange({DiscoverChangeOperation::Add, uri}, true, true, false).resolveAddition);
	assert(!library.AcceptsAddition(second.request));
	second = library.ApplyChange({DiscoverChangeOperation::Add, uri}, true, false, false);
	library.Reset();
	auto afterReset = library.ApplyChange({DiscoverChangeOperation::Add, uri}, true, false, false);
	assert(!library.AcceptsAddition(second.request));
	assert(library.AcceptsAddition(afterReset.request));
	library.CompleteAddition(afterReset.request);
	assert(!library.AcceptsAddition(afterReset.request));
	assert(library.ApplyChange({DiscoverChangeOperation::Remove, uri}, true, true, false).removeRow);
	assert(!library.ApplyChange({DiscoverChangeOperation::Invalid, uri}, true, false, false).resolveAddition);
}

static void
TestMembershipAndAudiobooks()
{
	DiscoverLibraryChangeController library;
	const std::string uri = "spotify:track:one";
	auto first = library.BeginMembership(uri);
	library.ObserveMembership({DiscoverChangeOperation::Add, uri});
	assert(!library.AcceptMembership(first, true, false));
	assert(library.KnownMembership(uri).value());
	first = library.BeginMembership(uri);
	library.Reset();
	auto second = library.BeginMembership(uri);
	assert(!library.AcceptMembership(first, true, false));
	assert(!library.AcceptMembership(second, false, false));
	assert(!library.KnownMembership(uri));
	assert(!library.AcceptMembership(second, true, false));
	second = library.BeginMembership(uri);
	assert(library.AcceptMembership(second, true, false));
	assert(!library.KnownMembership(uri).value());
	auto added = library.ApplyChange({DiscoverChangeOperation::Add, "spotify:audiobook:book"},
		true, false, true);
	assert(added.removePodcastDuplicates && added.saveCache && library.AudiobookIdsKnown());
	assert(!library.AcceptPrimaryRow(TAB_PODCASTS, "spotify:show:book"));
	auto removed = library.ApplyChange({DiscoverChangeOperation::Remove, "spotify:audiobook:book"},
		true, true, true);
	assert(removed.refreshPodcasts && library.AcceptPrimaryRow(TAB_PODCASTS, "spotify:show:book"));
	assert(DiscoverLibraryChangeController::TargetTab("spotify:track:one") == TAB_NONE);
	assert(DiscoverLibraryChangeController::TargetTab("spotify:album:") == TAB_NONE);
}

static DiscoverRowData
Playlist(const std::string& id, const std::string& name)
{
	return {{name, "Owner"}, {"spotify:playlist:" + id, ""}, {name, ""}, false, true};
}

static void
TestPlaylistDeltas()
{
	DiscoverPlaylistMutationController controller;
	auto original = Playlist("one", "Old");
	auto renamed = controller.PlanChange({DiscoverChangeOperation::Rename,
		"spotify:playlist:one", "New"}, original);
	assert(renamed.action == DiscoverPlaylistRowAction::Upsert);
	assert(renamed.row.vals[0] == "New" && renamed.row.vals[1] == "Owner");
	assert(!renamed.row.writable && renamed.row.owned);
	assert(controller.PlanChange({DiscoverChangeOperation::Rename, "spotify:playlist:one", "New"},
		std::nullopt).action == DiscoverPlaylistRowAction::Reload);
	assert(controller.PlanChange({DiscoverChangeOperation::Add, "spotify:playlist:one", ""},
		std::nullopt).action == DiscoverPlaylistRowAction::Ignore);
	assert(controller.PlanChange({DiscoverChangeOperation::Remove, "spotify:collection"},
		std::nullopt).action == DiscoverPlaylistRowAction::Ignore);
	auto added = controller.PlanChange({DiscoverChangeOperation::Add, "spotify:playlist:two", "Two"},
		std::nullopt);
	assert(added.row.vals[1] == "Spotify" && added.row.writable && !added.row.owned);
}

static void
TestPlaylistSnapshotRaces()
{
	DiscoverPlaylistMutationController controller;
	DiscoverRowData liked{{"Liked Songs", "Spotify"}, {"spotify:collection", ""}, {"Liked Songs", ""}};
	auto current = Playlist("current", "Current");
	int32_t snapshot = controller.BeginSnapshot();
	auto removal = controller.QueueMutation(DiscoverChangeOperation::Remove, "current", "", current);
	assert(removal.dispatch && !removal.row && !controller.CanPersist());
	assert(!controller.AcceptsSnapshot(snapshot));
	assert(!controller.QueueMutation(DiscoverChangeOperation::Remove, "current", "", current).accepted);
	auto plan = controller.PlanSnapshot({liked, Playlist("old", "Old")}, {current, current});
	assert(plan.upserts.empty() && plan.removals == std::vector<std::string>{"spotify:playlist:old"});
	assert((plan.order == std::vector<std::string>{"spotify:collection", "spotify:playlist:current"}));
	auto done = controller.CompleteMutation("current", removal.dispatch->generation, false);
	assert(done.failed && done.row->vals[0] == "Current" && controller.CanPersist());
	assert(!controller.CompleteMutation("current", removal.dispatch->generation, true).accepted);
	auto rename = controller.QueueMutation(DiscoverChangeOperation::Rename, "current", "New", current);
	plan = controller.PlanSnapshot({liked, current}, {Playlist("current", "Server")});
	assert(plan.upserts.empty() && plan.removals.empty());
	controller.Reset();
	assert(controller.CanPersist());
	assert(!controller.CompleteMutation("current", rename.dispatch->generation, true).accepted);
	auto next = controller.QueueMutation(DiscoverChangeOperation::Remove, "current", "", current);
	assert(next.dispatch->generation != removal.dispatch->generation);
}

static void
TestRenameQueue(bool firstSucceeds, bool secondSucceeds)
{
	DiscoverPlaylistMutationController controller;
	auto first = controller.QueueMutation(DiscoverChangeOperation::Rename, "one", "A", Playlist("one", "Original"));
	auto second = controller.QueueMutation(DiscoverChangeOperation::Rename, "one", "B", first.row);
	assert(first.dispatch && !second.dispatch && second.row->vals[0] == "B");
	auto firstDone = controller.CompleteMutation("one", first.dispatch->generation, firstSucceeds);
	assert(firstDone.dispatch && firstDone.pending && firstDone.row->vals[0] == "B");
	assert(!controller.CompleteMutation("one", first.dispatch->generation, true).accepted);
	auto secondDone = controller.CompleteMutation("one", firstDone.dispatch->generation, secondSucceeds);
	std::string expected = secondSucceeds ? "B" : firstSucceeds ? "A" : "Original";
	assert(!secondDone.pending && secondDone.row->vals[0] == expected && controller.CanPersist());
}

static void
TestRenameThenRemoval()
{
	DiscoverPlaylistMutationController controller;
	auto rename = controller.QueueMutation(DiscoverChangeOperation::Rename, "one", "New", Playlist("one", "Old"));
	auto removal = controller.QueueMutation(DiscoverChangeOperation::Remove, "one", "", rename.row);
	assert(!removal.row && !removal.dispatch);
	auto done = controller.CompleteMutation("one", rename.dispatch->generation, true);
	assert(!done.row && done.dispatch);
	// The row is detached when the application's rename broadcast arrives.
	assert(controller.PlanChange({DiscoverChangeOperation::Rename, "spotify:playlist:one", "New"},
		std::nullopt).action == DiscoverPlaylistRowAction::Ignore);
	auto restored = controller.CompleteMutation("one", done.dispatch->generation, false);
	assert(restored.failed && restored.row->vals[0] == "New" && restored.row->owned);
	auto pending = controller.QueueMutation(DiscoverChangeOperation::Rename, "one", "Next", restored.row);
	assert(controller.PlanChange({DiscoverChangeOperation::Remove, "spotify:playlist:one"}, pending.row).action
		== DiscoverPlaylistRowAction::Remove);
	assert(!controller.CompleteMutation("one", pending.dispatch->generation, true).accepted);
}

static void
TestInvalidationDuringLoad()
{
	DiscoverCacheController cache;
	cache.Reset("account");
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		auto disk = cache.BeginRead(tab);
		cache.PrepareLoad(tab, false, 100);
		int32_t generation = cache.State(tab).loadGeneration;
		cache.BeginPage(tab);
		cache.CompleteRows(tab, true, false);
		assert(cache.State(tab).pageLoading);
		cache.Invalidate(tab);
		assert(!cache.AcceptFreshRows(tab, generation) && !cache.ShouldPersist(tab));
		assert(!cache.AcceptCacheBatch(disk, true, true, tab));
		assert(!cache.FinishPage(tab, generation, true, 50, "cursor"));
		assert(!cache.CanRead(tab) && cache.Expired(tab, 100));
		cache.PrepareLoad(tab, false, 200);
		assert(cache.AcceptFreshRows(tab, cache.State(tab).loadGeneration));
		cache.CompleteRows(tab, false, true);
		assert(cache.ShouldPersist(tab) && !cache.State(tab).invalidated);
	}
}

static void
TestAsyncContexts()
{
	DiscoverAsyncScope scope;
	scope.Reset("a", true);
	auto old = scope.Begin(TAB_AUDIOBOOKS);
	scope.Reset("a", false);
	assert(!scope.Accepts(old.context));
	assert(scope.Complete(old, true).disposition == DiscoverAsyncDisposition::Reconcile);
	assert(scope.Complete(old, true).disposition == DiscoverAsyncDisposition::Ignore);
	auto otherAccount = scope.Begin(TAB_PLAYLISTS);
	scope.Reset("b", false);
	assert(scope.Complete(otherAccount, true).disposition == DiscoverAsyncDisposition::Ignore);
	auto current = scope.Begin(TAB_SAVED_ALBUMS);
	auto forged = current;
	forged.context.accountId = "wrong";
	assert(scope.Complete(forged, true).disposition == DiscoverAsyncDisposition::Ignore);
	assert(scope.Complete(current, false).disposition == DiscoverAsyncDisposition::Current);
	assert(scope.Complete(current, false).disposition == DiscoverAsyncDisposition::Ignore);
	auto failure = scope.Begin(TAB_PODCASTS);
	scope.Reset("b", false);
	assert(scope.Complete(failure, false).disposition == DiscoverAsyncDisposition::Ignore);
}

static void
TestLibraryWriteQueue()
{
	DiscoverLibraryWriteController controller;
	const std::string uri = "spotify:album:one";
	auto check = controller.Queue(uri, DiscoverLibraryWriteKind::EnsureSaved, true, true);
	assert(check.dispatch && check.accepted);
	auto remove = controller.Queue(uri, DiscoverLibraryWriteKind::Remove, false, true);
	assert(remove.accepted && !remove.dispatch);
	auto save = controller.Complete(*check.dispatch, true, false);
	assert(save.dispatch && !save.confirmed && save.dispatch->kind == DiscoverLibraryWriteKind::Save);
	assert(!controller.Complete(*check.dispatch, true, false).accepted);
	auto saved = controller.Complete(*save.dispatch, true, false);
	assert(saved.selectTarget && saved.confirmed->operation == DiscoverChangeOperation::Add);
	assert(saved.dispatch && saved.dispatch->kind == DiscoverLibraryWriteKind::Remove);
	auto failed = controller.Complete(*saved.dispatch, false, false);
	assert(failed.failed && !failed.confirmed && !failed.dispatch);
	auto alreadySaved = controller.Queue(uri, DiscoverLibraryWriteKind::EnsureSaved, true, true);
	auto known = controller.Complete(*alreadySaved.dispatch, true, true);
	assert(known.confirmed && known.selectTarget && !known.dispatch);
	auto pending = controller.Queue(uri, DiscoverLibraryWriteKind::Save, false, true);
	controller.Reset();
	auto next = controller.Queue(uri, DiscoverLibraryWriteKind::Save, false, true);
	assert(!controller.Complete(*pending.dispatch, true, false).accepted);
	assert(next.dispatch->generation != pending.dispatch->generation);
	assert(!controller.Queue("spotify:audiobook:one", DiscoverLibraryWriteKind::Save, false, false).accepted);
	assert(!controller.Queue("spotify:album:", DiscoverLibraryWriteKind::Save, false, true).accepted);
}

static void
TestMalformedPlaylistSnapshotRows()
{
	DiscoverPlaylistMutationController controller;
	DiscoverRowData malformed{{}, {"spotify:playlist:broken"}, {}};
	auto valid = Playlist("valid", "Valid");
	auto plan = controller.PlanSnapshot({}, {malformed, valid});
	assert(plan.upserts.size() == 1 && plan.upserts[0].uris[0] == valid.uris[0]);
}

int
main()
{
	TestCacheExpiryAndReset();
	TestCacheAndFreshRaces();
	TestPaging();
	TestWriteOrdering();
	TestLibraryRequestLifetimes();
	TestMembershipAndAudiobooks();
	TestPlaylistDeltas();
	TestPlaylistSnapshotRaces();
	TestMalformedPlaylistSnapshotRows();
	TestRenameQueue(false, false);
	TestRenameQueue(false, true);
	TestRenameQueue(true, false);
	TestRenameQueue(true, true);
	TestRenameThenRemoval();
	TestInvalidationDuringLoad();
	TestAsyncContexts();
	TestLibraryWriteQueue();
	std::puts("Discover state tests passed.");
}
