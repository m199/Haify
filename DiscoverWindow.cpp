#include "DiscoverWindow.h"
#include "DiscoverListView.h"
#include "TextInputDialog.h"
#include "Messages.h"
#include "SettingsController.h"
#include "App.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"
#include "spotify/api/SpotifyResponse.h"
#include <nlohmann/json.hpp>
#include "TrackContextMenu.h"
#include "UiLogic.h"

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <File.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <TabView.h>
#include <Catalog.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>
#include <set>
#include <thread>
#include <time.h>
#include <utility>
#include <unistd.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DiscoverWindow"

enum {
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

static_assert(TAB_COUNT == kDiscoverTabCount, "Tab count mismatch");
static const uint32 kMsgCheckLazyLoad = 'dLzy';
static const uint32 kMsgPageDone = 'dPgD';
static const uint32 kMsgCacheLoaded = 'dCch';
static const uint32 kMsgSaveCache = 'dCsv';
static const uint32 kMsgLibraryStateCached = 'dLSt';
static const uint32 kMsgAudiobookIdsUpdated = 'dAId';
static const int32 kDiscoverCacheVersion = 5;
static const int32 kMaxDiscoverCachedRowsPerTab = 500;
static const int32 kDiscoverCacheBatchRows = 50;

struct TabDef { const char* id; const char* label; };
static const TabDef kTabDefs[TAB_COUNT] = {
	{ "playlists",        B_TRANSLATE_MARK("Playlists")        },
	{ "top_tracks",       B_TRANSLATE_MARK("Top Tracks")       },
	{ "top_artists",      B_TRANSLATE_MARK("Top Artists")      },
	{ "new_releases",     B_TRANSLATE_MARK("New Releases")     },
	{ "saved_albums",     B_TRANSLATE_MARK("Saved Albums")     },
	{ "podcasts",         B_TRANSLATE_MARK("Podcasts")         },
	{ "followed_artists", B_TRANSLATE_MARK("Followed Artists") },
	{ "saved_episodes",   B_TRANSLATE_MARK("Saved Episodes")   },
	{ "audiobooks",       B_TRANSLATE_MARK("Audiobooks")       },
};

static const std::vector<ColDef> kTabCols[TAB_COUNT] = {
	  { {"Name",      220, kColOpenOnDouble},
	                        {"Owner",     140, kColNone} },
	  { {"Track",     220, kColPlayOnDouble},
	                        {"Artist",    140, kColOpenOnDouble} },
	  { {"Artist",    220, kColOpenOnDouble},
	                        {"Genre",     140, kColNone} },
	  { {"Album",     220, kColOpenOnDouble},
	                        {"Artist",    140, kColOpenOnDouble} },
	  { {"Album",     220, kColOpenOnDouble},
	                        {"Artist",    140, kColOpenOnDouble} },
	  { {"Podcast",   220, kColOpenOnDouble},
	                        {"Publisher", 140, kColNone} },
	  { {"Artist",    220, kColOpenOnDouble},
	                        {"Genre",     140, kColNone} },
	  { {"Episode",   220, kColPlayOnDouble},
	                        {"Show",      140, kColOpenOnDouble},
	                        {"Date",       90, kColNone},
	                        {"Duration",   70, kColNone},
	                        {"Progress",   70, kColNone} },
	  { {"Audiobook", 220, kColOpenOnDouble},
	                        {"Author",    140, kColNone} },
};

static bool
PrimaryUriMatchesTab(int32 tab, const std::string& uri)
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

struct RowData {
	std::vector<std::string> vals;
	std::vector<std::string> uris;
	std::vector<std::string> ttls;
	bool writable = true;
	bool owned = false;
};

static BLocker sDiscoverCacheWriterLock("Haify discover cache writer");
static std::map<std::string, uint64> sDiscoverCacheWriteGenerations;
static uint64 sNextDiscoverCacheWriteGeneration = 0;

static std::string
SafeCacheName(const std::string& accountId)
{
	std::string name;
	for (unsigned char character : accountId) {
		if (isalnum(character) || character == '-' || character == '_')
			name += (char)character;
		else
			name += '_';
		if (name.size() >= 96)
			break;
	}
	return name;
}

static std::string
DiscoverCachePath(const std::string& accountId, bool createDirectory)
{
	std::string name = SafeCacheName(accountId);
	if (name.empty())
		return "";
	return SettingsController::CacheFilePath("discover",
		name + ".json", createDirectory);
}

static uint64
BeginDiscoverCacheWrite(const std::string& path)
{
	BAutolock lock(&sDiscoverCacheWriterLock);
	uint64 generation = ++sNextDiscoverCacheWriteGeneration;
	sDiscoverCacheWriteGenerations[path] = generation;
	return generation;
}

static bool
ReadDiscoverCacheFile(const std::string& path, nlohmann::json& existing)
{
	BFile file(path.c_str(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;
	off_t size = 0;
	if (file.GetSize(&size) != B_OK || size <= 0
			|| size > 50LL * 1024LL * 1024LL) {
		return false;
	}
	std::string content((size_t)size, '\0');
	if (file.Read(&content[0], (size_t)size) != size)
		return false;
	try {
		existing = nlohmann::json::parse(content);
		return true;
	} catch (...) {
		return false;
	}
}

static bool
IsCompatibleDiscoverCache(const nlohmann::json& existing,
	const nlohmann::json& data)
{
	return existing.value("version", 0) == kDiscoverCacheVersion
		&& existing.value("account_id", "") == data.value("account_id", "")
		&& existing.contains("tabs") && existing["tabs"].is_object();
}

static void
MergeDiscoverCacheTabs(nlohmann::json& data, const nlohmann::json& existing)
{
	for (auto tab = existing["tabs"].begin(); tab != existing["tabs"].end();
			++tab) {
		if (!data["tabs"].contains(tab.key()))
			data["tabs"][tab.key()] = tab.value();
	}
	if (!data.contains("audiobook_ids") && existing.contains("audiobook_ids")
			&& existing["audiobook_ids"].is_array()) {
		data["audiobook_ids"] = existing["audiobook_ids"];
	}
}

static void
MergeExistingDiscoverCache(const std::string& path, nlohmann::json& data)
{
	nlohmann::json existing;
	if (ReadDiscoverCacheFile(path, existing)
			&& IsCompatibleDiscoverCache(existing, data)) {
		MergeDiscoverCacheTabs(data, existing);
	}
}

static bool
WriteDiscoverCacheFile(const std::string& path, uint64 generation,
	const nlohmann::json& data)
{
	std::string serialized = data.dump();
	std::string temporary = path + ".part-" + std::to_string(generation);
	BFile file(temporary.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	bool written = file.InitCheck() == B_OK
		&& file.Write(serialized.data(), serialized.size())
			== (ssize_t)serialized.size();
	file.Unset();
	return written;
}

static bool
TakeCurrentDiscoverCacheWrite(const std::string& path, uint64 generation)
{
	BAutolock lock(&sDiscoverCacheWriterLock);
	auto latest = sDiscoverCacheWriteGenerations.find(path);
	bool current = latest != sDiscoverCacheWriteGenerations.end()
		&& latest->second == generation;
	if (current)
		sDiscoverCacheWriteGenerations.erase(latest);
	return current;
}

static void
CommitDiscoverCacheWrite(const std::string& path, uint64 generation,
	bool written)
{
	std::string temporary = path + ".part-" + std::to_string(generation);
	bool current = TakeCurrentDiscoverCacheWrite(path, generation);
	if (written && current) {
		unlink(path.c_str());
		rename(temporary.c_str(), path.c_str());
		return;
	}
	unlink(temporary.c_str());
}

static void
WriteDiscoverCacheAsync(const std::string& path, nlohmann::json data)
{
	if (path.empty())
		return;
	uint64 generation = BeginDiscoverCacheWrite(path);
	std::thread([path, generation, data = std::move(data)]() mutable {
		MergeExistingDiscoverCache(path, data);
		bool written = WriteDiscoverCacheFile(path, generation, data);
		CommitDiscoverCacheWrite(path, generation, written);
	}).detach();
}

static void
AddAudiobookIdsToDiscoverCache(nlohmann::json& cache,
	const std::set<std::string>& audiobookIds)
{
	cache["audiobook_ids"] = nlohmann::json::array();
	for (const std::string& id : audiobookIds)
		cache["audiobook_ids"].push_back(id);
}

static bool
BuildCachedDiscoverRow(int32 tab, DiscoverRow* row, nlohmann::json& rowJson)
{
	if (!row || row->fUris.empty()
			|| !PrimaryUriMatchesTab(tab, row->fUris[0]))
		return false;
	size_t columns = kTabCols[tab].size();
	if (row->fUris.size() < columns)
		return false;

	nlohmann::json values = nlohmann::json::array();
	nlohmann::json uris = nlohmann::json::array();
	nlohmann::json titles = nlohmann::json::array();
	for (size_t column = 0; column < columns; column++) {
		BStringField* field = dynamic_cast<BStringField*>(
			row->GetField((int32)column));
		values.push_back(field ? field->String() : "");
		uris.push_back(row->fUris[column]);
		titles.push_back(column < row->fTitles.size()
			? row->fTitles[column] : "");
	}
	rowJson = {{"values", std::move(values)}, {"uris", std::move(uris)},
		{"titles", std::move(titles)}, {"writable", row->fWritable},
		{"owned", row->fOwned}};
	return true;
}

static nlohmann::json
BuildCachedDiscoverRows(int32 tab, BColumnListView* list)
{
	nlohmann::json rows = nlohmann::json::array();
	for (int32 index = 0; index < list->CountRows()
			&& (int32)rows.size() < kMaxDiscoverCachedRowsPerTab; index++) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(list->RowAt(index));
		nlohmann::json rowJson;
		if (BuildCachedDiscoverRow(tab, row, rowJson))
			rows.push_back(std::move(rowJson));
	}
	return rows;
}

static void
AddDiscoverTabCache(nlohmann::json& tabs, int32 tab, BColumnListView* list,
	bool freshSnapshot, bool cacheBacked)
{
	if (!list || (!freshSnapshot && !cacheBacked))
		return;
	tabs[kTabDefs[tab].id] = BuildCachedDiscoverRows(tab, list);
}

static nlohmann::json
BuildDiscoverCachePayload(const std::string& accountId,
	const std::set<std::string>& audiobookIds, bool audiobookIdsKnown,
	BColumnListView* const lists[], const bool cacheBacked[],
	const bool freshSnapshot[])
{
	nlohmann::json cache = {
		{"version", kDiscoverCacheVersion},
		{"account_id", accountId},
		{"saved_at", (int64)time(nullptr)},
		{"tabs", nlohmann::json::object()}
	};
	if (audiobookIdsKnown)
		AddAudiobookIdsToDiscoverCache(cache, audiobookIds);
	for (int32 tab = 0; tab < TAB_COUNT; tab++) {
		AddDiscoverTabCache(cache["tabs"], tab, lists[tab],
			freshSnapshot[tab], cacheBacked[tab]);
	}
	return cache;
}

static std::string
PlaylistUri(const std::string& id)
{
	return SpotifyItemKindForUri(id) == kSpotifyItemPlaylist
		? id : SpotifyUriForItemKind(kSpotifyItemPlaylist, id);
}


static void
PostPlaylistChange(const char* operation, const std::string& id,
	const std::string& name = "", const std::string& owner = "",
	bool writable = true, bool owned = false)
{
	BMessage changed(MSG_PLAYLISTS_CHANGED);
	changed.AddString("operation", operation);
	changed.AddString("id", id.c_str());
	changed.AddString("uri", PlaylistUri(id).c_str());
	if (!name.empty())
		changed.AddString("name", name.c_str());
	if (!owner.empty())
		changed.AddString("owner", owner.c_str());
	changed.AddBool("writable", writable);
	changed.AddBool("owned", owned);
	be_app->PostMessage(&changed);
}


static void
PostLibraryChange(const char* operation, const std::string& uri)
{
	BMessage changed(MSG_LIBRARY_CHANGED);
	changed.AddString("operation", operation);
	changed.AddString("uri", uri.c_str());
	be_app->PostMessage(&changed);
}

static std::string
JsonString(const nlohmann::json& object, const char* key,
	const char* fallback = "")
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_string())
		return fallback;
	return object[key].get<std::string>();
}

static int32
JsonInt32(const nlohmann::json& object, const char* key, int32 fallback = 0)
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_number_integer())
		return fallback;
	return object[key].get<int32>();
}

static bool
JsonBool(const nlohmann::json& object, const char* key, bool fallback = false)
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_boolean())
		return fallback;
	return object[key].get<bool>();
}


static nlohmann::json
MutationResponseBody(const nlohmann::json& response)
{
	if (!response.is_object() || !response.contains("body")
			|| !response["body"].is_string())
		return response;
	try {
		const std::string body = response["body"].get<std::string>();
		return body.empty() ? nlohmann::json::object()
			: nlohmann::json::parse(body);
	} catch (...) {
		return nlohmann::json::object();
	}
}

static std::string
DurationText(int32 milliseconds)
{
	int32 seconds = std::max((int32)0, milliseconds) / 1000;
	char text[32];
	snprintf(text, sizeof(text), "%ld:%02ld", (long)(seconds / 60),
		(long)(seconds % 60));
	return text;
}

class DiscoverTabView : public BTabView {
public:
	DiscoverTabView() : BTabView("tabs", B_WIDTH_FROM_LABEL) {}

	virtual void Select(int32 tab) {
		BTabView::Select(tab);
		if (Window()) {
			BMessage msg('tabS');
			msg.AddInt32("tab", tab);
			Window()->PostMessage(&msg);
		}
	}

	virtual void MouseDown(BPoint where) {
		fSource = _TabAt(where);
		fTarget = fSource;
		fStart = where;
		fDragging = false;
		BTabView::MouseDown(where);
		if (fSource >= 0)
			SetMouseEventMask(B_POINTER_EVENTS, 0);
	}

	virtual void MouseMoved(BPoint where, uint32 transit,
		const BMessage* dragMessage) {
		if (fSource >= 0) {
			if (!fDragging && std::fabs(where.x - fStart.x) >= 4.0f)
				fDragging = true;
			if (fDragging) {
				int32 target = _TabAt(where);
				if (target != fTarget) {
					fTarget = target;
					Invalidate();
				}
				return;
			}
		}
		BTabView::MouseMoved(where, transit, dragMessage);
	}

	virtual void MouseUp(BPoint where) {
		if (fDragging && fSource >= 0 && fTarget >= 0
				&& fSource != fTarget && Window()) {
			BMessage reorder('tRdr');
			reorder.AddInt32("source", fSource);
			reorder.AddInt32("target", fTarget);
			Window()->PostMessage(&reorder);
		}
		fSource = fTarget = -1;
		fDragging = false;
		Invalidate();
		BTabView::MouseUp(where);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes) {
		if (numBytes == 1 && bytes[0] == B_ESCAPE && fSource >= 0) {
			fSource = fTarget = -1;
			fDragging = false;
			Invalidate();
			return;
		}
		BTabView::KeyDown(bytes, numBytes);
	}

	virtual void Draw(BRect update) {
		BTabView::Draw(update);
		if (fDragging && fTarget >= 0 && fTarget < CountTabs()) {
			BRect frame = TabFrame(fTarget);
			SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
			StrokeRect(frame.InsetByCopy(1, 1));
		}
	}

private:
	int32 _TabAt(BPoint where) const {
		for (int32 i = 0; i < CountTabs(); i++) {
			if (TabFrame(i).Contains(where))
				return i;
		}
		return -1;
	}

