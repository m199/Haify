#include "discover/DiscoverTabPolicy.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Discover tab policy tests require assertions; compile without NDEBUG.
#endif

static const DiscoverTabOrder kDefaultOrder = {
	TAB_PLAYLISTS, TAB_TOP_TRACKS, TAB_TOP_ARTISTS, TAB_NEW_RELEASES,
	TAB_SAVED_ALBUMS, TAB_PODCASTS, TAB_FOLLOWED_ARTISTS,
	TAB_SAVED_EPISODES, TAB_AUDIOBOOKS
};

static void
TestStableOrder()
{
	const std::vector<std::string> ids = {
		"playlists", "top_tracks", "top_artists", "new_releases", "saved_albums",
		"podcasts", "followed_artists", "saved_episodes", "audiobooks"
	};
	assert(NormalizeDiscoverTabOrder({}) == kDefaultOrder);
	assert(DiscoverTabOrderIds(kDefaultOrder) == ids);
	for (int32_t i = 0; i < TAB_COUNT; i++) {
		assert(kDefaultOrder[i] == i);
		assert(DiscoverTabForId(ids[i]) == i);
	}
	assert(DiscoverTabForId("") == TAB_NONE);
	assert(DiscoverTabForId("unknown") == TAB_NONE);
	assert(std::string(DiscoverTabId(-1)).empty());
	assert(std::string(DiscoverTabId(TAB_COUNT)).empty());
	DiscoverTabOrder expected = {TAB_PODCASTS, TAB_PLAYLISTS, TAB_TOP_TRACKS,
		TAB_TOP_ARTISTS, TAB_NEW_RELEASES, TAB_SAVED_ALBUMS,
		TAB_FOLLOWED_ARTISTS, TAB_SAVED_EPISODES, TAB_AUDIOBOOKS};
	assert(NormalizeDiscoverTabOrder({"podcasts", "unknown", "podcasts", "playlists"})
		== expected);
	assert(NormalizeDiscoverTabOrder(DiscoverTabOrderIds(expected)) == expected);
}

static void
CheckVisibilityMask(unsigned mask, bool audiobooksEnabled)
{
	DiscoverTabVisibility visible{};
	unsigned effective = audiobooksEnabled ? mask : mask & ~(1u << TAB_AUDIOBOOKS);
	int32_t count = 0;
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		visible[tab] = (mask & (1u << tab)) != 0;
		count += (effective >> tab) & 1u;
	}
	assert(VisibleDiscoverTabCount(visible, audiobooksEnabled) == count);
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		assert(IsDiscoverTabVisible(tab, visible, audiobooksEnabled)
			== ((effective & (1u << tab)) != 0));
		auto toggled = visible;
		bool permitted = (tab != TAB_AUDIOBOOKS || audiobooksEnabled)
			&& (!visible[tab] || count > 1);
		assert(ToggleDiscoverTabVisibility(toggled, tab, audiobooksEnabled) == permitted);
		auto expected = visible;
		if (permitted)
			expected[tab] = !expected[tab];
		assert(toggled == expected);
	}
	auto repaired = visible;
	EnsureVisibleDiscoverTab(repaired, audiobooksEnabled);
	if (count == 0)
		visible[TAB_PLAYLISTS] = true;
	assert(repaired == visible);
}

static void
TestVisibility()
{
	for (unsigned mask = 0; mask < (1u << TAB_COUNT); mask++) {
		CheckVisibilityMask(mask, false);
		CheckVisibilityMask(mask, true);
	}
	DiscoverTabVisibility visible{};
	assert(!ToggleDiscoverTabVisibility(visible, -1, true));
	assert(!ToggleDiscoverTabVisibility(visible, TAB_COUNT, true));
	assert(!IsDiscoverTabVisible(-1, visible, true));
	assert(!IsDiscoverTabVisible(TAB_COUNT, visible, true));
}

