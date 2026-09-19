#pragma once

#include "messages/DragItem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Numeric values and stable IDs are shared by messages, settings and caches.
// Keep their order compatible with existing data; labels belong to the UI.
enum DiscoverTab : int32_t {
	TAB_NONE = -1,
	TAB_PLAYLISTS = 0,
	TAB_TOP_TRACKS,
	TAB_TOP_ARTISTS,
	TAB_NEW_RELEASES,
	TAB_SAVED_ALBUMS,
	TAB_PODCASTS,
	TAB_FOLLOWED_ARTISTS,
	TAB_SAVED_EPISODES,
	TAB_AUDIOBOOKS,
	TAB_COUNT
};

inline constexpr int32_t kDiscoverTabCount = TAB_COUNT;
using DiscoverTabOrder = std::vector<DiscoverTab>;
using DiscoverTabVisibility = std::array<bool, TAB_COUNT>;

inline std::vector<int32_t>
DiscoverAffectedTabs(int32_t tab)
{
	if (tab < 0 || tab >= TAB_COUNT)
		return {};
	if (tab == TAB_AUDIOBOOKS)
		return {TAB_AUDIOBOOKS, TAB_PODCASTS};
	return {tab};
}

inline int32_t
DiscoverTabColumnCount(int32_t tab)
{
	return tab < 0 || tab >= TAB_COUNT ? 0 : (tab == TAB_SAVED_EPISODES ? 5 : 2);
}

inline const char*
DiscoverTabId(int32_t tab)
{
	static const char* const ids[TAB_COUNT] = {
		"playlists", "top_tracks", "top_artists", "new_releases", "saved_albums",
		"podcasts", "followed_artists", "saved_episodes", "audiobooks"
	};
	return tab >= 0 && tab < TAB_COUNT ? ids[tab] : "";
}

inline DiscoverTab
DiscoverTabForId(const std::string& id)
{
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		if (id == DiscoverTabId(tab))
			return static_cast<DiscoverTab>(tab);
	}
	return TAB_NONE;
}

inline DiscoverTabOrder
NormalizeDiscoverTabOrder(const std::vector<std::string>& configured)
{
	DiscoverTabOrder result;
	for (const std::string& id : configured) {
		DiscoverTab tab = DiscoverTabForId(id);
		if (tab != TAB_NONE && std::find(result.begin(), result.end(), tab) == result.end())
			result.push_back(tab);
	}
	for (int32_t index = 0; index < TAB_COUNT; index++) {
		DiscoverTab tab = static_cast<DiscoverTab>(index);
		if (std::find(result.begin(), result.end(), tab) == result.end())
			result.push_back(tab);
	}
	return result;
}

inline std::vector<std::string>
DiscoverTabOrderIds(const DiscoverTabOrder& order)
{
	std::vector<std::string> result;
	for (DiscoverTab tab : order) {
		if (tab >= 0 && tab < TAB_COUNT)
			result.emplace_back(DiscoverTabId(tab));
	}
	return result;
}

inline bool
IsDiscoverTabVisible(int32_t tab, const DiscoverTabVisibility& visible,
	bool audiobooksEnabled)
{
	return tab >= 0 && tab < TAB_COUNT && visible[tab]
		&& (tab != TAB_AUDIOBOOKS || audiobooksEnabled);
}

inline int32_t
VisibleDiscoverTabCount(const DiscoverTabVisibility& visible, bool audiobooksEnabled)
{
	int32_t count = 0;
	for (int32_t tab = 0; tab < TAB_COUNT; tab++) {
		if (IsDiscoverTabVisible(tab, visible, audiobooksEnabled))
			count++;
	}
	return count;
}

inline void
EnsureVisibleDiscoverTab(DiscoverTabVisibility& visible, bool audiobooksEnabled)
{
	if (VisibleDiscoverTabCount(visible, audiobooksEnabled) == 0)
		visible[TAB_PLAYLISTS] = true;
}

inline bool
ToggleDiscoverTabVisibility(DiscoverTabVisibility& visible, int32_t tab,
	bool audiobooksEnabled)
{
	if (tab < 0 || tab >= TAB_COUNT || (tab == TAB_AUDIOBOOKS && !audiobooksEnabled))
		return false;
	if (visible[tab] && VisibleDiscoverTabCount(visible, audiobooksEnabled) <= 1)
		return false;
	visible[tab] = !visible[tab];
	return true;
}