	int32 fSource = -1;
	int32 fTarget = -1;
	BPoint fStart;
	bool fDragging = false;
};

class DiscoverWindowDropFilter : public BMessageFilter {
public:
	explicit DiscoverWindowDropFilter(DiscoverWindow* window)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE), fWindow(window) {}

	filter_result Filter(BMessage* message, BHandler** target) override {
		if (!fWindow || !message || message->what != 'drag'
				|| !message->WasDropped())
			return B_DISPATCH_MESSAGE;
		if (target && fWindow->ForwardDroppedMessage(message, *target))
			return B_SKIP_MESSAGE;
		BMessage drop(*message);
		drop.what = 'dDrp';
		fWindow->PostMessage(&drop);
		return B_SKIP_MESSAGE;
	}

private:
	DiscoverWindow* fWindow;
};


DiscoverWindow::DiscoverWindow()
	: BWindow(BRect(250, 200,
		250 + kDefaultDiscoverWindowWidth,
		200 + kDefaultDiscoverWindowHeight), "Discover",
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	  fPlaylistSyncGeneration(0)
{
	memset(fTabs,         0, sizeof(fTabs));
	memset(fLists,        0, sizeof(fLists));
	memset(fTabMenuItems, 0, sizeof(fTabMenuItems));
	memset(fTabMap,       0, sizeof(fTabMap));
	memset(fLoaded,       0, sizeof(fLoaded));
	memset(fLoadTime,     0, sizeof(fLoadTime));
	memset(fPageLoading,  0, sizeof(fPageLoading));
	memset(fPageHasMore,  0, sizeof(fPageHasMore));
	memset(fPageOffset,   0, sizeof(fPageOffset));
	memset(fTabLoadGeneration, 0, sizeof(fTabLoadGeneration));
	memset(fCacheLoadGeneration, 0, sizeof(fCacheLoadGeneration));
	memset(fCacheLoadPending, 0, sizeof(fCacheLoadPending));
	memset(fCacheBacked,  0, sizeof(fCacheBacked));
	memset(fFreshSnapshot, 0, sizeof(fFreshSnapshot));
	HaifySettings s = SettingsController::Load();
	fCacheAccountId = s.spotifyAccountId;
	_LoadTabVisibility(s);
	_LoadTabOrder(s);
	if (s.browserWindowW > 0) {
		MoveTo(s.browserWindowX, s.browserWindowY);
		ResizeTo(s.browserWindowW, s.browserWindowH);
	}

	_InitMenu();
	_InitLayout();
	AddCommonFilter(new DiscoverWindowDropFilter(this));

	int32 initialTab = _LogicalTab(fTabView ? fTabView->Selection() : 0);
	if (initialTab >= 0) {
		_LoadPersistentCache(initialTab);
		_LoadTab(initialTab);
	}
	BMessage lazy(kMsgCheckLazyLoad);
	fLazyLoadRunner = new BMessageRunner(BMessenger(this), &lazy, 500000LL);
}


DiscoverWindow::~DiscoverWindow()
{
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	_WriteCacheNow();
	delete fLazyLoadRunner;
	for (auto& pending : fPendingPlaylistRemovals)
		delete pending.second.row;
	fPendingPlaylistRemovals.clear();

	for (int32 i = 0; i < TAB_COUNT; i++) {
		// BTab owns its content view. Tabs currently installed in fTabView are
		// released by BTabView; hidden tabs remain our responsibility.
		if (fTabs[i] && fTabView->IndexOf(fTabs[i]) < 0)
			delete fTabs[i];
	}
}


bool
DiscoverWindow::QuitRequested()
{
	App* app = dynamic_cast<App*>(be_app);
	if (!(app && app->IsQuitting())) {
		BRect f = Frame();
		SettingsController::Update([&](HaifySettings& s) {
			s.browserWindowOpen = false;
			s.browserWindowX = f.left;  s.browserWindowY = f.top;
			s.browserWindowW = f.Width(); s.browserWindowH = f.Height();
			_SaveTabVisibility(s);
			_SaveTabOrder(s);
		});
	}
	return true;
}


int32
DiscoverWindow::_LogicalTab(int32 visual) const
{
	if (visual >= 0 && fTabView && visual < fTabView->CountTabs())
		return fTabMap[visual];
	return -1;
}


BColumnListView*
DiscoverWindow::_MakeList(int32 i)
{
	return new DiscoverListView(kTabDefs[i].label, kTabCols[i], i, true);
}


bool
DiscoverWindow::ForwardDroppedMessage(BMessage* message, BHandler* target)
{
	BView* targetView = dynamic_cast<BView*>(target);
	if (!message || !targetView)
		return false;

	for (int32 i = 0; i < TAB_COUNT; i++) {
		DiscoverListView* list = dynamic_cast<DiscoverListView*>(fLists[i]);
		if (!list)
			continue;
		for (BView* view = targetView; view; view = view->Parent()) {
			if (view == list || view == list->ScrollView()) {
				list->ForwardDroppedMessage(message);
				return true;
			}
		}
	}
	return false;
}


void
DiscoverWindow::_RebuildTabs()
{
	int32 prevLogical = _LogicalTab(fTabView->Selection());

	while (fTabView->CountTabs() > 0) {
		BTab* tab = fTabView->RemoveTab(0);
		if (!tab)
			continue;
		for (int32 i = 0; i < TAB_COUNT; i++) {
			if (tab->View() == fLists[i]) {
				fTabs[i] = tab;
				break;
			}
		}
	}

	int32 visual = 0, restoreVisual = 0;
	for (int32 orderIndex : fTabOrder) {
		int i = (int)orderIndex;
		if (!_IsTabEffectivelyVisible(i)) continue;
		if (!fLists[i])
			fLists[i] = _MakeList(i);
		fTabView->AddTab(fLists[i], fTabs[i]);
		fTabs[i] = fTabView->TabAt(visual);
		fTabView->TabAt(visual)->SetLabel(B_TRANSLATE(kTabDefs[i].label));
		fTabMap[visual] = i;
		if (i == prevLogical) restoreVisual = visual;
		visual++;
	}

	if (fTabView->CountTabs() > 0)
		fTabView->Select(restoreVisual);
}


void
DiscoverWindow::_LoadTabVisibility(const HaifySettings& settings)
{
	fTabVisible[TAB_PLAYLISTS]     = settings.discoverTabPlaylists;
	fTabVisible[TAB_TOP_TRACKS]    = settings.discoverTabTopTracks;
	fTabVisible[TAB_TOP_ARTISTS]   = settings.discoverTabTopArtists;
	fTabVisible[TAB_NEW_RELEASES]  = settings.discoverTabNewReleases;
	fTabVisible[TAB_SAVED_ALBUMS]  = settings.discoverTabSavedAlbums;
	fTabVisible[TAB_PODCASTS]      = settings.discoverTabPodcasts;
	fTabVisible[TAB_FOLLOWED_ARTISTS] = settings.discoverTabFollowedArtists;
	fTabVisible[TAB_SAVED_EPISODES] = settings.discoverTabSavedEpisodes;
	fTabVisible[TAB_AUDIOBOOKS] = settings.discoverTabAudiobooks;

	bool anyVisible = false;
	for (int i = 0; i < TAB_COUNT; i++)
		anyVisible = anyVisible || _IsTabEffectivelyVisible(i);
	if (!anyVisible)
		fTabVisible[TAB_PLAYLISTS] = true;
}


void
DiscoverWindow::_SaveTabVisibility(HaifySettings& settings) const
{
	settings.discoverTabPlaylists    = fTabVisible[TAB_PLAYLISTS];
	settings.discoverTabTopTracks    = fTabVisible[TAB_TOP_TRACKS];
	settings.discoverTabTopArtists   = fTabVisible[TAB_TOP_ARTISTS];
	settings.discoverTabNewReleases  = fTabVisible[TAB_NEW_RELEASES];
	settings.discoverTabSavedAlbums  = fTabVisible[TAB_SAVED_ALBUMS];
	settings.discoverTabPodcasts     = fTabVisible[TAB_PODCASTS];
	settings.discoverTabFollowedArtists = fTabVisible[TAB_FOLLOWED_ARTISTS];
	settings.discoverTabSavedEpisodes = fTabVisible[TAB_SAVED_EPISODES];
	settings.discoverTabAudiobooks = fTabVisible[TAB_AUDIOBOOKS];
}


void
DiscoverWindow::_LoadTabOrder(const HaifySettings& settings)
{
	fTabOrder.clear();
	std::vector<std::string> known;
	for (int32 i = 0; i < TAB_COUNT; i++) known.push_back(kTabDefs[i].id);
	std::vector<std::string> normalized = NormalizeStableOrder(
		settings.discoverTabOrder, known);
	for (const std::string& id : normalized) {
		for (int32 i = 0; i < TAB_COUNT; i++) {
			if (id == kTabDefs[i].id
					&& std::find(fTabOrder.begin(), fTabOrder.end(), i)
						== fTabOrder.end()) {
				fTabOrder.push_back(i);
				break;
			}
		}
	}
}


void
DiscoverWindow::_SaveTabOrder(HaifySettings& settings) const
{
	settings.discoverTabOrder.clear();
	for (int32 logical : fTabOrder) {
		if (logical >= 0 && logical < TAB_COUNT)
			settings.discoverTabOrder.push_back(kTabDefs[logical].id);
	}
}


bool
DiscoverWindow::_AudiobooksEnabled() const
{
	App* app = dynamic_cast<App*>(be_app);
	return app && app->GetCapabilities()
		&& app->GetCapabilities()->AudiobooksEnabled();
}


bool
DiscoverWindow::_IsTabEffectivelyVisible(int32 logical) const
{
	if (logical < 0 || logical >= TAB_COUNT || !fTabVisible[logical])
		return false;
	return logical != TAB_AUDIOBOOKS || _AudiobooksEnabled();
}


void
DiscoverWindow::_MoveTab(int32 sourceVisual, int32 targetVisual)
{
	if (!fTabView || sourceVisual < 0 || targetVisual < 0
			|| sourceVisual >= fTabView->CountTabs()
			|| targetVisual >= fTabView->CountTabs()
			|| sourceVisual == targetVisual)
		return;
	int32 sourceLogical = fTabMap[sourceVisual];
	int32 targetLogical = fTabMap[targetVisual];
	auto source = std::find(fTabOrder.begin(), fTabOrder.end(), sourceLogical);
	auto target = std::find(fTabOrder.begin(), fTabOrder.end(), targetLogical);
	if (source == fTabOrder.end() || target == fTabOrder.end())
		return;
	bool movingRight = source < target;
	fTabOrder.erase(source);
	target = std::find(fTabOrder.begin(), fTabOrder.end(), targetLogical);
	if (movingRight && target != fTabOrder.end())
		++target;
	fTabOrder.insert(target, sourceLogical);
	SettingsController::Update([&](HaifySettings& settings) {
		_SaveTabOrder(settings);
	});
	_RebuildTabs();
}


void
DiscoverWindow::_HandleLibraryDrop(const std::string& uri)
{
	SpotifyItemKind kind = SpotifyItemKindForUri(uri);
	if (kind == kSpotifyItemUnknown)
		return;
	if (kind == kSpotifyItemAudiobook && !_AudiobooksEnabled()) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Audiobooks are not available for this account or market."),
			B_TRANSLATE("OK"));
		alert->Go();
		return;
	}
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api) return;
	BMessenger self(this);
	api->Library().CheckLibraryItems({uri}, [self, uri](bool ok,
			const nlohmann::json& data) {
		BMessage result('dSts');
		result.AddString("uri", uri.c_str());
		result.AddBool("ok", ok);
		result.AddBool("saved", ok && data.is_array() && !data.empty()
			&& data[0].is_boolean() && data[0].get<bool>());
		self.SendMessage(&result);
	});
}


bool
DiscoverWindow::_HandlePlaylistDrop(const std::string& itemUri,
	const std::string& targetUri, bool writable)
{
	SpotifyItemKind kind = SpotifyItemKindForUri(itemUri);
	if (!SpotifyItemCanAddToPlaylist(kind) || targetUri.empty())
		return false;

	if (targetUri == "spotify:collection") {
		if (kind == kSpotifyItemTrack) {
			_HandleLibraryDrop(itemUri);
		} else {
			BAlert* alert = new BAlert("", B_TRANSLATE(
				"Only songs can be added to Liked Songs."), B_TRANSLATE("OK"));
			alert->Go();
		}
		return true;
	}

	if (SpotifyItemKindForUri(targetUri) != kSpotifyItemPlaylist)
		return false;
	if (!writable) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"This playlist cannot be modified."), B_TRANSLATE("OK"));
		alert->Go();
		return true;
	}

	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return true;

	BMessenger self(this);
	std::string playlistId = targetUri.substr(17);
	api->Playlists().AddTrackToPlaylist(playlistId, itemUri,
		[self](bool ok, const nlohmann::json& data) {
			BMessage result('dPlA');
			result.AddBool("ok", ok);
			result.AddInt32("status", SpotifyResponseStatus(data));
			self.SendMessage(&result);
		});
	return true;
}


void
DiscoverWindow::_SelectLibraryTarget(const std::string& uri)
{
	const char* targetId = SpotifyLibraryTargetId(SpotifyItemKindForUri(uri));
	int32 logical = -1;
	for (int32 i = 0; i < TAB_COUNT; i++) {
		if (strcmp(kTabDefs[i].id, targetId) == 0) {
			logical = i;
			break;
		}
	}
	if (logical < 0 || (logical == TAB_AUDIOBOOKS && !_AudiobooksEnabled()))
		return;
	if (!fTabVisible[logical]) {
		fTabVisible[logical] = true;
		if (fTabMenuItems[logical])
			fTabMenuItems[logical]->SetMarked(true);
		SettingsController::Update([&](HaifySettings& settings) {
			_SaveTabVisibility(settings);
		});
	}
	_RebuildTabs();
	for (int32 visual = 0; visual < fTabView->CountTabs(); visual++) {
		if (fTabMap[visual] == logical) {
			fTabView->Select(visual);
			break;
		}
	}
	// Selecting an untouched tab still needs its initial load. A tab that is
	// already populated must remain intact; the library delta adds its new row.
	if (!fLoaded[logical])
		_LoadTab(logical);
}


void
DiscoverWindow::MessageReceived(BMessage* message)
{
	if (_HandleTabMessage(message) || _HandleDataMessage(message)
			|| _HandlePlaybackOpenMessage(message)
			|| _HandleLibraryActionMessage(message)
			|| _HandlePlaylistActionMessage(message)
			|| _HandleAppForwardMessage(message)) {
		return;
	}

	BWindow::MessageReceived(message);
}


bool
DiscoverWindow::_HandleTabMessage(BMessage* message)
{
	switch (message->what) {
		case 'togT':
			_ToggleTabVisibility(message);
			return true;

		case 'tRdr':
			_MoveTab(message->GetInt32("source", -1),
				message->GetInt32("target", -1));
			return true;

		case 'tRst':
			_ResetTabOrder();
			return true;

		case MSG_SPOTIFY_CAPABILITIES_CHANGED:
			_ApplySpotifyCapabilities();
			return true;

		case 'tabS':
			_SelectTab(message);
			return true;

		case kMsgAudiobookIdsUpdated:
			_ApplyAudiobookIdSnapshot(message);
			return true;

		default:
			return false;
	}
}