static void
TestLayoutAndSelection()
{
	DiscoverTabVisibility visible{};
	visible[TAB_PLAYLISTS] = visible[TAB_SAVED_ALBUMS] = visible[TAB_AUDIOBOOKS] = true;
	auto order = NormalizeDiscoverTabOrder({"audiobooks", "saved_albums"});
	auto layout = MakeDiscoverTabLayout(order, visible, false);
	assert(layout.tabs == DiscoverTabOrder({TAB_SAVED_ALBUMS, TAB_PLAYLISTS}));
	assert(layout.LogicalTab(0) == TAB_SAVED_ALBUMS);
	assert(layout.LogicalTab(1) == TAB_PLAYLISTS);
	assert(layout.LogicalTab(-1) == TAB_NONE && layout.LogicalTab(2) == TAB_NONE);
	assert(layout.VisualTab(TAB_PLAYLISTS) == 1);
	assert(layout.VisualTab(TAB_AUDIOBOOKS) == -1);
	assert(layout.RestoreSelection(TAB_PLAYLISTS) == 1);
	assert(layout.RestoreSelection(TAB_TOP_TRACKS) == 0);
	layout = MakeDiscoverTabLayout(order, visible, true);
	assert(layout.tabs == DiscoverTabOrder({TAB_AUDIOBOOKS, TAB_SAVED_ALBUMS, TAB_PLAYLISTS}));
	assert(layout.RestoreSelection(TAB_PLAYLISTS) == 2);
	// Capability changes hide the tab without destroying its saved position or preference.
	assert(visible[TAB_AUDIOBOOKS] && order.front() == TAB_AUDIOBOOKS);
	visible.fill(false);
	visible[TAB_AUDIOBOOKS] = true;
	layout = MakeDiscoverTabLayout(order, visible, false);
	assert(layout.tabs.empty() && layout.RestoreSelection(TAB_AUDIOBOOKS) == -1);
}

static void
TestMovesWithHiddenTabs()
{
	auto order = kDefaultOrder;
	DiscoverTabVisibility visible{};
	visible[TAB_PLAYLISTS] = visible[TAB_TOP_ARTISTS] = visible[TAB_AUDIOBOOKS] = true;
	auto layout = MakeDiscoverTabLayout(order, visible, true);
	assert(MoveDiscoverTab(order, layout.LogicalTab(0), layout.LogicalTab(1)));
	assert(order == DiscoverTabOrder({TAB_TOP_TRACKS, TAB_TOP_ARTISTS, TAB_PLAYLISTS,
		TAB_NEW_RELEASES, TAB_SAVED_ALBUMS, TAB_PODCASTS, TAB_FOLLOWED_ARTISTS,
		TAB_SAVED_EPISODES, TAB_AUDIOBOOKS}));
	layout = MakeDiscoverTabLayout(order, visible, true);
	assert(MoveDiscoverTab(order, layout.LogicalTab(2), layout.LogicalTab(0)));
	assert(order == DiscoverTabOrder({TAB_TOP_TRACKS, TAB_AUDIOBOOKS, TAB_TOP_ARTISTS,
		TAB_PLAYLISTS, TAB_NEW_RELEASES, TAB_SAVED_ALBUMS, TAB_PODCASTS,
		TAB_FOLLOWED_ARTISTS, TAB_SAVED_EPISODES}));
	auto unchanged = order;
	assert(!MoveDiscoverTab(order, TAB_NONE, TAB_PLAYLISTS));
	assert(!MoveDiscoverTab(order, TAB_PLAYLISTS, TAB_NONE));
	assert(!MoveDiscoverTab(order, TAB_PLAYLISTS, TAB_PLAYLISTS));
	assert(order == unchanged);
}

static void
TestCapabilityLossKeepsVisibleTab()
{
	DiscoverTabVisibility visible{};
	visible[TAB_AUDIOBOOKS] = true;
	auto order = NormalizeDiscoverTabOrder({"audiobooks"});
	auto layout = MakeDiscoverTabLayout(order, visible, true);
	assert(layout.tabs == DiscoverTabOrder({TAB_AUDIOBOOKS}));
	// Same repair used at startup and when Spotify capabilities change.
	EnsureVisibleDiscoverTab(visible, false);
	layout = MakeDiscoverTabLayout(order, visible, false);
	assert(layout.tabs == DiscoverTabOrder({TAB_PLAYLISTS}));
	assert(layout.RestoreSelection(TAB_AUDIOBOOKS) == 0);
	assert(visible[TAB_AUDIOBOOKS] && order.front() == TAB_AUDIOBOOKS);
	layout = MakeDiscoverTabLayout(order, visible, true);
	assert(layout.tabs == DiscoverTabOrder({TAB_AUDIOBOOKS, TAB_PLAYLISTS}));
}

struct DropCase {
	const char* uri = "";
	SpotifyItemKind kind = kSpotifyItemUnknown;
	DiscoverTab target = TAB_NONE;
};

static const DropCase kDropCases[] = {
	{"spotify:track:one", kSpotifyItemTrack, TAB_PLAYLISTS},
	{"spotify:episode:one", kSpotifyItemEpisode, TAB_SAVED_EPISODES},
	{"spotify:album:one", kSpotifyItemAlbum, TAB_SAVED_ALBUMS},
	{"spotify:show:one", kSpotifyItemShow, TAB_PODCASTS},
	{"spotify:artist:one", kSpotifyItemArtist, TAB_FOLLOWED_ARTISTS},
	{"spotify:audiobook:one", kSpotifyItemAudiobook, TAB_AUDIOBOOKS},
	{"spotify:playlist:one", kSpotifyItemPlaylist, TAB_PLAYLISTS}
};