// Snapshot of installed tabs, rebuilt together with BTabView by DiscoverWindow.
// Requested visibility and order survive a capability change independently.
struct DiscoverTabLayout {
	DiscoverTabOrder tabs;

	DiscoverTab LogicalTab(int32_t visual) const
	{
		return visual >= 0 && visual < static_cast<int32_t>(tabs.size())
			? tabs[visual] : TAB_NONE;
	}

	int32_t VisualTab(int32_t logical) const
	{
		auto found = std::find(tabs.begin(), tabs.end(), logical);
		return found == tabs.end() ? -1 : static_cast<int32_t>(found - tabs.begin());
	}

	int32_t RestoreSelection(int32_t logical) const
	{
		int32_t visual = VisualTab(logical);
		return visual >= 0 || tabs.empty() ? visual : 0;
	}
};

inline DiscoverTabLayout
MakeDiscoverTabLayout(const DiscoverTabOrder& order,
	const DiscoverTabVisibility& visible, bool audiobooksEnabled)
{
	DiscoverTabLayout layout;
	for (DiscoverTab tab : order) {
		if (IsDiscoverTabVisible(tab, visible, audiobooksEnabled))
			layout.tabs.push_back(tab);
	}
	return layout;
}

// Move across the complete order so hidden tabs keep their existing positions
// relative to the remaining tabs. A move right inserts after the target.
inline bool
MoveDiscoverTab(DiscoverTabOrder& order, DiscoverTab sourceTab, DiscoverTab targetTab)
{
	if (sourceTab == targetTab)
		return false;
	auto source = std::find(order.begin(), order.end(), sourceTab);
	auto target = std::find(order.begin(), order.end(), targetTab);
	if (source == order.end() || target == order.end())
		return false;
	bool movingRight = source < target;
	order.erase(source);
	target = std::find(order.begin(), order.end(), targetTab);
	if (movingRight)
		++target;
	order.insert(target, sourceTab);
	return true;
}

inline DiscoverTab
DiscoverLibraryTargetTab(SpotifyItemKind kind, bool audiobooksEnabled)
{
	DiscoverTab tab = DiscoverTabForId(SpotifyLibraryTargetId(kind));
	return tab == TAB_AUDIOBOOKS && !audiobooksEnabled ? TAB_NONE : tab;
}

// The boundary reader validates URI/kind consistency and the intent enum.
// This policy consumes that validated DragItem without losing its provenance.
inline DiscoverTab
DiscoverDropTargetTab(const MessageContracts::DragItem& item, bool audiobooksEnabled)
{
	if (item.intent == MessageContracts::DropIntent::Reorder
			|| SpotifyItemIdForUri(item.uri).empty())
		return TAB_NONE;
	return DiscoverLibraryTargetTab(item.kind, audiobooksEnabled);
}

struct DiscoverDragHover {
	DiscoverTab target = TAB_NONE;
	bool scheduleTabSwitch = false;
	bool showRowMarker = false;
};

inline DiscoverDragHover
ResolveDiscoverDragHover(const MessageContracts::DragItem& item,
	const DiscoverTabVisibility& visible, bool audiobooksEnabled,
	int32_t selectedTab, int32_t hoveredTab)
{
	DiscoverTab target = DiscoverDropTargetTab(item, audiobooksEnabled);
	if (!IsDiscoverTabVisible(target, visible, audiobooksEnabled))
		return {};
	return {target, hoveredTab == target, selectedTab == target};
}

inline bool
PrimaryUriMatchesTab(int32_t tab, const std::string& uri)
{
	SpotifyItemKind kind = SpotifyItemKindForUri(uri);
	if (tab == TAB_PLAYLISTS && uri == "spotify:collection")
		return true;
	if (SpotifyItemIdForUri(uri).empty())
		return false;
	if (tab == TAB_TOP_TRACKS) return kind == kSpotifyItemTrack;
	if (tab == TAB_TOP_ARTISTS || tab == TAB_FOLLOWED_ARTISTS)
		return kind == kSpotifyItemArtist;
	if (tab == TAB_NEW_RELEASES || tab == TAB_SAVED_ALBUMS)
		return kind == kSpotifyItemAlbum;
	if (tab == TAB_PODCASTS) return kind == kSpotifyItemShow;
	if (tab == TAB_SAVED_EPISODES) return kind == kSpotifyItemEpisode;
	if (tab == TAB_AUDIOBOOKS) return kind == kSpotifyItemAudiobook;
	return tab == TAB_PLAYLISTS && kind == kSpotifyItemPlaylist;
}