bool
DiscoverWindow::_HandleDataMessage(BMessage* message)
{
	switch (message->what) {
		case kMsgCacheLoaded:
		case 'uRow':
			_ApplyDiscoverRows(message);
			return true;

		case kMsgCheckLazyLoad:
			_CheckLazyLoad();
			return true;

		case kMsgPageDone:
			_ApplyPageDone(message);
			return true;

		case kMsgSaveCache:
			_SaveCacheNowFromMessage();
			return true;

		case 'lddt':
			LoadData();
			return true;

		default:
			return false;
	}
}


bool
DiscoverWindow::_HandlePlaybackOpenMessage(BMessage* message)
{
	switch (message->what) {
		case 'play':
			_ForwardPlayback(message);
			return true;

		case 'open':
			_ForwardOpenRequest(message);
			return true;

		case 'pStU':
			_ApplyPlayingTrackUpdate(message);
			return true;

		case 'rClk':
			_ShowDiscoverContextMenu(message);
			return true;

		case 'iCmR':
			_ShowPlayableContextMenu(message);
			return true;

		case kMsgLibraryStateCached:
			_ApplyLibraryStateCached(message);
			return true;

		case 'tply':
			_PlayTrackFromMessage(message);
			return true;

		default:
			return false;
	}
}


bool
DiscoverWindow::_HandleLibraryActionMessage(BMessage* message)
{
	switch (message->what) {
		case 'dDrp':
			_HandleDiscoverDrop(message);
			return true;

		case 'dPlA':
			_ApplyPlaylistDropResult(message);
			return true;

		case 'dSts':
			_ApplyLibraryStatusResult(message);
			return true;

		case 'dAdd':
			_ApplyLibraryAddResult(message);
			return true;

		case 'savA':
			_SaveAlbumFromMessage(message);
			return true;

		case 'remA':
			_RemoveAlbumFromMessage(message);
			return true;

		case 'remI':
			_RemoveFollowedItem(message);
			return true;

		case 'rmIR':
			_ApplyRemoveFollowedItemResult(message);
			return true;

		case 'remL':
			_RemovePlayableFromLibrary(message);
			return true;

		default:
			return false;
	}
}


bool
DiscoverWindow::_HandlePlaylistActionMessage(BMessage* message)
{
	switch (message->what) {
		case 'plNw':
			_ShowNewPlaylistDialog();
			return true;

		case 'plNc':
			_CreatePlaylist(message);
			return true;

		case 'plCr':
			_ApplyPlaylistCreateResult(message);
			return true;

		case 'plRn':
			_ShowRenamePlaylistDialog(message);
			return true;

		case 'plRc':
			_RenamePlaylist(message);
			return true;

		case 'plRr':
			_ApplyPlaylistRenameResult(message);
			return true;

		case 'plDl':
			_DeletePlaylist(message);
			return true;

		case 'plDr':
			_ApplyPlaylistDeleteResult(message);
			return true;

		case MSG_PLAYLISTS_CHANGED:
			_ApplyPlaylistsChanged(message);
			return true;

		case MSG_LIBRARY_CHANGED:
			_ApplyLibraryChanged(message);
			return true;

		case 'lAdd':
			_ApplyResolvedLibraryAddition(message);
			return true;

		case 'pSyn':
			_ApplyPlaylistSnapshot(message);
			return true;

		case 'rTab':
			_ReloadTabFromMessage(message);
			return true;

		default:
			return false;
	}
}


bool
DiscoverWindow::_HandleAppForwardMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_OPEN_BROWSER:
		case MSG_OPEN_PLAYLIST:
		case MSG_INIT_AUTH:
			be_app->PostMessage(message);
			return true;

		case 'sout':
			be_app->PostMessage('sout');
			return true;

		default:
			return false;
	}
}


void
DiscoverWindow::_SaveCacheNowFromMessage()
{
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	_WriteCacheNow();
}


void
DiscoverWindow::_ToggleTabVisibility(BMessage* message)
{
	int32 tab;
	if (message->FindInt32("tab", &tab) != B_OK)
		return;
	if (tab < 0 || tab >= TAB_COUNT)
		return;
	if (tab == TAB_AUDIOBOOKS && !_AudiobooksEnabled())
		return;
	if (fTabVisible[tab]) {
		int32 visibleCount = 0;
		for (int i = 0; i < TAB_COUNT; i++) {
			if (_IsTabEffectivelyVisible(i))
				visibleCount++;
		}
		if (visibleCount <= 1)
			return;
	}
	fTabVisible[tab] = !fTabVisible[tab];
	fTabMenuItems[tab]->SetMarked(fTabVisible[tab]);
	SettingsController::Update([&](HaifySettings& settings) {
		_SaveTabVisibility(settings);
	});
	_RebuildTabs();
}


void
DiscoverWindow::_ResetTabOrder()
{
	fTabOrder.clear();
	for (int32 i = 0; i < TAB_COUNT; i++)
		fTabOrder.push_back(i);
	SettingsController::Update([&](HaifySettings& settings) {
		_SaveTabOrder(settings);
	});
	_RebuildTabs();
}


void
DiscoverWindow::_ApplySpotifyCapabilities()
{
	if (fTabMenuItems[TAB_AUDIOBOOKS]) {
		fTabMenuItems[TAB_AUDIOBOOKS]->SetEnabled(_AudiobooksEnabled());
		fTabMenuItems[TAB_AUDIOBOOKS]->SetMarked(
			fTabVisible[TAB_AUDIOBOOKS] && _AudiobooksEnabled());
	}
	_RebuildTabs();
}


void
DiscoverWindow::_SelectTab(BMessage* message)
{
	int32 visual = 0;
	message->FindInt32("tab", &visual);
	int32 logical = _LogicalTab(visual);
	if (logical < 0)
		return;
	_LoadPersistentCache(logical);
	if (fLists[logical])
		((DiscoverListView*)fLists[logical])->SetPlayingUri(fCurrentTrackUri);
	bool expired = fLoaded[logical]
		&& (fCacheBacked[logical]
			|| (system_time() - fLoadTime[logical]) > kCacheExpiry);
	if (expired && logical == TAB_PLAYLISTS) {
		ReloadPlaylists();
	} else if (!fLoaded[logical] || expired) {
		if (expired)
			_InvalidateTabCache(logical);
		fLoaded[logical] = false;
		_LoadTab(logical);
	}
}


void
DiscoverWindow::_ApplyPageDone(BMessage* message)
{
	int32 tab = message->GetInt32("tab", -1);
	if (tab < 0 || tab >= TAB_COUNT)
		return;
	if (message->GetInt32("load_generation", -1)
			!= fTabLoadGeneration[tab])
		return;
	fPageLoading[tab] = false;
	fPageHasMore[tab] = message->GetBool("has_more", false);
	fPageOffset[tab] = message->GetInt32("next_offset", fPageOffset[tab]);
	fPageCursor[tab] = message->GetString("next_cursor", "");
	_CheckLazyLoad();
}


void
DiscoverWindow::_ApplyDiscoverRows(BMessage* message)
{
	RowUpdateData update;
	if (!_ReadRowUpdate(message, update))
		return;
	if (message->GetBool("audiobook_ids_snapshot", false))
		_ApplyAudiobookIdSnapshot(message);

	if (update.tab == TAB_AUDIOBOOKS && update.snapshotMessage) {
		fAudiobookIds.clear();
		fAudiobookIdsKnown = true;
	}
	_ApplyRowUpdateRows(message, update);
	_PruneSnapshotRows(update);
	_ReorderSnapshotRows(update);
	_FinishRowUpdate(message, update);
}


bool
DiscoverWindow::_ReadRowUpdate(BMessage* message, RowUpdateData& update)
{
	if (message->FindInt32("tab", &update.tab) != B_OK)
		return false;
	if (message->FindInt32("cols", &update.cols) != B_OK || update.cols <= 0)
		return false;
	if (update.tab < 0 || update.tab >= TAB_COUNT || !fLists[update.tab])
		return false;

	update.fromCache = message->GetBool("from_cache", false);
	update.snapshotMessage = message->GetBool("snapshot", false);
	if (update.fromCache) {
		update.snapshotMessage = false;
		if (!_ApplyCacheRowUpdateStart(message, update))
			return false;
	} else if (!_ApplyFreshRowUpdateStart(message, update)) {
		return false;
	}
	_CollectRowUpdateStrings(message, update);
	update.nRows = (int32)update.allV.size() / update.cols;
	return true;
}


bool
DiscoverWindow::_ApplyCacheRowUpdateStart(BMessage* message,
	RowUpdateData& update)
{
	int32 cacheGeneration = message->GetInt32("cache_generation", -1);
	if (cacheGeneration != fCacheLoadGeneration[update.tab]
			|| message->GetString("account_id", "") != fCacheAccountId)
		return false;
	update.cacheLast = message->GetBool("cache_last", true);
	if (update.cacheLast)
		fCacheLoadPending[update.tab] = false;
	int32 selectedTab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
	if (!message->GetBool("cache_available", true)
			|| fFreshSnapshot[update.tab] || update.tab != selectedTab)
		return false;
	if (message->GetBool("cache_first", false))
		fLists[update.tab]->Clear();
	return true;
}


bool
DiscoverWindow::_ApplyFreshRowUpdateStart(BMessage* message,
	const RowUpdateData& update)
{
	int32 loadGeneration = -1;
	return message->FindInt32("load_generation", &loadGeneration) != B_OK
		|| loadGeneration == fTabLoadGeneration[update.tab];
}


void
DiscoverWindow::_CollectRowUpdateStrings(BMessage* message,
	RowUpdateData& update)
{
	const char* value;
	for (int32 i = 0; message->FindString("v", i, &value) == B_OK; i++)
		update.allV.push_back(value);
	for (int32 i = 0; message->FindString("u", i, &value) == B_OK; i++)
		update.allU.push_back(value);
	for (int32 i = 0; message->FindString("t", i, &value) == B_OK; i++)
		update.allT.push_back(value);
}


void
DiscoverWindow::_ApplyRowUpdateRows(BMessage* message, RowUpdateData& update)
{
	for (int32 rowIndex = 0; rowIndex < update.nRows; rowIndex++)
		_ApplyRowUpdateRow(message, update, rowIndex);
}


void
DiscoverWindow::_ApplyRowUpdateRow(BMessage* message, RowUpdateData& update,
	int32 rowIndex)
{
	if (!_RowUpdateColumnsAvailable(update, rowIndex))
		return;
	bool writable = true;
	message->FindBool("writable", rowIndex, &writable);
	bool owned = false;
	message->FindBool("owned", rowIndex, &owned);
	auto uris = update.allU.begin() + rowIndex * update.cols;
	if (!_AcceptRowUpdatePrimaryUri(update, *uris))
		return;
	if (_ApplyExistingRowUpdateIfPresent(update, rowIndex, writable, owned))
		return;
	if (_ShouldSkipPlaceholderRowUpdate(update, *uris))
		return;
	_AddRowUpdateRow(update, rowIndex, writable, owned);
}


bool
DiscoverWindow::_RowUpdateColumnsAvailable(const RowUpdateData& update,
	int32 rowIndex) const
{
	return (int32)update.allU.size() >= (rowIndex + 1) * update.cols
		&& (int32)update.allT.size() >= (rowIndex + 1) * update.cols;
}


bool
DiscoverWindow::_AcceptRowUpdatePrimaryUri(RowUpdateData& update,
	const std::string& uri)
{
	if (!uri.empty() && !PrimaryUriMatchesTab(update.tab, uri))
		return false;
	std::string primaryId = SpotifyItemIdForUri(uri);
	if (update.tab == TAB_PODCASTS
			&& SpotifyEffectiveItemKind(kSpotifyItemShow, primaryId,
				fAudiobookIds) == kSpotifyItemAudiobook) {
		return false;
	}
	if (update.tab == TAB_AUDIOBOOKS && !primaryId.empty()) {
		fAudiobookIds.insert(primaryId);
		fAudiobookIdsKnown = true;
	}
	if (!uri.empty()) {
		update.snapshotUris.insert(uri);
		update.snapshotOrder.push_back(uri);
	}
	return true;
}


bool
DiscoverWindow::_ApplyExistingRowUpdateIfPresent(
	const RowUpdateData& update, int32 rowIndex, bool writable, bool owned)
{
	auto uris = update.allU.begin() + rowIndex * update.cols;
	if (uris->empty())
		return false;
	DiscoverRow* existing = _FindRow(update.tab, *uris);
	if (!existing)
		return false;
	_ApplyExistingRowUpdate(existing, update, rowIndex, writable, owned);
	return true;
}


bool
DiscoverWindow::_ShouldSkipPlaceholderRowUpdate(const RowUpdateData& update,
	const std::string& uri) const
{
	return update.tab >= TAB_SAVED_ALBUMS && update.tab <= TAB_AUDIOBOOKS
		&& uri.empty() && !update.snapshotMessage
		&& _RowUpdateHasRealRow(update.tab);
}


void
DiscoverWindow::_AddRowUpdateRow(const RowUpdateData& update, int32 rowIndex,
	bool writable, bool owned)
{
	auto values = update.allV.begin() + rowIndex * update.cols;
	auto uris = update.allU.begin() + rowIndex * update.cols;
	auto titles = update.allT.begin() + rowIndex * update.cols;
	fLists[update.tab]->AddRow(new DiscoverRow(
		std::vector<std::string>(values, values + update.cols),
		std::vector<std::string>(uris, uris + update.cols),
		std::vector<std::string>(titles, titles + update.cols),
		writable, owned));
}


bool
DiscoverWindow::_RowUpdateHasRealRow(int32 tab) const
{
	for (int32 i = 0; i < fLists[tab]->CountRows(); i++) {
		DiscoverRow* existing = dynamic_cast<DiscoverRow*>(
			fLists[tab]->RowAt(i));
		if (existing && !existing->fUris.empty()
				&& !existing->fUris[0].empty())
			return true;
	}
	return false;
}


void
DiscoverWindow::_ApplyExistingRowUpdate(DiscoverRow* row,
	const RowUpdateData& update, int32 rowIndex, bool writable, bool owned)
{
	auto values = update.allV.begin() + rowIndex * update.cols;
	auto uris = update.allU.begin() + rowIndex * update.cols;
	auto titles = update.allT.begin() + rowIndex * update.cols;
	row->fUris.assign(uris, uris + update.cols);
	row->fTitles.assign(titles, titles + update.cols);
	row->fWritable = writable;
	row->fOwned = owned;
	for (int32 column = 0; column < update.cols; column++) {
		BoldStringField* field = dynamic_cast<BoldStringField*>(
			row->GetField(column));
		if (field) {
			field->SetString((values + column)->c_str());
			field->fEnabled = writable;
		}
	}
	fLists[update.tab]->UpdateRow(row);
}


void
DiscoverWindow::_PruneSnapshotRows(const RowUpdateData& update)
{
	if (!update.snapshotMessage)
		return;
	bool keptPlaceholder = false;
	for (int32 index = fLists[update.tab]->CountRows() - 1;
			index >= 0; index--) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(
			fLists[update.tab]->RowAt(index));
		if (!row)
			continue;
		std::string uri = row->fUris.empty() ? "" : row->fUris[0];
		if (uri.empty() && update.nRows > 0 && !keptPlaceholder) {
			keptPlaceholder = true;
			continue;
		}
		if (update.snapshotUris.find(uri) == update.snapshotUris.end()) {
			fLists[update.tab]->RemoveRow(row);
			delete row;
		}
	}
}