static void
CheckDropCase(const DropCase& sample)
{
	using namespace MessageContracts;
	DragItem item{sample.uri, sample.kind, ""};
	DiscoverTabVisibility visible;
	visible.fill(true);
	assert(DiscoverDropTargetTab(item, true) == sample.target);
	auto hover = ResolveDiscoverDragHover(item, visible, true, TAB_NONE, sample.target);
	assert(hover.target == sample.target && hover.scheduleTabSwitch && !hover.showRowMarker);
	hover = ResolveDiscoverDragHover(item, visible, true, sample.target, TAB_NONE);
	assert(hover.target == sample.target && !hover.scheduleTabSwitch && hover.showRowMarker);
	visible[sample.target] = false;
	hover = ResolveDiscoverDragHover(item, visible, true, TAB_NONE, sample.target);
	assert(hover.target == TAB_NONE && !hover.scheduleTabSwitch && !hover.showRowMarker);
	item.intent = DropIntent::Add;
	assert(DiscoverDropTargetTab(item, true) == sample.target);
	item.intent = DropIntent::Reorder;
	assert(DiscoverDropTargetTab(item, true) == TAB_NONE);
}

static void
TestDropTargets()
{
	for (const auto& sample : kDropCases)
		CheckDropCase(sample);
	assert(DiscoverDropTargetTab({"spotify:audiobook:one", kSpotifyItemAudiobook, ""}, false)
		== TAB_NONE);
	assert(DiscoverDropTargetTab({"spotify:track:", kSpotifyItemTrack, ""}, true) == TAB_NONE);
	assert(DiscoverDropTargetTab({"unknown", kSpotifyItemUnknown, ""}, true) == TAB_NONE);
	assert(DiscoverLibraryTargetTab(kSpotifyItemEpisode, false) == TAB_SAVED_EPISODES);
}

static void
CheckHoverPositions(const DropCase& sample, bool audiobooksEnabled)
{
	MessageContracts::DragItem item{sample.uri, sample.kind, ""};
	DiscoverTabVisibility visible;
	visible.fill(true);
	bool available = sample.kind != kSpotifyItemAudiobook || audiobooksEnabled;
	for (int32_t selected = TAB_NONE; selected < TAB_COUNT; selected++) {
		for (int32_t hovered = TAB_NONE; hovered < TAB_COUNT; hovered++) {
			auto hover = ResolveDiscoverDragHover(item, visible, audiobooksEnabled,
				selected, hovered);
			assert(hover.target == (available ? sample.target : TAB_NONE));
			assert(hover.scheduleTabSwitch == (available && hovered == sample.target));
			assert(hover.showRowMarker == (available && selected == sample.target));
		}
	}
}

static void
TestDragHoverMatrix()
{
	for (const auto& sample : kDropCases) {
		CheckHoverPositions(sample, false);
		CheckHoverPositions(sample, true);
	}
}

static void
TestPrimaryRows()
{
	const SpotifyItemKind expected[TAB_COUNT] = {kSpotifyItemPlaylist,
		kSpotifyItemTrack, kSpotifyItemArtist, kSpotifyItemAlbum, kSpotifyItemAlbum,
		kSpotifyItemShow, kSpotifyItemArtist, kSpotifyItemEpisode, kSpotifyItemAudiobook};
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		for (const auto& sample : kDropCases)
			assert(PrimaryUriMatchesTab(tab, sample.uri) == (sample.kind == expected[tab]));
		assert(PrimaryUriMatchesTab(tab, "spotify:collection") == (tab == TAB_PLAYLISTS));
		assert(!PrimaryUriMatchesTab(tab, "spotify:track:"));
		assert(!PrimaryUriMatchesTab(tab, ""));
	}
	assert(!PrimaryUriMatchesTab(-1, "spotify:track:one"));
	assert(!PrimaryUriMatchesTab(TAB_COUNT, "spotify:track:one"));
}

int
main()
{
	TestStableOrder();
	TestVisibility();
	TestLayoutAndSelection();
	TestMovesWithHiddenTabs();
	TestCapabilityLossKeepsVisibleTab();
	TestDropTargets();
	TestDragHoverMatrix();
	TestPrimaryRows();
	std::puts("Discover tab policy tests passed (all 512 visibility masks, both capability states).");
	return 0;
}