void
DiscoverWindow::_ReorderSnapshotRows(const RowUpdateData& update)
{
	if (!update.snapshotMessage)
		return;
	for (int32 target = 0; target < (int32)update.snapshotOrder.size();
			target++) {
		DiscoverRow* row = _FindRow(update.tab, update.snapshotOrder[target]);
		if (!row)
			continue;
		int32 current = fLists[update.tab]->IndexOf(row);
		if (current == target)
			continue;
		bool selected = fLists[update.tab]->CurrentSelection() == row;
		fLists[update.tab]->RemoveRow(row);
		fLists[update.tab]->AddRow(row, target);
		if (selected)
			fLists[update.tab]->AddToSelection(row);
	}
}


void
DiscoverWindow::_FinishRowUpdate(BMessage*, const RowUpdateData& update)
{
	if (update.fromCache && !update.cacheLast)
		return;

	int32 selectedTab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
	if (update.tab == selectedTab)
		((DiscoverListView*)fLists[update.tab])->SetPlayingUri(
			fCurrentTrackUri);
	if (update.tab == TAB_AUDIOBOOKS)
		_RemoveAudiobookDuplicatesFromPodcasts();
	if (update.fromCache) {
		fLoaded[update.tab] = true;
		fCacheBacked[update.tab] = true;
		fPageLoading[update.tab] = false;
		fPageHasMore[update.tab] = false;
		fLoadTime[update.tab] = 0;
	} else {
		if (update.snapshotMessage) {
			fFreshSnapshot[update.tab] = true;
			fCacheBacked[update.tab] = false;
		}
		_ScheduleCacheSave();
	}
	_CheckLazyLoad();
}


void
DiscoverWindow::_ForwardPlayback(BMessage* message)
{
	const char* uri = message->GetString("uri", "");
	if (uri && SpotifyItemIsPlayable(SpotifyItemKindForUri(uri))) {
		if (fCurrentTrackUri != uri) {
			fCurrentTrackUri = uri;
			int32 tab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
			if (tab >= 0 && fLists[tab])
				((DiscoverListView*)fLists[tab])->SetPlayingUri(
					fCurrentTrackUri);
		}
	}
	be_app->PostMessage(message);
}


void
DiscoverWindow::_ForwardOpenRequest(BMessage* message)
{
	const char* uri = nullptr;
	const char* title = nullptr;
	message->FindString("uri", &uri);
	message->FindString("title", &title);
	if (!uri || !uri[0])
		return;

	BMessage forward('open');
	forward.AddString("uri", uri);
	forward.AddString("title", title ? title : "");
	be_app->PostMessage(&forward);
}


void
DiscoverWindow::_ApplyPlayingTrackUpdate(BMessage* message)
{
	const char* newUri = nullptr;
	message->FindString("trackUri", &newUri);
	if (!newUri)
		return;
	std::string uri = newUri;
	if (fCurrentTrackUri == uri)
		return;
	fCurrentTrackUri = uri;
	int32 tab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
	if (tab >= 0 && fLists[tab])
		((DiscoverListView*)fLists[tab])->SetPlayingUri(uri);
}


void
DiscoverWindow::_ShowDiscoverContextMenu(BMessage* message)
{
	const char* uri = nullptr;
	const char* title = nullptr;
	BPoint screen;
	if (message->FindString("uri", &uri) != B_OK)
		return;
	message->FindString("title", &title);
	if (message->FindPoint("screenPt", &screen) != B_OK)
		return;
	int32 sourceTab = -1;
	message->FindInt32("tab", &sourceTab);
	std::string uriString = uri;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();

	SpotifyItemKind kind = SpotifyItemKindForUri(uriString);
	if (SpotifyItemIsPlayable(kind)) {
		std::string context = sourceTab == TAB_SAVED_EPISODES
			? "spotify:saved-episodes" : "";
		bool saved = sourceTab == TAB_SAVED_EPISODES;
		auto state = fKnownLibraryStates.find(uriString);
		if (state != fKnownLibraryStates.end())
			saved = state->second;
		else
			_RequestPlayableLibraryState(uriString);
		::ShowPlayableItemContextMenu(uriString, context, screen,
			BMessenger(this), api, false, true, saved);
	} else if (kind == kSpotifyItemPlaylist) {
		_ShowPlaylistContextMenu(SpotifyItemIdForUri(uriString),
			message->GetBool("owned", false), screen);
	} else if (kind != kSpotifyItemUnknown) {
		_ShowBrowsableItemContextMenu(uriString, title ? title : "",
			sourceTab, screen);
	}
}


void
DiscoverWindow::_RequestPlayableLibraryState(const std::string& uri)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;
	int32 generation = ++fLibraryStateGenerations[uri];
	BMessenger self(this);
	api->Library().CheckLibraryItems({uri}, [self, uri, generation](bool ok,
			const nlohmann::json& data) {
		BMessage result(kMsgLibraryStateCached);
		result.AddString("uri", uri.c_str());
		result.AddInt32("generation", generation);
		result.AddBool("ok", ok && data.is_array() && !data.empty()
			&& data[0].is_boolean());
		result.AddBool("saved", ok && data.is_array() && !data.empty()
			&& data[0].is_boolean() && data[0].get<bool>());
		self.SendMessage(&result);
	});
}


void
DiscoverWindow::_ShowBrowsableItemContextMenu(const std::string& uri,
	const std::string& title, int32 sourceTab, BPoint screen)
{
	BPopUpMenu* menu = new BPopUpMenu("item", false, false);
	BMessage* openMsg = new BMessage('open');
	openMsg->AddString("uri", uri.c_str());
	openMsg->AddString("title", title.c_str());
	menu->AddItem(new BMenuItem(B_TRANSLATE("Open"), openMsg));

	BMessage* playMsg = new BMessage('play');
	playMsg->AddString("uri", uri.c_str());
	menu->AddItem(new BMenuItem(B_TRANSLATE("Play"), playMsg));

	_AddAlbumContextActions(menu, uri, sourceTab);
	_AddLibraryRemovalContextAction(menu, uri, sourceTab);

	BMenuItem* selected = menu->Go(screen, false, true);
	if (selected && selected->Message())
		PostMessage(selected->Message());
	delete menu;
}


void
DiscoverWindow::_AddAlbumContextActions(BPopUpMenu* menu,
	const std::string& uri, int32 sourceTab)
{
	if (SpotifyItemKindForUri(uri) != kSpotifyItemAlbum)
		return;
	menu->AddSeparatorItem();
	if (sourceTab == TAB_SAVED_ALBUMS) {
		BMessage* removeMsg = new BMessage('remA');
		removeMsg->AddString("uri", uri.c_str());
		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Remove from Saved Albums"), removeMsg));
	} else {
		BMessage* saveMsg = new BMessage('savA');
		saveMsg->AddString("uri", uri.c_str());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Save Album"), saveMsg));
	}
}


void
DiscoverWindow::_AddLibraryRemovalContextAction(BPopUpMenu* menu,
	const std::string& uri, int32 sourceTab)
{
	SpotifyItemKind kind = SpotifyItemKindForUri(uri);
	bool removable = (kind == kSpotifyItemShow && sourceTab == TAB_PODCASTS)
		|| (kind == kSpotifyItemArtist && sourceTab == TAB_FOLLOWED_ARTISTS)
		|| (kind == kSpotifyItemAudiobook && sourceTab == TAB_AUDIOBOOKS);
	if (!removable)
		return;
	menu->AddSeparatorItem();
	BMessage* removeMsg = new BMessage('remI');
	removeMsg->AddString("uri", uri.c_str());
	const char* label = kind == kSpotifyItemShow
		? B_TRANSLATE("Unsubscribe") : kind == kSpotifyItemArtist
		? B_TRANSLATE("Unfollow Artist") : B_TRANSLATE("Remove from Audiobooks");
	menu->AddItem(new BMenuItem(label, removeMsg));
}


void
DiscoverWindow::_ShowPlayableContextMenu(BMessage* message)
{
	App* app = dynamic_cast<App*>(be_app);
	ShowPlayableItemContextMenu(message->GetString("uri", ""),
		message->GetString("context_uri", ""),
		message->GetPoint("screen_point", BPoint()), BMessenger(this),
		app ? app->GetApi() : nullptr,
		message->GetBool("library_only", false), true,
		message->GetBool("saved", false));
}


void
DiscoverWindow::_ApplyLibraryStateCached(BMessage* message)
{
	std::string uri = message->GetString("uri", "");
	if (!message->GetBool("ok", false) || uri.empty()
			|| message->GetInt32("generation", -1)
				!= fLibraryStateGenerations[uri])
		return;
	fKnownLibraryStates[uri] = message->GetBool("saved", false);
}


void
DiscoverWindow::_HandleDiscoverDrop(BMessage* message)
{
	const char* uri = message->GetString("uri", "");
	if (!uri || !uri[0])
		uri = message->GetString("trackUri", "");
	if (!uri || !uri[0])
		uri = message->GetString("albumUri", "");
	if (!uri || !uri[0])
		return;
	int32 targetTab = message->GetInt32("tab", -1);
	std::string targetUri = message->GetString("targetUri", "");
	if (targetTab == TAB_PLAYLISTS && !targetUri.empty()
			&& _HandlePlaylistDrop(uri, targetUri,
				message->GetBool("targetWritable", false))) {
		return;
	}
	_HandleLibraryDrop(uri);
}


void
DiscoverWindow::_ApplyPlaylistDropResult(BMessage* message)
{
	if (message->GetBool("ok", false))
		return;
	const char* text = message->GetInt32("status", -1) == 403
		? B_TRANSLATE("This playlist cannot be modified.")
		: B_TRANSLATE("Spotify could not add this item to the playlist.");
	BAlert* alert = new BAlert("", text, B_TRANSLATE("OK"), nullptr, nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
}


void
DiscoverWindow::_ApplyLibraryStatusResult(BMessage* message)
{
	std::string uri = message->GetString("uri", "");
	if (!message->GetBool("ok", false)) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not check this item's library status."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return;
	}
	if (message->GetBool("saved", false)) {
		_SelectLibraryTarget(uri);
		PostLibraryChange("add", uri);
		return;
	}
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;
	BMessenger self(this);
	api->Library().SaveLibraryItems({uri}, [self, uri](bool ok,
			const nlohmann::json&) {
		BMessage result('dAdd');
		result.AddString("uri", uri.c_str());
		result.AddBool("ok", ok);
		self.SendMessage(&result);
	});
}


void
DiscoverWindow::_ApplyLibraryAddResult(BMessage* message)
{
	if (message->GetBool("ok", false)) {
		std::string uri = message->GetString("uri", "");
		_SelectLibraryTarget(uri);
		PostLibraryChange("add", uri);
	} else {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not add this item to your library."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
}


void
DiscoverWindow::_SaveAlbumFromMessage(BMessage* message)
{
	const char* uri = message->GetString("uri", "");
	std::string savedUri = uri ? uri : "";
	std::string albumId = SpotifyItemIdForUri(savedUri);
	if (SpotifyItemKindForUri(savedUri) != kSpotifyItemAlbum
			|| albumId.empty())
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	api->Library().SaveAlbum(albumId, [savedUri](bool ok,
			const nlohmann::json&) {
		if (ok)
			PostLibraryChange("add", savedUri);
	});
}


void
DiscoverWindow::_RemoveAlbumFromMessage(BMessage* message)
{
	const char* uri = message->GetString("uri", "");
	std::string removedUri = uri ? uri : "";
	std::string albumId = SpotifyItemIdForUri(removedUri);
	if (SpotifyItemKindForUri(removedUri) != kSpotifyItemAlbum
			|| albumId.empty())
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	api->Library().RemoveSavedAlbum(albumId, [removedUri](bool ok,
			const nlohmann::json&) {
		if (ok)
			PostLibraryChange("remove", removedUri);
	});
}


void
DiscoverWindow::_RemoveFollowedItem(BMessage* message)
{
	std::string uri = message->GetString("uri", "");
	SpotifyItemKind kind = SpotifyItemKindForUri(uri);
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api || uri.empty())
		return;
	BMessenger self(this);
	auto done = [self, uri](bool ok, const nlohmann::json&) {
		BMessage result('rmIR');
		result.AddBool("ok", ok);
		result.AddString("uri", uri.c_str());
		self.SendMessage(&result);
	};
	std::string id = SpotifyItemIdForUri(uri);
	if (kind == kSpotifyItemShow)
		api->Library().UnfollowShow(id, done);
	else if (kind == kSpotifyItemArtist)
		api->Library().UnfollowArtist(id, done);
	else if (kind == kSpotifyItemAudiobook)
		api->Library().RemoveSavedAudiobook(id, done);
}


void
DiscoverWindow::_ApplyRemoveFollowedItemResult(BMessage* message)
{
	if (message->GetBool("ok", false)) {
		PostLibraryChange("remove", message->GetString("uri", ""));
	} else {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not remove this item from your library."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
}


void
DiscoverWindow::_RemovePlayableFromLibrary(BMessage* message)
{
	const char* uri = message->GetString("trackUri", "");
	std::string removedUri = uri ? uri : "";
	SpotifyItemKind kind = SpotifyItemKindForUri(removedUri);
	if (!SpotifyItemIsPlayable(kind))
		return;
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;
	api->Library().RemoveLibraryItems({removedUri}, [removedUri](bool ok,
			const nlohmann::json&) {
		if (!ok)
			return;
		PostLibraryChange("remove", removedUri);
	});
}


void
DiscoverWindow::_PlayTrackFromMessage(BMessage* message)
{
	const char* trackUri = message->GetString("trackUri", "");
	if (!*trackUri)
		return;
	BMessage play('play');
	play.AddString("uri", trackUri);
	be_app->PostMessage(&play);
}


void
DiscoverWindow::_ShowNewPlaylistDialog()
{
	BMessage confirm('plNc');
	TextInputDialog* dialog = new TextInputDialog(
		B_TRANSLATE("New Playlist"), B_TRANSLATE("Name:"), "",
		BMessenger(this), confirm);
	dialog->Show();
}


void
DiscoverWindow::_CreatePlaylist(BMessage* message)
{
	const char* name = message->GetString("name", "");
	if (!*name)
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	BMessenger self(this);
	std::string requestedName = name;
	api->Playlists().CreatePlaylist(requestedName,
		[self, requestedName](bool ok, const nlohmann::json& data) {
		BMessage result('plCr');
		result.AddBool("ok", ok);
		nlohmann::json created = MutationResponseBody(data);
		if (ok && created.is_object()) {
			std::string id = JsonString(created, "id");
			std::string uri = JsonString(created, "uri");
			std::string createdName = JsonString(created, "name",
				requestedName.c_str());
			std::string owner = "Spotify";
			if (created.contains("owner") && created["owner"].is_object())
				owner = JsonString(created["owner"], "display_name", "Spotify");
			result.AddString("id", id.c_str());
			result.AddString("uri", uri.c_str());
			result.AddString("name", createdName.c_str());
			result.AddString("owner", owner.c_str());
		}
		self.SendMessage(&result);
	});
}


void
DiscoverWindow::_ApplyPlaylistCreateResult(BMessage* message)
{
	if (!message->GetBool("ok", false)) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not create the playlist."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return;
	}
	std::string id = message->GetString("id", "");
	if (id.empty()) {
		be_app->PostMessage(MSG_PLAYLISTS_CHANGED);
		return;
	}
	PostPlaylistChange("add", id, message->GetString("name", "Unknown"),
		message->GetString("owner", "Spotify"), true, true);
}


void
DiscoverWindow::_ShowRenamePlaylistDialog(BMessage* message)
{
	const char* id = message->GetString("id", "");
	if (!*id)
		return;
	std::string currentName;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (api) {
		for (const auto& playlist : api->Playlists().GetCachedPlaylists()) {
			if (playlist.first == id) {
				currentName = playlist.second;
				break;
			}
		}
	}
	BMessage confirm('plRc');
	confirm.AddString("id", id);
	TextInputDialog* dialog = new TextInputDialog(
		B_TRANSLATE("Rename Playlist"), B_TRANSLATE("Name:"),
		currentName.c_str(), BMessenger(this), confirm);
	dialog->Show();
}


void
DiscoverWindow::_RenamePlaylist(BMessage* message)
{
	const char* name = message->GetString("name", "");
	const char* id = message->GetString("id", "");
	if (!*name || !*id)
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	BMessenger self(this);
	std::string sid = id;
	std::string newName = name;
	std::string oldName;
	DiscoverRow* row = _FindPlaylistRow(PlaylistUri(sid));
	if (row && !row->fTitles.empty()) {
		oldName = row->fTitles[0];
		_UpdatePlaylistRow(row, newName, "", row->fWritable, row->fOwned);
	}
	api->Playlists().RenamePlaylist(sid, newName,
		[self, sid, oldName, newName](bool ok, const nlohmann::json&) {
		BMessage result('plRr');
		result.AddBool("ok", ok);
		result.AddString("id", sid.c_str());
		result.AddString("oldName", oldName.c_str());
		result.AddString("newName", newName.c_str());
		self.SendMessage(&result);
	});
}


void
DiscoverWindow::_ApplyPlaylistRenameResult(BMessage* message)
{
	std::string id = message->GetString("id", "");
	std::string oldName = message->GetString("oldName", "");
	std::string newName = message->GetString("newName", "");
	if (message->GetBool("ok", false)) {
		PostPlaylistChange("rename", id, newName);
	} else {
		DiscoverRow* row = _FindPlaylistRow(PlaylistUri(id));
		if (row && !row->fTitles.empty()
				&& row->fTitles[0] == newName && !oldName.empty()) {
			_UpdatePlaylistRow(row, oldName, "", row->fWritable,
				row->fOwned);
		}
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not rename the playlist."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
}


void
DiscoverWindow::_DeletePlaylist(BMessage* message)
{
	const char* id = message->GetString("id", "");
	bool owned = message->GetBool("owned", false);
	if (!*id)
		return;
	const char* label = owned
		? B_TRANSLATE("Delete Playlist") : B_TRANSLATE("Unfollow Playlist");
	const char* body = owned
		? B_TRANSLATE("Really delete this playlist? This cannot be undone.")
		: B_TRANSLATE("Unfollow this playlist?");
	BAlert* alert = new BAlert("", body, B_TRANSLATE("Cancel"), label,
		nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	if (alert->Go() != 1)
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	BMessenger self(this);
	std::string sid = id;
	std::string uri = PlaylistUri(sid);
	if (fPendingPlaylistRemovals.find(sid) != fPendingPlaylistRemovals.end())
		return;
	DiscoverRow* row = _FindPlaylistRow(uri);
	if (row) {
		PendingPlaylistRemoval pending = {
			row, fLists[TAB_PLAYLISTS]->IndexOf(row),
			fLists[TAB_PLAYLISTS]->CurrentSelection() == row
		};
		fLists[TAB_PLAYLISTS]->RemoveRow(row);
		fPendingPlaylistRemovals[sid] = pending;
	}
	api->Playlists().UnfollowPlaylist(sid, [self, sid](bool ok,
			const nlohmann::json&) {
		BMessage result('plDr');
		result.AddBool("ok", ok);
		result.AddString("id", sid.c_str());
		self.SendMessage(&result);
	});
}


void
DiscoverWindow::_ApplyPlaylistDeleteResult(BMessage* message)
{
	std::string id = message->GetString("id", "");
	auto pending = fPendingPlaylistRemovals.find(id);
	if (message->GetBool("ok", false)) {
		if (pending != fPendingPlaylistRemovals.end()) {
			delete pending->second.row;
			fPendingPlaylistRemovals.erase(pending);
		}
		PostPlaylistChange("remove", id);
	} else {
		if (pending != fPendingPlaylistRemovals.end()) {
			PendingPlaylistRemoval restore = pending->second;
			fPendingPlaylistRemovals.erase(pending);
			fLists[TAB_PLAYLISTS]->AddRow(restore.row, restore.index);
			if (restore.selected)
				fLists[TAB_PLAYLISTS]->AddToSelection(restore.row);
		}
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not remove the playlist."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
}


void
DiscoverWindow::_ApplyPlaylistsChanged(BMessage* message)
{
	const char* operation = nullptr;
	if (message->FindString("operation", &operation) == B_OK)
		_ApplyPlaylistChange(message);
	else
		ReloadPlaylists();
}


void
DiscoverWindow::_ApplyLibraryChanged(BMessage* message)
{
	const char* operation = nullptr;
	const char* uri = nullptr;
	if (message->FindString("operation", &operation) == B_OK
			&& message->FindString("uri", &uri) == B_OK) {
		fKnownLibraryStates[uri] = strcmp(operation, "add") == 0;
		fLibraryStateGenerations[uri]++;
		_ApplyLibraryChange(message);
	} else {
		int32 tab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
		if (tab == TAB_PLAYLISTS)
			ReloadPlaylists();
		else if (tab >= 0) {
			_InvalidateTabCache(tab);
			fLoaded[tab] = false;
			_LoadTab(tab);
		}
	}
}


void
DiscoverWindow::_ReloadTabFromMessage(BMessage* message)
{
	int32 tab = -1;
	if (message->FindInt32("tab", &tab) == B_OK)
		_ReloadTab(tab);
}


void
DiscoverWindow::_InitMenu()
{
	fMenuBar = new BMenuBar("MenuBar");

	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("New Playlist" B_UTF8_ELLIPSIS),
		new BMessage('plNw'), 'N'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Close Window"),
		new BMessage(B_QUIT_REQUESTED), 'W'));
	fMenuBar->AddItem(fileMenu);

	BMenu* viewMenu = new BMenu(B_TRANSLATE("View"));
	for (int i = 0; i < TAB_COUNT; i++) {
		BMessage* msg = new BMessage('togT');
		msg->AddInt32("tab", i);
		fTabMenuItems[i] = new BMenuItem(B_TRANSLATE(kTabDefs[i].label), msg);
		fTabMenuItems[i]->SetMarked(fTabVisible[i]);
		if (i == TAB_AUDIOBOOKS)
			fTabMenuItems[i]->SetEnabled(_AudiobooksEnabled());
		viewMenu->AddItem(fTabMenuItems[i]);
	}
	viewMenu->AddSeparatorItem();
	viewMenu->AddItem(new BMenuItem(B_TRANSLATE("Reset Tab Order"),
		new BMessage('tRst')));
	fMenuBar->AddItem(viewMenu);
}


void
DiscoverWindow::_InitLayout()
{
	fTabView = new DiscoverTabView();
	fTabView->SetBorder(B_NO_BORDER);
	_RebuildTabs();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.SetInsets(0)
			.Add(fTabView, 1)
		.End()
	.End();

	SetSizeLimits(300, 100000, 200, 100000);
}

static void
AddDiscoverCacheMessageFields(BMessage& message, int32 tab, int32 generation,
	const std::string& accountId, bool available, bool first, bool last)
{
	message.AddInt32("tab", tab);
	message.AddInt32("cols", (int32)kTabCols[tab].size());
	message.AddInt32("cache_generation", generation);
	message.AddString("account_id", accountId.c_str());
	message.AddBool("from_cache", true);
	message.AddBool("cache_available", available);
	message.AddBool("cache_first", first);
	message.AddBool("cache_last", last);
}

static void
SendDiscoverCacheUnavailable(BMessenger self, int32 tab, int32 generation,
	const std::string& accountId)
{
	BMessage response(kMsgCacheLoaded);
	AddDiscoverCacheMessageFields(response, tab, generation, accountId, false,
		true, true);
	self.SendMessage(&response);
}

static bool
IsReadableDiscoverCache(const nlohmann::json& cache,
	const std::string& accountId)
{
	return cache.is_object()
		&& cache.value("version", 0) == kDiscoverCacheVersion
		&& cache.value("account_id", "") == accountId
		&& cache.contains("tabs") && cache["tabs"].is_object();
}

static std::vector<std::string>
DiscoverCacheAudiobookIds(const nlohmann::json& cache)
{
	std::vector<std::string> audiobookIds;
	if (!cache.contains("audiobook_ids")
			|| !cache["audiobook_ids"].is_array()) {
		return audiobookIds;
	}
	for (const auto& id : cache["audiobook_ids"]) {
		if (id.is_string() && !id.get<std::string>().empty())
			audiobookIds.push_back(id.get<std::string>());
	}
	return audiobookIds;
}

static bool
CachedDiscoverRowArraysMatch(int32 tab, const nlohmann::json& row)
{
	if (!row.is_object() || !row.contains("values")
			|| !row.contains("uris") || !row.contains("titles")
			|| !row["values"].is_array() || !row["uris"].is_array()
			|| !row["titles"].is_array()
			|| row["values"].size() != kTabCols[tab].size()
			|| row["uris"].size() != kTabCols[tab].size()
			|| row["titles"].size() != kTabCols[tab].size()) {
		return false;
	}
	return true;
}

static bool
CachedDiscoverRowPrimaryUriMatches(int32 tab, const nlohmann::json& row)
{
	return !row["uris"].empty() && row["uris"][0].is_string()
		&& PrimaryUriMatchesTab(tab, row["uris"][0].get<std::string>());
}

static bool
CachedDiscoverRowColumnsAreStrings(int32 tab, const nlohmann::json& row)
{
	for (size_t column = 0; column < kTabCols[tab].size(); column++) {
		if (!row["values"][column].is_string()
				|| !row["uris"][column].is_string()
				|| !row["titles"][column].is_string()) {
			return false;
		}
	}
	return true;
}

static bool
IsValidCachedDiscoverRow(int32 tab, const nlohmann::json& row)
{
	return CachedDiscoverRowArraysMatch(tab, row)
		&& CachedDiscoverRowPrimaryUriMatches(tab, row)
		&& CachedDiscoverRowColumnsAreStrings(tab, row);
}

static RowData
CachedDiscoverRowFromJson(const nlohmann::json& row)
{
	RowData cached;
	for (const auto& value : row["values"])
		cached.vals.push_back(value.get<std::string>());
	for (const auto& value : row["uris"])
		cached.uris.push_back(value.get<std::string>());
	for (const auto& value : row["titles"])
		cached.ttls.push_back(value.get<std::string>());
	cached.writable = !row.contains("writable")
		|| !row["writable"].is_boolean() || row["writable"].get<bool>();
	cached.owned = row.contains("owned") && row["owned"].is_boolean()
		&& row["owned"].get<bool>();
	return cached;
}

static std::vector<RowData>
CachedDiscoverRowsFromJson(int32 tab, const nlohmann::json& rows)
{
	std::vector<RowData> cachedRows;
	for (const auto& row : rows) {
		if ((int32)cachedRows.size() >= kMaxDiscoverCachedRowsPerTab)
			break;
		if (IsValidCachedDiscoverRow(tab, row))
			cachedRows.push_back(CachedDiscoverRowFromJson(row));
	}
	return cachedRows;
}

static void
AddDiscoverCacheRows(BMessage& message,
	const std::vector<RowData>& cachedRows, size_t offset, size_t end)
{
	for (size_t index = offset; index < end; index++) {
		const RowData& row = cachedRows[index];
		for (const std::string& value : row.vals)
			message.AddString("v", value.c_str());
		for (const std::string& value : row.uris)
			message.AddString("u", value.c_str());
		for (const std::string& value : row.ttls)
			message.AddString("t", value.c_str());
		message.AddBool("writable", row.writable);
		message.AddBool("owned", row.owned);
	}
}

static void
AddDiscoverAudiobookSnapshot(BMessage& message,
	const std::vector<std::string>& audiobookIds)
{
	message.AddBool("audiobook_ids_snapshot", true);
	for (const std::string& id : audiobookIds)
		message.AddString("audiobook_id", id.c_str());
}

static bool
SendDiscoverCacheBatches(BMessenger self, int32 tab, int32 generation,
	const std::string& accountId, const std::vector<RowData>& cachedRows,
	const std::vector<std::string>& audiobookIds, bool hasAudiobookIds)
{
	size_t offset = 0;
	bool firstBatch = true;
	do {
		size_t end = std::min(offset + (size_t)kDiscoverCacheBatchRows,
			cachedRows.size());
		BMessage rows(kMsgCacheLoaded);
		AddDiscoverCacheMessageFields(rows, tab, generation, accountId, true,
			firstBatch, end >= cachedRows.size());
		if (firstBatch && hasAudiobookIds)
			AddDiscoverAudiobookSnapshot(rows, audiobookIds);
		AddDiscoverCacheRows(rows, cachedRows, offset, end);
		if (self.SendMessage(&rows) != B_OK)
			return false;
		firstBatch = false;
		offset = end;
	} while (offset < cachedRows.size());
	return true;
}

static void
LoadPersistentDiscoverCache(BMessenger self, const std::string& path,
	const std::string& accountId, int32 generation, int32 tab)
{
	nlohmann::json cache;
	try {
		if (!ReadDiscoverCacheFile(path, cache)
				|| !IsReadableDiscoverCache(cache, accountId)) {
			SendDiscoverCacheUnavailable(self, tab, generation, accountId);
			return;
		}
		auto found = cache["tabs"].find(kTabDefs[tab].id);
		if (found == cache["tabs"].end() || !found->is_array()) {
			SendDiscoverCacheUnavailable(self, tab, generation, accountId);
			return;
		}
		std::vector<RowData> cachedRows = CachedDiscoverRowsFromJson(tab,
			*found);
		SendDiscoverCacheBatches(self, tab, generation, accountId, cachedRows,
			DiscoverCacheAudiobookIds(cache), cache.contains("audiobook_ids"));
	} catch (...) {
		SendDiscoverCacheUnavailable(self, tab, generation, accountId);
	}
}


void
DiscoverWindow::_LoadPersistentCache(int32 tab)
{
	if (tab < 0 || tab >= TAB_COUNT || !fLists[tab]
			|| !_IsTabEffectivelyVisible(tab) || fCacheAccountId.empty()
			|| fCacheLoadPending[tab] || fCacheBacked[tab]
			|| fFreshSnapshot[tab])
		return;
	std::string accountId = fCacheAccountId;
	std::string path = DiscoverCachePath(accountId, false);
	if (path.empty())
		return;
	fCacheLoadPending[tab] = true;
	int32 generation = ++fCacheLoadGeneration[tab];
	BMessenger self(this);
	std::thread([self, path, accountId, generation, tab]() {
		LoadPersistentDiscoverCache(self, path, accountId, generation, tab);
	}).detach();
}


void
DiscoverWindow::_ScheduleCacheSave()
{
	if (fCacheAccountId.empty()) {
		HaifySettings settings = SettingsController::Load();
		if (settings.spotifyAccountId.empty())
			return;
		fCacheAccountId = settings.spotifyAccountId;
	}
	delete fCacheSaveRunner;
	BMessage save(kMsgSaveCache);
	fCacheSaveRunner = new BMessageRunner(BMessenger(this), &save,
		750000LL, 1);
}


void
DiscoverWindow::_WriteCacheNow()
{
	if (fCacheAccountId.empty())
		return;
	std::string path = DiscoverCachePath(fCacheAccountId, true);
	if (path.empty())
		return;
	WriteDiscoverCacheAsync(path, BuildDiscoverCachePayload(fCacheAccountId,
		fAudiobookIds, fAudiobookIdsKnown, fLists, fCacheBacked,
		fFreshSnapshot));
}


void
DiscoverWindow::LoadData()
{
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	HaifySettings settings = SettingsController::Load();
	fCacheAccountId = settings.spotifyAccountId;
	fPlaylistSyncGeneration++;
	fKnownLibraryStates.clear();
	fLibraryStateGenerations.clear();
	fAudiobookIds.clear();
	fAudiobookIdsKnown = false;
	for (int i = 0; i < TAB_COUNT; i++) {
		if (fLists[i]) fLists[i]->Clear();
		fLoaded[i] = false;
		fCacheBacked[i] = false;
		fFreshSnapshot[i] = false;
		fPageLoading[i] = false;
		fPageHasMore[i] = false;
		fPageOffset[i] = 0;
		fPageCursor[i].clear();
		fTabLoadGeneration[i]++;
		fCacheLoadGeneration[i]++;
		fCacheLoadPending[i] = false;
	}
	int32 logical = _LogicalTab(fTabView ? fTabView->Selection() : 0);
	if (logical >= 0) {
		_LoadPersistentCache(logical);
		_LoadTab(logical);
	}
}


void
DiscoverWindow::_ReloadTab(int32 tab)
{
	if (tab < 0 || tab >= TAB_COUNT)
		return;
	if (!fTabVisible[tab] || !fLists[tab]) {
		fLoaded[tab] = false;
		return;
	}
	if (tab == TAB_PLAYLISTS) {
		ReloadPlaylists();
		return;
	}
	_InvalidateTabCache(tab);
	fLoaded[tab] = false;
	_LoadTab(tab);
}


DiscoverRow*
DiscoverWindow::_FindRow(int32 tab, const std::string& uri) const
{
	if (tab < 0 || tab >= TAB_COUNT)
		return nullptr;
	BColumnListView* list = fLists[tab];
	if (!list || uri.empty())
		return nullptr;
	for (int32 i = 0; i < list->CountRows(); i++) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(list->RowAt(i));
		if (row && !row->fUris.empty() && row->fUris[0] == uri)
			return row;
	}
	return nullptr;
}


DiscoverRow*
DiscoverWindow::_FindPlaylistRow(const std::string& uri) const
{
	return _FindRow(TAB_PLAYLISTS, uri);
}


void
DiscoverWindow::_ApplyLibraryChange(BMessage* message)
{
	if (!message)
		return;
	std::string operation = message->GetString("operation", "");
	std::string uri = message->GetString("uri", "");
	int32 tab = _LibraryChangeTabForUri(uri);
	if (tab < 0 || !fLists[tab])
		return;
	int32 generation = ++fLibraryChangeGenerations[uri];
	bool refreshPodcasts = false;
	if (tab == TAB_AUDIOBOOKS)
		_UpdateAudiobookIdsForLibraryChange(operation, uri, refreshPodcasts);
	if (operation == "remove")
		_ApplyLibraryRemoval(tab, uri);
	else if (operation == "add")
		_ApplyLibraryAddition(tab, uri, generation);
	_RefreshPodcastsAfterLibraryChange(refreshPodcasts);
}


int32
DiscoverWindow::_LibraryChangeTabForUri(const std::string& uri) const
{
	switch (SpotifyItemKindForUri(uri)) {
		case kSpotifyItemAlbum:
			return TAB_SAVED_ALBUMS;
		case kSpotifyItemShow:
			return TAB_PODCASTS;
		case kSpotifyItemArtist:
			return TAB_FOLLOWED_ARTISTS;
		case kSpotifyItemEpisode:
			return TAB_SAVED_EPISODES;
		case kSpotifyItemAudiobook:
			return TAB_AUDIOBOOKS;
		default:
			return -1;
	}
}


void
DiscoverWindow::_UpdateAudiobookIdsForLibraryChange(
	const std::string& operation, const std::string& uri, bool& refreshPodcasts)
{
	std::string id = SpotifyItemIdForUri(uri);
	if (id.empty())
		return;
	fAudiobookIdsKnown = true;
	if (operation == "add") {
		fAudiobookIds.insert(id);
		_RemoveAudiobookDuplicatesFromPodcasts();
	} else if (operation == "remove") {
		refreshPodcasts = fAudiobookIds.erase(id) > 0;
	}
	_ScheduleCacheSave();
}


void
DiscoverWindow::_ApplyLibraryRemoval(int32 tab, const std::string& uri)
{
	DiscoverRow* row = _FindRow(tab, uri);
	if (row) {
		fLists[tab]->RemoveRow(row);
		delete row;
		_ScheduleCacheSave();
	}
	if (fLoaded[tab])
		fLoadTime[tab] = system_time();
}


void
DiscoverWindow::_ApplyLibraryAddition(int32 tab, const std::string& uri,
	int32 generation)
{
	if (!fLoaded[tab] || _FindRow(tab, uri))
		return;
	// Resolve just the newly saved object. This preserves the current list,
	// selection and scroll position instead of rebuilding the whole tab.
	_ResolveLibraryAddition(tab, uri, generation);
}


void
DiscoverWindow::_RefreshPodcastsAfterLibraryChange(bool refreshPodcasts)
{
	if (refreshPodcasts && fLoaded[TAB_PODCASTS]) {
		_InvalidateTabCache(TAB_PODCASTS);
		fLoaded[TAB_PODCASTS] = false;
		_LoadTab(TAB_PODCASTS);
	}
}


void
DiscoverWindow::_ApplyAudiobookIdSnapshot(BMessage* message)
{
	if (!message)
		return;
	int32 generation = -1;
	if (message->FindInt32("load_generation", &generation) == B_OK
			&& generation != fTabLoadGeneration[TAB_PODCASTS]) {
		return;
	}
	std::set<std::string> ids;
	const char* value = nullptr;
	for (int32 index = 0;
			message->FindString("audiobook_id", index, &value) == B_OK; index++) {
		if (value && value[0])
			ids.insert(value);
	}
	fAudiobookIds.swap(ids);
	fAudiobookIdsKnown = true;
	_RemoveAudiobookDuplicatesFromPodcasts();
	_ScheduleCacheSave();
}


void
DiscoverWindow::_RemoveAudiobookDuplicatesFromPodcasts()
{
	BColumnListView* list = fLists[TAB_PODCASTS];
	if (!list)
		return;
	bool changed = false;
	for (int32 index = list->CountRows() - 1; index >= 0; index--) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(list->RowAt(index));
		if (!row || row->fUris.empty())
			continue;
		std::string id = SpotifyItemIdForUri(row->fUris[0]);
		if (SpotifyEffectiveItemKind(kSpotifyItemShow, id, fAudiobookIds)
				!= kSpotifyItemAudiobook) {
			continue;
		}
		list->RemoveRow(row);
		delete row;
		changed = true;
	}
	if (changed)
		_ScheduleCacheSave();
}

static bool
BuildResolvedAlbumRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, RowData& row)
{
	std::string artist = "Unknown";
	std::string artistUri;
	if (item.contains("artists") && item["artists"].is_array()
			&& !item["artists"].empty() && item["artists"][0].is_object()) {
		artist = JsonString(item["artists"][0], "name", "Unknown");
		artistUri = JsonString(item["artists"][0], "uri");
	}
	row.vals = {name, artist};
	row.uris = {uri, artistUri};
	row.ttls = {name, artist};
	return true;
}

static bool
BuildResolvedPodcastRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, RowData& row)
{
	row.vals = {name, JsonString(item, "publisher", "Unknown")};
	row.uris = {uri, ""};
	row.ttls = {name, ""};
	return true;
}

static bool
BuildResolvedArtistRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, RowData& row)
{
	std::string genre = "Artist";
	if (item.contains("genres") && item["genres"].is_array()
			&& !item["genres"].empty() && item["genres"][0].is_string()) {
		genre = item["genres"][0].get<std::string>();
	}
	row.vals = {name, genre};
	row.uris = {uri, ""};
	row.ttls = {name, ""};
	return true;
}

static bool
BuildResolvedEpisodeRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, bool showProgress, RowData& row)
{
	if (name.empty())
		return false;
	std::string showName;
	std::string showUri;
	if (item.contains("show") && item["show"].is_object()) {
		showName = JsonString(item["show"], "name");
		std::string showId = JsonString(item["show"], "id");
		showUri = showId.empty() ? JsonString(item["show"], "uri")
			: SpotifyUriForItemKind(kSpotifyItemShow, showId);
	}
	std::string progress;
	if (showProgress && item.contains("resume_point")
			&& item["resume_point"].is_object()) {
		const auto& resume = item["resume_point"];
		progress = JsonBool(resume, "fully_played")
			? B_TRANSLATE("Done")
			: DurationText(JsonInt32(resume, "resume_position_ms"));
	}
	row.vals = {name, showName, JsonString(item, "release_date"),
		DurationText(JsonInt32(item, "duration_ms")), progress};
	row.uris = {uri, showUri, "", "", ""};
	row.ttls = {name, showName, "", "", ""};
	return true;
}

static bool
BuildResolvedAudiobookRow(const std::string& uri, const nlohmann::json& item,
	const std::string& name, RowData& row)
{
	std::string author;
	if (item.contains("authors") && item["authors"].is_array()
			&& !item["authors"].empty() && item["authors"][0].is_object()) {
		author = JsonString(item["authors"][0], "name");
	}
	row.vals = {name, author};
	row.uris = {uri, ""};
	row.ttls = {name, ""};
	return true;
}

static bool
BuildResolvedLibraryRow(int32 tab, const std::string& uri,
	const nlohmann::json& item, bool showProgress, RowData& row)
{
	std::string name = JsonString(item, "name");
	if (tab != TAB_SAVED_EPISODES && name.empty())
		name = "Unknown";
	if (tab == TAB_SAVED_ALBUMS)
		return BuildResolvedAlbumRow(uri, item, name, row);
	if (tab == TAB_PODCASTS)
		return BuildResolvedPodcastRow(uri, item, name, row);
	if (tab == TAB_FOLLOWED_ARTISTS)
		return BuildResolvedArtistRow(uri, item, name, row);
	if (tab == TAB_SAVED_EPISODES)
		return BuildResolvedEpisodeRow(uri, item, name, showProgress, row);
	if (tab == TAB_AUDIOBOOKS)
		return BuildResolvedAudiobookRow(uri, item, name, row);
	return false;
}

static void
AddResolvedLibraryRowToMessage(BMessage& result, const RowData& row)
{
	for (const std::string& value : row.vals)
		result.AddString("v", value.c_str());
	for (const std::string& value : row.uris)
		result.AddString("u", value.c_str());
	for (const std::string& value : row.ttls)
		result.AddString("t", value.c_str());
}


void
DiscoverWindow::_ResolveLibraryAddition(int32 tab, const std::string& uri,
	int32 generation)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api || uri.empty())
		return;

	size_t separator = uri.rfind(':');
	if (separator == std::string::npos || separator + 1 >= uri.size())
		return;
	std::string id = uri.substr(separator + 1);
	BMessenger self(this);
	HaifySettings accountSettings = SettingsController::Load();
	bool showProgress = accountSettings.grantedScopes.find(
		"user-read-playback-position") != std::string::npos;
	JsonCallback done = [self, tab, uri, generation, showProgress](bool ok,
			const nlohmann::json& item) {
		BMessage result('lAdd');
		result.AddInt32("tab", tab);
		result.AddInt32("generation", generation);
		result.AddString("uri", uri.c_str());
		result.AddBool("ok", ok && item.is_object());
		if (!ok || !item.is_object()) {
			self.SendMessage(&result);
			return;
		}

		RowData row;
		if (!BuildResolvedLibraryRow(tab, uri, item, showProgress, row))
			result.ReplaceBool("ok", false);
		else
			AddResolvedLibraryRowToMessage(result, row);
		self.SendMessage(&result);
	};

	if (tab == TAB_SAVED_ALBUMS)
		api->Content().GetAlbum(id, done);
	else if (tab == TAB_PODCASTS)
		api->Content().GetShow(id, done);
	else if (tab == TAB_FOLLOWED_ARTISTS)
		api->Artists().GetArtist(id, done);
	else if (tab == TAB_SAVED_EPISODES)
		api->Content().GetEpisode(id, done);
	else if (tab == TAB_AUDIOBOOKS)
		api->Content().GetAudiobook(id, done);
}


void
DiscoverWindow::_ApplyResolvedLibraryAddition(BMessage* message)
{
	if (!message || !message->GetBool("ok", false))
		return;
	int32 tab = message->GetInt32("tab", -1);
	int32 generation = message->GetInt32("generation", -1);
	std::string uri = message->GetString("uri", "");
	if (!_CanApplyResolvedLibraryAddition(tab, uri, generation))
		return;

	std::vector<std::string> values;
	std::vector<std::string> uris;
	std::vector<std::string> titles;
	const char* value = nullptr;
	for (int32 i = 0; message->FindString("v", i, &value) == B_OK; i++)
		values.push_back(value ? value : "");
	for (int32 i = 0; message->FindString("u", i, &value) == B_OK; i++)
		uris.push_back(value ? value : "");
	for (int32 i = 0; message->FindString("t", i, &value) == B_OK; i++)
		titles.push_back(value ? value : "");
	if (values.size() != kTabCols[tab].size()
			|| uris.size() != values.size() || titles.size() != values.size()) {
		return;
	}

	_RemoveEmptyRows(tab);
	fLists[tab]->AddRow(new DiscoverRow(values, uris, titles), 0);
	fLoadTime[tab] = system_time();
	_ScheduleCacheSave();
}


bool
DiscoverWindow::_CanApplyResolvedLibraryAddition(int32 logicalTab,
	const std::string& uri, int32 generation) const
{
	return logicalTab >= 0 && logicalTab < TAB_COUNT && fLists[logicalTab]
		&& fLoaded[logicalTab]
		&& fLibraryChangeGenerations.find(uri) != fLibraryChangeGenerations.end()
		&& fLibraryChangeGenerations.at(uri) == generation
		&& !_FindRow(logicalTab, uri);
}


void
DiscoverWindow::_RemoveEmptyRows(int32 logicalTab)
{
	for (int32 i = fLists[logicalTab]->CountRows() - 1; i >= 0; i--) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(
			fLists[logicalTab]->RowAt(i));
		if (row && (row->fUris.empty() || row->fUris[0].empty())) {
			fLists[logicalTab]->RemoveRow(row);
			delete row;
		}
	}
}


void
DiscoverWindow::_UpdatePlaylistRow(DiscoverRow* row, const std::string& name,
	const std::string& owner, bool writable, bool owned)
{
	if (!row || !fLists[TAB_PLAYLISTS])
		return;

	if (!name.empty()) {
		if (BoldStringField* field = dynamic_cast<BoldStringField*>(
				row->GetField(0)))
			field->SetString(name.c_str());
		if (row->fTitles.empty())
			row->fTitles.push_back(name);
		else
			row->fTitles[0] = name;
	}
	if (!owner.empty()) {
		if (BoldStringField* field = dynamic_cast<BoldStringField*>(
				row->GetField(1)))
			field->SetString(owner.c_str());
	}
	row->fWritable = writable;
	row->fOwned = owned;
	for (int32 column = 0; column < fLists[TAB_PLAYLISTS]->CountColumns();
			column++) {
		if (BoldStringField* field = dynamic_cast<BoldStringField*>(
				row->GetField(column)))
			field->fEnabled = writable;
	}
	fLists[TAB_PLAYLISTS]->UpdateRow(row);
}


void
DiscoverWindow::_ApplyPlaylistChange(BMessage* message)
{
	if (!message || !fLists[TAB_PLAYLISTS] || !fLoaded[TAB_PLAYLISTS])
		return;

	std::string operation = message->GetString("operation", "");
	std::string id = message->GetString("id", "");
	std::string uri = message->GetString("uri", "");
	if (uri.empty())
		uri = PlaylistUri(id);
	if (uri.empty() || uri == SpotifyItemUriPrefix(kSpotifyItemPlaylist))
		return;
	fPlaylistSyncGeneration++;
	fLoadTime[TAB_PLAYLISTS] = system_time();

	DiscoverRow* row = _FindPlaylistRow(uri);
	if (operation == "remove") {
		_RemovePlaylistRow(row);
		return;
	}

	std::string name = message->GetString("name", "");
	std::string owner = message->GetString("owner", "");
	bool writable = message->GetBool("writable", true);
	bool owned = message->GetBool("owned", row ? row->fOwned : false);
	if (operation == "rename") {
		if (row)
			_RenamePlaylistRow(row, name);
		else
			ReloadPlaylists();
		return;
	}
	if (operation != "add" || name.empty())
		return;

	_AddOrUpdatePlaylistRow(row, uri, name, owner, writable, owned);
}


void
DiscoverWindow::_RemovePlaylistRow(DiscoverRow* row)
{
	if (!row)
		return;
	fLists[TAB_PLAYLISTS]->RemoveRow(row);
	delete row;
	_ScheduleCacheSave();
}


void
DiscoverWindow::_RenamePlaylistRow(DiscoverRow* row, const std::string& name)
{
	_UpdatePlaylistRow(row, name, "", row->fWritable, row->fOwned);
	_ScheduleCacheSave();
}


void
DiscoverWindow::_AddOrUpdatePlaylistRow(DiscoverRow* row,
	const std::string& uri, const std::string& name,
	const std::string& owner, bool writable, bool owned)
{
	if (row) {
		_UpdatePlaylistRow(row, name, owner, writable, owned);
		_ScheduleCacheSave();
		return;
	}
	std::vector<std::string> values = {name,
		owner.empty() ? "Spotify" : owner};
	DiscoverRow* added = new DiscoverRow(values, {uri, ""}, {name, ""},
		writable, owned);
	int32 index = fLists[TAB_PLAYLISTS]->CountRows() > 0 ? 1 : 0;
	fLists[TAB_PLAYLISTS]->AddRow(added, index);
	_ScheduleCacheSave();
}


void
DiscoverWindow::_ApplyPlaylistSnapshot(BMessage* message)
{
	if (!_CanApplyPlaylistSnapshot(message))
		return;

	std::set<std::string> serverUris;
	for (int32 i = 0;; i++) {
		if (!_ApplyPlaylistSnapshotItem(message, i, serverUris))
			break;
	}

	_RemoveMissingPlaylistSnapshotRows(serverUris);
	fLoadTime[TAB_PLAYLISTS] = system_time();
	fFreshSnapshot[TAB_PLAYLISTS] = true;
	fCacheBacked[TAB_PLAYLISTS] = false;
	_ScheduleCacheSave();
}


bool
DiscoverWindow::_CanApplyPlaylistSnapshot(BMessage* message) const
{
	if (!message || !fLists[TAB_PLAYLISTS] || !fLoaded[TAB_PLAYLISTS])
		return false;
	int32 generation = -1;
	return message->FindInt32("generation", &generation) == B_OK
		&& generation == fPlaylistSyncGeneration;
}


bool
DiscoverWindow::_ApplyPlaylistSnapshotItem(BMessage* message, int32 index,
	std::set<std::string>& serverUris)
{
	const char* uri = nullptr;
	if (message->FindString("uri", index, &uri) != B_OK)
		return false;
	const char* name = "Unknown";
	const char* owner = "Spotify";
	message->FindString("name", index, &name);
	message->FindString("owner", index, &owner);
	bool writable = true;
	message->FindBool("writable", index, &writable);
	bool owned = false;
	message->FindBool("owned", index, &owned);

	std::string playlistUri = uri ? uri : "";
	if (playlistUri.empty())
		return true;
	std::string id = SpotifyItemKindForUri(playlistUri)
		== kSpotifyItemPlaylist ? SpotifyItemIdForUri(playlistUri) : "";
	serverUris.insert(playlistUri);
	if (!id.empty() && fPendingPlaylistRemovals.find(id)
			!= fPendingPlaylistRemovals.end())
		return true;

	DiscoverRow* row = _FindPlaylistRow(playlistUri);
	if (row) {
		_UpdatePlaylistRow(row, name, owner, writable, owned);
	} else {
		fLists[TAB_PLAYLISTS]->AddRow(new DiscoverRow(
			{name, owner}, {playlistUri, ""}, {name, ""}, writable, owned));
	}
	return true;
}


void
DiscoverWindow::_RemoveMissingPlaylistSnapshotRows(
	const std::set<std::string>& serverUris)
{
	for (int32 i = fLists[TAB_PLAYLISTS]->CountRows() - 1; i >= 0; i--) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(
			fLists[TAB_PLAYLISTS]->RowAt(i));
		if (!row || row->fUris.empty()
				|| row->fUris[0] == "spotify:collection")
			continue;
		if (serverUris.find(row->fUris[0]) == serverUris.end()) {
			fLists[TAB_PLAYLISTS]->RemoveRow(row);
			delete row;
		}
	}
}


void
DiscoverWindow::ReloadPlaylists()
{
	if (!fLists[TAB_PLAYLISTS] || !fLoaded[TAB_PLAYLISTS])
		return;
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;

	HaifySettings settings = SettingsController::Load();
	std::string accountId = settings.spotifyAccountId;
	int32 generation = ++fPlaylistSyncGeneration;
	BMessenger self(this);
	api->Playlists().GetPlaylists([self, accountId, generation](bool ok,
			const nlohmann::json& data) {
		if (!ok || !data.contains("items") || !data["items"].is_array())
			return;
		BMessage snapshot('pSyn');
		snapshot.AddInt32("generation", generation);
		for (const auto& item : data["items"]) {
			if (!item.is_object())
				continue;
			std::string uri = JsonString(item, "uri");
			if (uri.empty())
				continue;
			std::string owner = "Spotify";
			std::string ownerAccountId;
			std::string ownerLegacyId;
			if (item.contains("owner") && item["owner"].is_object()) {
				owner = JsonString(item["owner"], "display_name", "Spotify");
				ownerAccountId = JsonString(item["owner"], "account_id");
				ownerLegacyId = JsonString(item["owner"], "id");
			}
			snapshot.AddString("uri", uri.c_str());
			snapshot.AddString("name", JsonString(item, "name", "Unknown").c_str());
			snapshot.AddString("owner", owner.c_str());
			bool owned = !accountId.empty()
				&& (ownerAccountId == accountId || ownerLegacyId == accountId);
			snapshot.AddBool("owned", owned);
			snapshot.AddBool("writable", SpotifyPlaylistIsWritable(
				JsonBool(item, "collaborative"), ownerAccountId,
				ownerLegacyId, accountId));
		}
		self.SendMessage(&snapshot);
	});
}


void
DiscoverWindow::_InvalidateTabCache(int32 tab)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;
	switch (tab) {
		case TAB_TOP_TRACKS: api->Content().InvalidateTopItems("tracks"); break;
		case TAB_TOP_ARTISTS: api->Content().InvalidateTopItems("artists"); break;
		case TAB_NEW_RELEASES: api->Content().InvalidateNewReleases(); break;
		case TAB_SAVED_ALBUMS: api->Library().InvalidateSavedAlbums(); break;
		case TAB_PODCASTS: api->Library().InvalidateSavedShows(); break;
		case TAB_FOLLOWED_ARTISTS:
			api->Library().InvalidateFollowedArtists();
			break;
		case TAB_SAVED_EPISODES:
			api->Library().InvalidateSavedEpisodes();
			break;
		case TAB_AUDIOBOOKS: api->Library().InvalidateSavedAudiobooks(); break;
		default: break;
	}
}


void
DiscoverWindow::_CheckLazyLoad()
{
	int32 tab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
	if (tab != TAB_FOLLOWED_ARTISTS && tab != TAB_SAVED_EPISODES
			&& tab != TAB_AUDIOBOOKS)
		return;
	if (!fLoaded[tab] || fPageLoading[tab] || !fPageHasMore[tab]
			|| !fLists[tab])
		return;
	BScrollBar* scroll = nullptr;
	if (BView* scrollView = fLists[tab]->ScrollView())
		scroll = scrollView->ScrollBar(B_VERTICAL);
	if (!scroll)
		scroll = fLists[tab]->ScrollBar(B_VERTICAL);
	if (!scroll)
		return;
	float minimum = 0.0f;
	float maximum = 0.0f;
	scroll->GetRange(&minimum, &maximum);
	if (maximum <= 0.0f || scroll->Value() >= maximum - 220.0f)
		_LoadTab(tab, true);
}


static RowData
TrackLikeDiscoverRow(const nlohmann::json& item)
{
	std::string name = item.value("name", "Unknown");
	std::string artist = "Unknown";
	std::string artistUri;
	if (item.contains("artists") && item["artists"].is_array()
			&& !item["artists"].empty()) {
		artist = item["artists"][0].value("name", "Unknown");
		artistUri = item["artists"][0].value("uri", "");
	}
	return {{name, artist}, {item.value("uri", ""), artistUri},
		{name, artist}};
}

static std::vector<RowData>
TopTrackRows(const nlohmann::json& data)
{
	std::vector<RowData> rows;
	if (!data.contains("items"))
		return rows;
	for (const auto& item : data["items"]) {
		if (item.is_object())
			rows.push_back(TrackLikeDiscoverRow(item));
	}
	return rows;
}

static std::vector<RowData>
TopArtistRows(const nlohmann::json& data)
{
	std::vector<RowData> rows;
	if (!data.contains("items"))
		return rows;
	for (const auto& item : data["items"]) {
		if (!item.is_object())
			continue;
		std::string name = item.value("name", "Unknown");
		std::string genre = "Artist";
		if (item.contains("genres") && item["genres"].is_array()
				&& !item["genres"].empty())
			genre = item["genres"][0].get<std::string>();
		rows.push_back({{name, genre}, {item.value("uri", ""), ""},
			{name, ""}});
	}
	return rows;
}

static std::vector<RowData>
NewReleaseRows(const nlohmann::json& data)
{
	std::vector<RowData> rows;
	if (!data.contains("albums") || !data["albums"].contains("items"))
		return rows;
	for (const auto& item : data["albums"]["items"]) {
		if (item.is_object())
			rows.push_back(TrackLikeDiscoverRow(item));
	}
	return rows;
}

static std::vector<RowData>
SavedAlbumRows(const nlohmann::json& data)
{
	std::vector<RowData> rows;
	if (!data.contains("items"))
		return rows;
	for (const auto& item : data["items"]) {
		if (item.contains("album") && item["album"].is_object())
			rows.push_back(TrackLikeDiscoverRow(item["album"]));
	}
	return rows;
}

static void
SendDiscoverTabRows(BMessenger messenger, int32 tab, bool snapshot,
	int32 loadGeneration, const std::vector<RowData>& rows)
{
	BMessage* msg = new BMessage('uRow');
	msg->AddInt32("tab", tab);
	msg->AddInt32("load_generation", loadGeneration);
	msg->AddInt32("cols", rows.empty()
		? (int32)kTabCols[tab].size() : (int32)rows[0].vals.size());
	msg->AddBool("snapshot", snapshot);
	for (const RowData& row : rows) {
		for (const std::string& value : row.vals)
			msg->AddString("v", value.c_str());
		for (const std::string& uri : row.uris)
			msg->AddString("u", uri.c_str());
		for (const std::string& title : row.ttls)
			msg->AddString("t", title.c_str());
		msg->AddBool("writable", row.writable);
		msg->AddBool("owned", row.owned);
	}
	messenger.SendMessage(msg);
	delete msg;
}

static void
SendDiscoverPageDone(BMessenger messenger, int32 tab, int32 loadGeneration,
	const std::string& nextCursor, bool hasMore)
{
	BMessage done(kMsgPageDone);
	done.AddInt32("tab", tab);
	done.AddInt32("load_generation", loadGeneration);
	if (!nextCursor.empty())
		done.AddString("next_cursor", nextCursor.c_str());
	done.AddBool("has_more", hasMore);
	messenger.SendMessage(&done);
}

static std::set<std::string>
AudiobookIdsFromResponse(bool ok, const nlohmann::json& books,
	const std::set<std::string>& fallback, bool& freshIds)
{
	freshIds = ok && books.is_array();
	if (!freshIds)
		return fallback;
	std::set<std::string> audiobookIds;
	for (const auto& book : books) {
		if (!book.is_object() || JsonString(book, "type") != "audiobook")
			continue;
		std::string id = JsonString(book, "id");
		if (id.empty())
			id = SpotifyItemIdForUri(JsonString(book, "uri"));
		if (!id.empty())
			audiobookIds.insert(id);
	}
	return audiobookIds;
}

static void
SendAudiobookIdsSnapshot(BMessenger messenger, int32 loadGeneration,
	const std::set<std::string>& audiobookIds)
{
	BMessage ids(kMsgAudiobookIdsUpdated);
	ids.AddInt32("load_generation", loadGeneration);
	for (const std::string& id : audiobookIds)
		ids.AddString("audiobook_id", id.c_str());
	messenger.SendMessage(&ids);
}

static std::vector<RowData>
PodcastRows(const nlohmann::json& data,
	const std::set<std::string>& audiobookIds)
{
	std::vector<RowData> rows;
	if (!data.contains("items") || !data["items"].is_array())
		return rows;
	for (const auto& item : data["items"]) {
		if (!item.is_object() || !item.contains("show")
				|| !item["show"].is_object())
			continue;
		const auto& show = item["show"];
		std::string id = JsonString(show, "id");
		std::string uri = JsonString(show, "uri");
		if (id.empty())
			id = SpotifyItemIdForUri(uri);
		if (JsonString(show, "type") != "show"
				|| !PrimaryUriMatchesTab(TAB_PODCASTS, uri)
				|| SpotifyEffectiveItemKind(kSpotifyItemShow, id,
					audiobookIds) == kSpotifyItemAudiobook) {
			continue;
		}
		std::string name = show.value("name", "Unknown");
		std::string publisher = show.value("publisher", "Unknown");
		rows.push_back({{name, publisher}, {uri, ""}, {name, ""}});
	}
	return rows;
}

static std::string
FollowedArtistsNextCursor(const nlohmann::json& data)
{
	if (!data.contains("artists") || !data["artists"].is_object()
			|| !data["artists"].contains("cursors")
			|| !data["artists"]["cursors"].is_object()) {
		return "";
	}
	const auto& cursors = data["artists"]["cursors"];
	if (cursors.contains("after") && cursors["after"].is_string())
		return cursors["after"].get<std::string>();
	return "";
}

static std::vector<RowData>
FollowedArtistRows(const nlohmann::json& data)
{
	std::vector<RowData> rows;
	if (!data.contains("artists") || !data["artists"].is_object()
			|| !data["artists"].contains("items")
			|| !data["artists"]["items"].is_array())
		return rows;
	for (const auto& item : data["artists"]["items"]) {
		if (!item.is_object())
			continue;
		std::string name = item.contains("name") && item["name"].is_string()
			? item["name"].get<std::string>() : "Unknown";
		std::string uri = item.contains("uri") && item["uri"].is_string()
			? item["uri"].get<std::string>() : "";
		std::string genre = "Artist";
		if (item.contains("genres") && item["genres"].is_array()
				&& !item["genres"].empty() && item["genres"][0].is_string())
			genre = item["genres"][0].get<std::string>();
		rows.push_back({{name, genre}, {uri, ""}, {name, ""}});
	}
	return rows;
}

static bool
HasFollowedArtistsItems(const nlohmann::json& data)
{
	return data.contains("artists") && data["artists"].is_object()
		&& data["artists"].contains("items")
		&& data["artists"]["items"].is_array();
}

static void
HandleFollowedArtistsResponse(bool ok, const nlohmann::json& data,
	BMessenger messenger, bool snapshot, int32 loadGeneration)
{
	if (!ok) {
		if (SpotifyResponseStatus(data) == 403) {
			SendDiscoverTabRows(messenger, TAB_FOLLOWED_ARTISTS, snapshot,
				loadGeneration,
				{{{B_TRANSLATE("Permission to read followed artists is missing"),
					B_TRANSLATE("Reconnect Spotify in Settings")},
					{"", ""}, {"", ""}}});
		}
		SendDiscoverPageDone(messenger, TAB_FOLLOWED_ARTISTS, loadGeneration,
			"", false);
		return;
	}
	if (!HasFollowedArtistsItems(data)) {
		SendDiscoverPageDone(messenger, TAB_FOLLOWED_ARTISTS, loadGeneration,
			"", false);
		return;
	}
	std::vector<RowData> rows = FollowedArtistRows(data);
	SendDiscoverTabRows(messenger, TAB_FOLLOWED_ARTISTS, snapshot,
		loadGeneration, rows);
	std::string next = FollowedArtistsNextCursor(data);
	if (next.empty() && rows.empty()) {
		SendDiscoverTabRows(messenger, TAB_FOLLOWED_ARTISTS, snapshot,
			loadGeneration, {{{B_TRANSLATE("No followed artists"), ""},
				{"", ""}, {"", ""}}});
	}
	SendDiscoverPageDone(messenger, TAB_FOLLOWED_ARTISTS, loadGeneration, next,
		!next.empty());
}

static RowData
SavedEpisodeRow(const nlohmann::json& episode, bool showProgress)
{
	std::string showName;
	std::string showUri;
	if (episode.contains("show") && episode["show"].is_object()) {
		showName = JsonString(episode["show"], "name");
		std::string showId = JsonString(episode["show"], "id");
		showUri = !showId.empty()
			? SpotifyUriForItemKind(kSpotifyItemShow, showId)
			: JsonString(episode["show"], "uri");
	}
	std::string episodeId = JsonString(episode, "id");
	std::string episodeUri = !episodeId.empty()
		? SpotifyUriForItemKind(kSpotifyItemEpisode, episodeId)
		: JsonString(episode, "uri");
	std::string episodeName = JsonString(episode, "name");
	std::string progress;
	if (showProgress && episode.contains("resume_point")
			&& episode["resume_point"].is_object()) {
		const auto& resume = episode["resume_point"];
		progress = JsonBool(resume, "fully_played")
			? B_TRANSLATE("Done")
			: DurationText(JsonInt32(resume, "resume_position_ms"));
	}
	return {{episodeName, showName, JsonString(episode, "release_date"),
		DurationText(JsonInt32(episode, "duration_ms")), progress},
		{episodeUri, showUri, "", "", ""},
		{episodeName, showName, "", "", ""}};
}

static std::vector<RowData>
SavedEpisodeRows(const nlohmann::json& data, bool showProgress)
{
	std::vector<RowData> rows;
	if (!data.contains("items") || !data["items"].is_array())
		return rows;
	for (const auto& saved : data["items"]) {
		if (!saved.is_object() || !saved.contains("episode")
				|| !saved["episode"].is_object())
			continue;
		const auto& episode = saved["episode"];
		if (JsonString(episode, "type") != "episode")
			continue;
		RowData row = SavedEpisodeRow(episode, showProgress);
		if (!row.vals[0].empty()
				&& PrimaryUriMatchesTab(TAB_SAVED_EPISODES, row.uris[0]))
			rows.push_back(std::move(row));
	}
	return rows;
}

static void
HandleSavedEpisodesResponse(bool ok, const nlohmann::json& data,
	BMessenger messenger, int32 offset, bool showProgress, bool snapshot,
	int32 loadGeneration)
{
	if (!ok || !data.is_object() || !data.contains("items")
			|| !data["items"].is_array()) {
		SendDiscoverPageDone(messenger, TAB_SAVED_EPISODES, loadGeneration, "",
			false);
		return;
	}
	std::vector<RowData> rows = SavedEpisodeRows(data, showProgress);
	SendDiscoverTabRows(messenger, TAB_SAVED_EPISODES, snapshot,
		loadGeneration, rows);
	int32 count = ok && data.contains("items") && data["items"].is_array()
		? (int32)data["items"].size() : 0;
	int32 total = ok ? JsonInt32(data, "total", offset + count)
		: offset + count;
	SendDiscoverPageDone(messenger, TAB_SAVED_EPISODES, loadGeneration, "",
		ok && count > 0 && offset + count < total);
}

static std::vector<RowData>
AudiobookRows(const nlohmann::json& data)
{
	std::vector<RowData> rows;
	if (!data.contains("items") || !data["items"].is_array())
		return rows;
	for (const auto& book : data["items"]) {
		if (!book.is_object() || JsonString(book, "type") != "audiobook")
			continue;
		std::string id = JsonString(book, "id");
		std::string uri = !id.empty()
			? SpotifyUriForItemKind(kSpotifyItemAudiobook, id)
			: JsonString(book, "uri");
		if (!PrimaryUriMatchesTab(TAB_AUDIOBOOKS, uri))
			continue;
		std::string author;
		if (book.contains("authors") && book["authors"].is_array()
				&& !book["authors"].empty()
				&& book["authors"][0].is_object())
			author = JsonString(book["authors"][0], "name");
		std::string name = JsonString(book, "name", "Unknown");
		rows.push_back({{name, author}, {uri, ""}, {name, ""}});
	}
	return rows;
}

static void
HandleAudiobooksResponse(bool ok, const nlohmann::json& data,
	BMessenger messenger, int32 offset, bool snapshot, int32 loadGeneration)
{
	if (!ok || !data.is_object() || !data.contains("items")
			|| !data["items"].is_array()) {
		SendDiscoverPageDone(messenger, TAB_AUDIOBOOKS, loadGeneration, "",
			false);
		return;
	}
	std::vector<RowData> rows = AudiobookRows(data);
	SendDiscoverTabRows(messenger, TAB_AUDIOBOOKS, snapshot, loadGeneration,
		rows);
	int32 count = ok && data.contains("items") && data["items"].is_array()
		? (int32)data["items"].size() : 0;
	int32 total = ok ? JsonInt32(data, "total", offset + count)
		: offset + count;
	SendDiscoverPageDone(messenger, TAB_AUDIOBOOKS, loadGeneration, "",
		ok && count > 0 && offset + count < total);
}

bool
DiscoverWindow::_CanLoadTab(int32 tab, bool nextPage, SpotifyApi*& api) const
{
	if (tab < 0 || tab >= TAB_COUNT || !_IsTabEffectivelyVisible(tab)
			|| !fLists[tab])
		return false;
	App* app = (App*)be_app;
	api = app ? app->GetApi() : nullptr;
	if (!api)
		return false;
	bool paged = tab == TAB_FOLLOWED_ARTISTS
		|| tab == TAB_SAVED_EPISODES || tab == TAB_AUDIOBOOKS;
	return !nextPage || (paged && !fPageLoading[tab] && fPageHasMore[tab]);
}


void
DiscoverWindow::_PrepareLoadTab(int32 tab, bool nextPage)
{
	if (nextPage)
		return;
	fTabLoadGeneration[tab]++;
	fLoaded[tab] = true;
	fLoadTime[tab] = system_time();
	fPageLoading[tab] = false;
	fPageHasMore[tab] = true;
	fPageOffset[tab] = 0;
	fPageCursor[tab].clear();
}


void
DiscoverWindow::_LoadPlaylistsTab(SpotifyApi*, const BMessenger& messenger,
	bool snapshot, int32 loadGeneration)
{
	SendDiscoverTabRows(messenger, TAB_PLAYLISTS, snapshot, loadGeneration,
		{{{ B_TRANSLATE("Liked Songs"), "Spotify"},
		{"spotify:collection", ""},
		{B_TRANSLATE("Liked Songs"), ""}, true, true}});
	ReloadPlaylists();
}


void
DiscoverWindow::_LoadTopTracksTab(SpotifyApi* api, const BMessenger& messenger,
	bool snapshot, int32 loadGeneration)
{
	api->Content().GetTopItems("tracks", 20,
		[messenger, snapshot, loadGeneration](bool ok,
				const nlohmann::json& data) {
		if (ok)
			SendDiscoverTabRows(messenger, TAB_TOP_TRACKS, snapshot,
				loadGeneration, TopTrackRows(data));
	});
}


void
DiscoverWindow::_LoadTopArtistsTab(SpotifyApi* api, const BMessenger& messenger,
	bool snapshot, int32 loadGeneration)
{
	api->Content().GetTopItems("artists", 20,
		[messenger, snapshot, loadGeneration](bool ok,
				const nlohmann::json& data) {
		if (ok)
			SendDiscoverTabRows(messenger, TAB_TOP_ARTISTS, snapshot,
				loadGeneration, TopArtistRows(data));
	});
}


void
DiscoverWindow::_LoadNewReleasesTab(SpotifyApi* api,
	const BMessenger& messenger, bool snapshot, int32 loadGeneration)
{
	api->Content().GetNewReleases(20,
		[messenger, snapshot, loadGeneration](bool ok,
				const nlohmann::json& data) {
		if (ok)
			SendDiscoverTabRows(messenger, TAB_NEW_RELEASES, snapshot,
				loadGeneration, NewReleaseRows(data));
	});
}


void
DiscoverWindow::_LoadSavedAlbumsTab(SpotifyApi* api,
	const BMessenger& messenger, bool snapshot, int32 loadGeneration)
{
	api->Library().GetSavedAlbums(20,
		[messenger, snapshot, loadGeneration](bool ok,
				const nlohmann::json& data) {
		if (ok)
			SendDiscoverTabRows(messenger, TAB_SAVED_ALBUMS, snapshot,
				loadGeneration, SavedAlbumRows(data));
	});
}


void
DiscoverWindow::_LoadPodcastsTab(SpotifyApi* api, const BMessenger& messenger,
	bool, int32 loadGeneration)
{
	std::set<std::string> cachedAudiobookIds = fAudiobookIds;
	auto loadShows = [api, messenger, loadGeneration](
			const std::set<std::string>& audiobookIds, bool freshIds) {
		if (freshIds)
			SendAudiobookIdsSnapshot(messenger, loadGeneration, audiobookIds);
		api->Library().GetSavedShows(20,
			[messenger, audiobookIds, loadGeneration](bool ok,
					const nlohmann::json& data) {
			if (ok) {
				SendDiscoverTabRows(messenger, TAB_PODCASTS, true,
					loadGeneration, PodcastRows(data, audiobookIds));
			}
		});
	};
	api->Library().GetAllSavedAudiobooks(
		[loadShows, cachedAudiobookIds](bool ok, const nlohmann::json& books) {
		bool freshIds = false;
		std::set<std::string> audiobookIds = AudiobookIdsFromResponse(ok,
			books, cachedAudiobookIds, freshIds);
		loadShows(audiobookIds, freshIds);
	});
}


void
DiscoverWindow::_LoadFollowedArtistsTab(SpotifyApi* api,
	const BMessenger& messenger, bool snapshot, int32 loadGeneration)
{
	fPageLoading[TAB_FOLLOWED_ARTISTS] = true;
	std::string after = fPageCursor[TAB_FOLLOWED_ARTISTS];
	api->Artists().GetFollowedArtists(after, 50,
		[messenger, snapshot, loadGeneration](bool ok,
				const nlohmann::json& data) {
		HandleFollowedArtistsResponse(ok, data, messenger, snapshot,
			loadGeneration);
	});
}


void
DiscoverWindow::_LoadSavedEpisodesTab(SpotifyApi* api,
	const BMessenger& messenger, bool snapshot, int32 loadGeneration)
{
	HaifySettings accountSettings = SettingsController::Load();
	bool showProgress = accountSettings.grantedScopes.find(
		"user-read-playback-position") != std::string::npos;
	fPageLoading[TAB_SAVED_EPISODES] = true;
	int32 offset = fPageOffset[TAB_SAVED_EPISODES];
	api->Library().GetSavedEpisodes(offset, 50,
		[messenger, offset, showProgress, snapshot, loadGeneration](bool ok,
				const nlohmann::json& data) {
		HandleSavedEpisodesResponse(ok, data, messenger, offset, showProgress,
			snapshot, loadGeneration);
	});
}


void
DiscoverWindow::_LoadAudiobooksTab(SpotifyApi* api, const BMessenger& messenger,
	bool snapshot, int32 loadGeneration)
{
	fPageLoading[TAB_AUDIOBOOKS] = true;
	int32 offset = fPageOffset[TAB_AUDIOBOOKS];
	api->Library().GetSavedAudiobooks(offset, 50,
		[messenger, offset, snapshot, loadGeneration](bool ok,
				const nlohmann::json& data) {
		HandleAudiobooksResponse(ok, data, messenger, offset, snapshot,
			loadGeneration);
	});
}

void
DiscoverWindow::_LoadTab(int32 tab, bool nextPage)
{
	SpotifyApi* api = nullptr;
	if (!_CanLoadTab(tab, nextPage, api))
		return;
	_PrepareLoadTab(tab, nextPage);
	BMessenger messenger(this);
	bool snapshot = !nextPage && tab != TAB_PLAYLISTS;
	typedef void (DiscoverWindow::*Loader)(SpotifyApi*, const BMessenger&,
		bool, int32);
	static const Loader loaders[TAB_COUNT] = {
		&DiscoverWindow::_LoadPlaylistsTab,
		&DiscoverWindow::_LoadTopTracksTab,
		&DiscoverWindow::_LoadTopArtistsTab,
		&DiscoverWindow::_LoadNewReleasesTab,
		&DiscoverWindow::_LoadSavedAlbumsTab,
		&DiscoverWindow::_LoadPodcastsTab,
		&DiscoverWindow::_LoadFollowedArtistsTab,
		&DiscoverWindow::_LoadSavedEpisodesTab,
		&DiscoverWindow::_LoadAudiobooksTab
	};
	(this->*loaders[tab])(api, messenger, snapshot, fTabLoadGeneration[tab]);
}


void
DiscoverWindow::_ShowPlaylistContextMenu(const std::string& playlistId,
    bool owned, BPoint screen)
{
	if (playlistId.empty())
		return;
	BPopUpMenu* menu = new BPopUpMenu("playlist", false, false);

	if (owned) {
		BMessage* renMsg = new BMessage('plRn');
		renMsg->AddString("id", playlistId.c_str());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Rename" B_UTF8_ELLIPSIS), renMsg));
	}

	BMessage* delMsg = new BMessage('plDl');
	delMsg->AddString("id",    playlistId.c_str());
	delMsg->AddBool  ("owned", owned);
	menu->AddItem(new BMenuItem(
		owned ? B_TRANSLATE("Delete Playlist") : B_TRANSLATE("Unfollow Playlist"),
		delMsg));

	BMenuItem* sel = menu->Go(screen, false, true);
	if (sel) PostMessage(sel->Message());
	delete menu;
}
