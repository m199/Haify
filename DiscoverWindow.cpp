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

static void
WriteDiscoverCacheAsync(const std::string& path, nlohmann::json data)
{
	if (path.empty())
		return;
	uint64 generation;
	{
		BAutolock lock(&sDiscoverCacheWriterLock);
		generation = ++sNextDiscoverCacheWriteGeneration;
		sDiscoverCacheWriteGenerations[path] = generation;
	}
	std::thread([path, generation, data = std::move(data)]() mutable {
		BFile existingFile(path.c_str(), B_READ_ONLY);
		if (existingFile.InitCheck() == B_OK) {
			off_t existingSize = 0;
			if (existingFile.GetSize(&existingSize) == B_OK
					&& existingSize > 0
					&& existingSize <= 50LL * 1024LL * 1024LL) {
				std::string existingContent((size_t)existingSize, '\0');
				if (existingFile.Read(&existingContent[0],
						(size_t)existingSize) == existingSize) {
					try {
						nlohmann::json existing = nlohmann::json::parse(
							existingContent);
						if (existing.value("version", 0)
								== kDiscoverCacheVersion
								&& existing.value("account_id", "")
									== data.value("account_id", "")
								&& existing.contains("tabs")
								&& existing["tabs"].is_object()) {
							for (auto tab = existing["tabs"].begin();
									tab != existing["tabs"].end(); ++tab) {
								if (!data["tabs"].contains(tab.key()))
									data["tabs"][tab.key()] = tab.value();
							}
							if (!data.contains("audiobook_ids")
									&& existing.contains("audiobook_ids")
									&& existing["audiobook_ids"].is_array()) {
								data["audiobook_ids"] = existing["audiobook_ids"];
							}
						}
					} catch (...) {
					}
				}
			}
		}
		existingFile.Unset();
		std::string serialized = data.dump();
		std::string temporary = path + ".part-" + std::to_string(generation);
		BFile file(temporary.c_str(), B_WRITE_ONLY | B_CREATE_FILE
			| B_ERASE_FILE);
		bool written = file.InitCheck() == B_OK
			&& file.Write(serialized.data(), serialized.size())
				== (ssize_t)serialized.size();
		file.Unset();
		bool current = false;
		{
			BAutolock lock(&sDiscoverCacheWriterLock);
			auto latest = sDiscoverCacheWriteGenerations.find(path);
			current = latest != sDiscoverCacheWriteGenerations.end()
				&& latest->second == generation;
			if (current)
				sDiscoverCacheWriteGenerations.erase(latest);
		}
		if (written && current) {
			unlink(path.c_str());
			rename(temporary.c_str(), path.c_str());
		} else {
			unlink(temporary.c_str());
		}
	}).detach();
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
	switch (message->what) {
		case 'togT':
		{
			int32 tab;
			if (message->FindInt32("tab", &tab) != B_OK) break;
			if (tab < 0 || tab >= TAB_COUNT) break;
			if (tab == TAB_AUDIOBOOKS && !_AudiobooksEnabled()) break;
			if (fTabVisible[tab]) {
				int32 visibleCount = 0;
				for (int i = 0; i < TAB_COUNT; i++)
					if (_IsTabEffectivelyVisible(i))
						visibleCount++;
				if (visibleCount <= 1)
					break;
			}
			fTabVisible[tab] = !fTabVisible[tab];
			fTabMenuItems[tab]->SetMarked(fTabVisible[tab]);
			{
				SettingsController::Update([&](HaifySettings& s) {
					_SaveTabVisibility(s);
				});
			}
			_RebuildTabs();
			break;
		}

		case 'tRdr':
			_MoveTab(message->GetInt32("source", -1),
				message->GetInt32("target", -1));
			break;

		case 'tRst':
		{
			fTabOrder.clear();
			for (int32 i = 0; i < TAB_COUNT; i++)
				fTabOrder.push_back(i);
			SettingsController::Update([&](HaifySettings& settings) {
				_SaveTabOrder(settings);
			});
			_RebuildTabs();
			break;
		}

		case MSG_SPOTIFY_CAPABILITIES_CHANGED:
			if (fTabMenuItems[TAB_AUDIOBOOKS]) {
				fTabMenuItems[TAB_AUDIOBOOKS]->SetEnabled(_AudiobooksEnabled());
				fTabMenuItems[TAB_AUDIOBOOKS]->SetMarked(
					fTabVisible[TAB_AUDIOBOOKS] && _AudiobooksEnabled());
			}
			_RebuildTabs();
			break;

		case 'tabS':
		{
			int32 visual = 0;
			message->FindInt32("tab", &visual);
			int32 logical = _LogicalTab(visual);
			if (logical >= 0) {
				_LoadPersistentCache(logical);
				if (fLists[logical])
					((DiscoverListView*)fLists[logical])->SetPlayingUri(
						fCurrentTrackUri);
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
			break;
		}

		case kMsgAudiobookIdsUpdated:
			_ApplyAudiobookIdSnapshot(message);
			break;

		case kMsgCacheLoaded:
		case 'uRow':
		{
			int32 tab, cols;
			if (message->FindInt32("tab",  &tab)  != B_OK) break;
			if (message->FindInt32("cols", &cols) != B_OK || cols <= 0) break;
			if (tab < 0 || tab >= TAB_COUNT || !fLists[tab]) break;
			bool fromCache = message->GetBool("from_cache", false);
			bool cacheLast = true;
			if (fromCache) {
				int32 cacheGeneration = message->GetInt32(
					"cache_generation", -1);
				if (cacheGeneration != fCacheLoadGeneration[tab]
						|| message->GetString("account_id", "")
							!= fCacheAccountId)
					break;
				cacheLast = message->GetBool("cache_last", true);
				if (cacheLast)
					fCacheLoadPending[tab] = false;
				int32 selectedTab = _LogicalTab(
					fTabView ? fTabView->Selection() : -1);
				if (!message->GetBool("cache_available", true)
						|| fFreshSnapshot[tab] || tab != selectedTab)
					break;
				if (message->GetBool("cache_first", false))
					fLists[tab]->Clear();
			}
			if (!fromCache) {
				int32 loadGeneration = -1;
				if (message->FindInt32("load_generation", &loadGeneration) == B_OK
						&& loadGeneration != fTabLoadGeneration[tab])
					break;
			}
			if (message->GetBool("audiobook_ids_snapshot", false))
				_ApplyAudiobookIdSnapshot(message);

			std::vector<std::string> allV, allU, allT;
			bool snapshotMessage = message->GetBool("snapshot", false);
			if (fromCache)
				snapshotMessage = false;
			const char* s;
			for (int32 i = 0; message->FindString("v", i, &s) == B_OK; i++) allV.push_back(s);
			for (int32 i = 0; message->FindString("u", i, &s) == B_OK; i++) allU.push_back(s);
			for (int32 i = 0; message->FindString("t", i, &s) == B_OK; i++) allT.push_back(s);

			int32 nRows = (int32)allV.size() / cols;
			if (tab == TAB_AUDIOBOOKS && snapshotMessage) {
				fAudiobookIds.clear();
				fAudiobookIdsKnown = true;
			}
			std::set<std::string> snapshotUris;
			std::vector<std::string> snapshotOrder;
			for (int32 r = 0; r < nRows; r++) {
				if ((int32)allU.size() < (r + 1) * cols
						|| (int32)allT.size() < (r + 1) * cols)
					continue;
				bool writable = true;
				message->FindBool("writable", r, &writable);
				bool owned = false;
				message->FindBool("owned", r, &owned);
				auto vb = allV.begin() + r * cols;
				auto ub = allU.begin() + r * cols;
				auto tb = allT.begin() + r * cols;
				if (!ub->empty() && !PrimaryUriMatchesTab(tab, *ub))
					continue;
				std::string primaryId = SpotifyItemIdForUri(*ub);
				if (tab == TAB_PODCASTS
						&& SpotifyEffectiveItemKind(kSpotifyItemShow, primaryId,
							fAudiobookIds) == kSpotifyItemAudiobook) {
					continue;
				}
				if (tab == TAB_AUDIOBOOKS && !primaryId.empty()) {
					fAudiobookIds.insert(primaryId);
					fAudiobookIdsKnown = true;
				}
				if (!ub->empty()) {
					snapshotUris.insert(*ub);
					snapshotOrder.push_back(*ub);
				}
				if (!ub->empty()) {
					DiscoverRow* existing = _FindRow(tab, *ub);
					if (existing) {
						existing->fUris.assign(ub, ub + cols);
						existing->fTitles.assign(tb, tb + cols);
						existing->fWritable = writable;
						existing->fOwned = owned;
						for (int32 column = 0; column < cols; column++) {
							BoldStringField* field = dynamic_cast<BoldStringField*>(
								existing->GetField(column));
							if (field) {
								field->SetString((vb + column)->c_str());
								field->fEnabled = writable;
							}
						}
						fLists[tab]->UpdateRow(existing);
						continue;
					}
				}
				if (tab >= TAB_SAVED_ALBUMS && tab <= TAB_AUDIOBOOKS) {
					if (ub->empty() && !snapshotMessage) {
						bool hasRealRow = false;
						for (int32 i = 0; i < fLists[tab]->CountRows(); i++) {
							DiscoverRow* existing = dynamic_cast<DiscoverRow*>(
								fLists[tab]->RowAt(i));
							if (existing && !existing->fUris.empty()
									&& !existing->fUris[0].empty()) {
								hasRealRow = true;
								break;
							}
						}
						if (hasRealRow)
							continue;
					}
				}
				fLists[tab]->AddRow(new DiscoverRow(
					std::vector<std::string>(vb, vb + cols),
					std::vector<std::string>(ub, ub + cols),
					std::vector<std::string>(tb, tb + cols), writable, owned));
			}
			if (snapshotMessage) {
				bool keptPlaceholder = false;
				for (int32 index = fLists[tab]->CountRows() - 1;
						index >= 0; index--) {
					DiscoverRow* row = dynamic_cast<DiscoverRow*>(
						fLists[tab]->RowAt(index));
					if (!row)
						continue;
					std::string uri = row->fUris.empty() ? "" : row->fUris[0];
					if (uri.empty() && nRows > 0 && !keptPlaceholder) {
						keptPlaceholder = true;
						continue;
					}
					if (snapshotUris.find(uri) == snapshotUris.end()) {
						fLists[tab]->RemoveRow(row);
						delete row;
					}
				}
				for (int32 target = 0;
						target < (int32)snapshotOrder.size(); target++) {
					DiscoverRow* row = _FindRow(tab, snapshotOrder[target]);
					if (!row)
						continue;
					int32 current = fLists[tab]->IndexOf(row);
					if (current == target)
						continue;
					bool selected = fLists[tab]->CurrentSelection() == row;
					fLists[tab]->RemoveRow(row);
					fLists[tab]->AddRow(row, target);
					if (selected)
						fLists[tab]->AddToSelection(row);
				}
			}
			if (fromCache && !cacheLast)
				break;

			int32 selectedTab = _LogicalTab(
				fTabView ? fTabView->Selection() : -1);
			if (tab == selectedTab)
				((DiscoverListView*)fLists[tab])->SetPlayingUri(fCurrentTrackUri);
			if (tab == TAB_AUDIOBOOKS)
				_RemoveAudiobookDuplicatesFromPodcasts();
			if (fromCache) {
				fLoaded[tab] = true;
				fCacheBacked[tab] = true;
				fPageLoading[tab] = false;
				fPageHasMore[tab] = false;
				fLoadTime[tab] = 0;
			} else {
				if (snapshotMessage) {
					fFreshSnapshot[tab] = true;
					fCacheBacked[tab] = false;
				}
				_ScheduleCacheSave();
			}
			_CheckLazyLoad();
			break;
		}

		case kMsgCheckLazyLoad:
			_CheckLazyLoad();
			break;

		case kMsgPageDone:
		{
			int32 tab = message->GetInt32("tab", -1);
			if (tab < 0 || tab >= TAB_COUNT)
				break;
			if (message->GetInt32("load_generation", -1)
					!= fTabLoadGeneration[tab])
				break;
			fPageLoading[tab] = false;
			fPageHasMore[tab] = message->GetBool("has_more", false);
			fPageOffset[tab] = message->GetInt32("next_offset",
				fPageOffset[tab]);
			fPageCursor[tab] = message->GetString("next_cursor", "");
			_CheckLazyLoad();
			break;
		}

		case kMsgSaveCache:
			delete fCacheSaveRunner;
			fCacheSaveRunner = nullptr;
			_WriteCacheNow();
			break;

		case 'play':
		{
			const char* uri = message->GetString("uri", "");
			if (uri && SpotifyItemIsPlayable(
					SpotifyItemKindForUri(uri))) {
				if (fCurrentTrackUri != uri) {
					fCurrentTrackUri = uri;
					int32 tab = _LogicalTab(
						fTabView ? fTabView->Selection() : -1);
					if (tab >= 0 && fLists[tab])
						((DiscoverListView*)fLists[tab])->SetPlayingUri(
							fCurrentTrackUri);
				}
			}
			be_app->PostMessage(message);
			break;
		}

		case 'open':
		{
			const char* uri = nullptr;
			const char* titleStr = nullptr;
			message->FindString("uri",   &uri);
			message->FindString("title", &titleStr);
			if (!uri || !uri[0]) break;

			BMessage fwd('open');
			fwd.AddString("uri", uri);
			fwd.AddString("title", titleStr ? titleStr : "");
			be_app->PostMessage(&fwd);
			break;
		}

		case 'pStU':
		{
			const char* newUri = nullptr;
			message->FindString("trackUri", &newUri);
			if (!newUri) break;
			std::string uriStr = newUri;
			if (fCurrentTrackUri == uriStr)
				break;
			fCurrentTrackUri = uriStr;
			int32 tab = _LogicalTab(
				fTabView ? fTabView->Selection() : -1);
			if (tab >= 0 && fLists[tab])
				((DiscoverListView*)fLists[tab])->SetPlayingUri(uriStr);
			break;
		}

		case 'rClk':
		{
			const char* uri = nullptr;
			const char* title = nullptr;
			BPoint pt;
			if (message->FindString("uri",      &uri) != B_OK) break;
			message->FindString("title", &title);
			if (message->FindPoint ("screenPt", &pt)  != B_OK) break;
			int32 sourceTab = -1;
			message->FindInt32("tab", &sourceTab);
			std::string uriStr = uri;

			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();

			SpotifyItemKind kind = SpotifyItemKindForUri(uriStr);
			if (SpotifyItemIsPlayable(kind)) {
				std::string context = sourceTab == TAB_SAVED_EPISODES
					? "spotify:saved-episodes" : "";
				bool saved = sourceTab == TAB_SAVED_EPISODES;
				bool known = saved;
				auto state = fKnownLibraryStates.find(uriStr);
				if (state != fKnownLibraryStates.end()) {
					known = true;
					saved = state->second;
				}
				if (!known && api) {
					int32 generation = ++fLibraryStateGenerations[uriStr];
					BMessenger self(this);
					api->Library().CheckLibraryItems({uriStr},
						[self, uriStr, generation](bool ok,
								const nlohmann::json& data) {
							BMessage result(kMsgLibraryStateCached);
							result.AddString("uri", uriStr.c_str());
							result.AddInt32("generation", generation);
							result.AddBool("ok", ok && data.is_array()
								&& !data.empty() && data[0].is_boolean());
							result.AddBool("saved", ok && data.is_array()
								&& !data.empty() && data[0].is_boolean()
								&& data[0].get<bool>());
							self.SendMessage(&result);
						});
				}
				::ShowPlayableItemContextMenu(uriStr, context, pt,
					BMessenger(this), api, false, true, saved);
			} else if (kind == kSpotifyItemPlaylist) {
				_ShowPlaylistContextMenu(SpotifyItemIdForUri(uriStr),
					message->GetBool("owned", false), pt);
			} else if (kind != kSpotifyItemUnknown) {
				BPopUpMenu* menu = new BPopUpMenu("item", false, false);
				BMessage* openMsg = new BMessage('open');
				openMsg->AddString("uri", uriStr.c_str());
				openMsg->AddString("title", title ? title : "");
				menu->AddItem(new BMenuItem(B_TRANSLATE("Open"), openMsg));

				BMessage* playMsg = new BMessage('play');
				playMsg->AddString("uri", uriStr.c_str());
				menu->AddItem(new BMenuItem(B_TRANSLATE("Play"), playMsg));

				if (kind == kSpotifyItemAlbum) {
					menu->AddSeparatorItem();
					if (sourceTab == TAB_SAVED_ALBUMS) {
						BMessage* removeMsg = new BMessage('remA');
						removeMsg->AddString("uri", uriStr.c_str());
						menu->AddItem(new BMenuItem(
							B_TRANSLATE("Remove from Saved Albums"),
							removeMsg));
					} else {
						BMessage* saveMsg = new BMessage('savA');
						saveMsg->AddString("uri", uriStr.c_str());
						menu->AddItem(new BMenuItem(B_TRANSLATE("Save Album"), saveMsg));
					}
				} else if ((kind == kSpotifyItemShow
							&& sourceTab == TAB_PODCASTS)
						|| (kind == kSpotifyItemArtist
							&& sourceTab == TAB_FOLLOWED_ARTISTS)
						|| (kind == kSpotifyItemAudiobook
							&& sourceTab == TAB_AUDIOBOOKS)) {
					menu->AddSeparatorItem();
					BMessage* removeMsg = new BMessage('remI');
					removeMsg->AddString("uri", uriStr.c_str());
					const char* label = kind == kSpotifyItemShow
						? B_TRANSLATE("Unsubscribe")
						: kind == kSpotifyItemArtist
						? B_TRANSLATE("Unfollow Artist")
						: B_TRANSLATE("Remove from Audiobooks");
					menu->AddItem(new BMenuItem(label, removeMsg));
				}

				BMenuItem* sel = menu->Go(pt, false, true);
				if (sel && sel->Message())
					PostMessage(sel->Message());
				delete menu;
			}
			break;
		}

		case 'iCmR':
		{
			App* app = dynamic_cast<App*>(be_app);
			ShowPlayableItemContextMenu(message->GetString("uri", ""),
				message->GetString("context_uri", ""),
				message->GetPoint("screen_point", BPoint()), BMessenger(this),
				app ? app->GetApi() : nullptr,
				message->GetBool("library_only", false), true,
				message->GetBool("saved", false));
			break;
		}

		case kMsgLibraryStateCached:
		{
			std::string uri = message->GetString("uri", "");
			if (!message->GetBool("ok", false) || uri.empty()
					|| message->GetInt32("generation", -1)
						!= fLibraryStateGenerations[uri])
				break;
			fKnownLibraryStates[uri] = message->GetBool("saved", false);
			break;
		}

		case 'dDrp':
		{
			const char* uri = message->GetString("uri", "");
			if (!uri || !uri[0]) uri = message->GetString("trackUri", "");
			if (!uri || !uri[0]) uri = message->GetString("albumUri", "");
			if (!uri || !uri[0])
				break;
			int32 targetTab = message->GetInt32("tab", -1);
			std::string targetUri = message->GetString("targetUri", "");
			if (targetTab == TAB_PLAYLISTS && !targetUri.empty()
					&& _HandlePlaylistDrop(uri, targetUri,
						message->GetBool("targetWritable", false))) {
				break;
			}
			_HandleLibraryDrop(uri);
			break;
		}

		case 'dPlA':
		{
			if (message->GetBool("ok", false))
				break;
			const char* text = message->GetInt32("status", -1) == 403
				? B_TRANSLATE("This playlist cannot be modified.")
				: B_TRANSLATE("Spotify could not add this item to the playlist.");
			BAlert* alert = new BAlert("", text, B_TRANSLATE("OK"), nullptr,
				nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->Go();
			break;
		}

		case 'dSts':
		{
			std::string uri = message->GetString("uri", "");
			if (!message->GetBool("ok", false)) {
				BAlert* alert = new BAlert("", B_TRANSLATE(
					"Spotify could not check this item's library status."),
					B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->Go();
				break;
			}
			if (message->GetBool("saved", false)) {
				_SelectLibraryTarget(uri);
				PostLibraryChange("add", uri);
				break;
			}
			App* app = dynamic_cast<App*>(be_app);
			SpotifyApi* api = app ? app->GetApi() : nullptr;
			if (!api) break;
			BMessenger self(this);
			api->Library().SaveLibraryItems({uri}, [self, uri](bool ok,
					const nlohmann::json&) {
				BMessage result('dAdd');
				result.AddString("uri", uri.c_str());
				result.AddBool("ok", ok);
				self.SendMessage(&result);
			});
			break;
		}

		case 'dAdd':
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
			break;
		}

		case 'savA':
		{
			const char* uri = message->GetString("uri", "");
			std::string savedUri = uri ? uri : "";
			std::string albumId = SpotifyItemIdForUri(savedUri);
			if (SpotifyItemKindForUri(savedUri) != kSpotifyItemAlbum
					|| albumId.empty())
				break;
			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();
			if (!api)
				break;
			api->Library().SaveAlbum(albumId, [savedUri](bool ok,
					const nlohmann::json&) {
				if (ok) PostLibraryChange("add", savedUri);
			});
			break;
		}

		case 'remA':
		{
			const char* uri = message->GetString("uri", "");
			std::string removedUri = uri ? uri : "";
			std::string albumId = SpotifyItemIdForUri(removedUri);
			if (SpotifyItemKindForUri(removedUri) != kSpotifyItemAlbum
					|| albumId.empty())
				break;
			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();
			if (!api)
				break;
			api->Library().RemoveSavedAlbum(albumId, [removedUri](bool ok,
					const nlohmann::json&) {
				if (ok) PostLibraryChange("remove", removedUri);
			});
			break;
		}

		case 'remI':
		{
			std::string uri = message->GetString("uri", "");
			SpotifyItemKind kind = SpotifyItemKindForUri(uri);
			App* app = dynamic_cast<App*>(be_app);
			SpotifyApi* api = app ? app->GetApi() : nullptr;
			if (!api || uri.empty()) break;
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
			break;
		}

		case 'rmIR':
			if (message->GetBool("ok", false)) {
				PostLibraryChange("remove", message->GetString("uri", ""));
			} else {
				BAlert* alert = new BAlert("", B_TRANSLATE(
					"Spotify could not remove this item from your library."),
					B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->Go();
			}
			break;

		case 'remL':
		{
			const char* uri = message->GetString("trackUri", "");
			std::string removedUri = uri ? uri : "";
			SpotifyItemKind kind = SpotifyItemKindForUri(removedUri);
			if (!SpotifyItemIsPlayable(kind)) break;
			App* app = dynamic_cast<App*>(be_app);
			SpotifyApi* api = app ? app->GetApi() : nullptr;
			if (!api) break;
			api->Library().RemoveLibraryItems({removedUri}, [removedUri](bool ok,
					const nlohmann::json&) {
				if (!ok) return;
				PostLibraryChange("remove", removedUri);
			});
			break;
		}

		case 'tply':
		{
			const char* trackUri = message->GetString("trackUri", "");
			if (!*trackUri) break;
			BMessage play('play');
			play.AddString("uri", trackUri);
			be_app->PostMessage(&play);
			break;
		}

		case 'plNw':
		{
			BMessage confirm('plNc');
			TextInputDialog* dlg = new TextInputDialog(
				B_TRANSLATE("New Playlist"),
				B_TRANSLATE("Name:"), "", BMessenger(this), confirm);
			dlg->Show();
			break;
		}

		case 'plNc':
		{
			const char* name = message->GetString("name", "");
			if (!*name) break;
			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();
			if (!api) break;
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
			break;
		}

		case 'plCr':
		{
			if (!message->GetBool("ok", false)) {
				BAlert* alert = new BAlert("", B_TRANSLATE(
					"Spotify could not create the playlist."),
					B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->Go();
				break;
			}
			std::string id = message->GetString("id", "");
			if (id.empty()) {
				be_app->PostMessage(MSG_PLAYLISTS_CHANGED);
				break;
			}
			PostPlaylistChange("add", id,
				message->GetString("name", "Unknown"),
				message->GetString("owner", "Spotify"), true, true);
			break;
		}

		case 'plRn':
		{
			const char* id = message->GetString("id", "");
			if (!*id) break;
			std::string currentName;
			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();
			if (api) {
				for (const auto& pl : api->Playlists().GetCachedPlaylists())
					if (pl.first == id) { currentName = pl.second; break; }
			}
			BMessage confirm('plRc');
			confirm.AddString("id", id);
			TextInputDialog* dlg = new TextInputDialog(
				B_TRANSLATE("Rename Playlist"),
				B_TRANSLATE("Name:"), currentName.c_str(),
				BMessenger(this), confirm);
			dlg->Show();
			break;
		}

		case 'plRc':
		{
			const char* name = message->GetString("name", "");
			const char* id   = message->GetString("id",   "");
			if (!*name || !*id) break;
			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();
			if (!api) break;
			BMessenger self(this);
			std::string sid = id, newName = name;
			std::string oldName;
			DiscoverRow* row = _FindPlaylistRow(PlaylistUri(sid));
			if (row && !row->fTitles.empty()) {
				oldName = row->fTitles[0];
				_UpdatePlaylistRow(row, newName, "", row->fWritable,
					row->fOwned);
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
			break;
		}

		case 'plRr':
		{
			std::string id = message->GetString("id", "");
			std::string oldName = message->GetString("oldName", "");
			std::string newName = message->GetString("newName", "");
			if (message->GetBool("ok", false)) {
				PostPlaylistChange("rename", id, newName);
			} else {
				DiscoverRow* row = _FindPlaylistRow(PlaylistUri(id));
				if (row && !row->fTitles.empty()
						&& row->fTitles[0] == newName && !oldName.empty())
					_UpdatePlaylistRow(row, oldName, "", row->fWritable,
						row->fOwned);
				BAlert* alert = new BAlert("", B_TRANSLATE(
					"Spotify could not rename the playlist."),
					B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->Go();
			}
			break;
		}

		case 'plDl':
		{
			const char* id      = message->GetString("id",      "");
			bool        owned   = message->GetBool ("owned",    false);
			if (!*id) break;
			const char* label = owned
				? B_TRANSLATE("Delete Playlist")
				: B_TRANSLATE("Unfollow Playlist");
			const char* body  = owned
				? B_TRANSLATE("Really delete this playlist? This cannot be undone.")
				: B_TRANSLATE("Unfollow this playlist?");
			BAlert* alert = new BAlert("", body,
				B_TRANSLATE("Cancel"), label, nullptr,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			if (alert->Go() == 1) {
				App* app = (App*)be_app;
				SpotifyApi* api = app->GetApi();
				if (api) {
					BMessenger self(this);
					std::string sid = id;
					std::string uri = PlaylistUri(sid);
					if (fPendingPlaylistRemovals.find(sid)
							!= fPendingPlaylistRemovals.end())
						break;
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
			}
			break;
		}

		case 'plDr':
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
					fLists[TAB_PLAYLISTS]->AddRow(restore.row,
						restore.index);
					if (restore.selected)
						fLists[TAB_PLAYLISTS]->AddToSelection(restore.row);
				}
				BAlert* alert = new BAlert("", B_TRANSLATE(
					"Spotify could not remove the playlist."),
					B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->Go();
			}
			break;
		}

		case 'lddt':
			LoadData();
			break;

		case MSG_PLAYLISTS_CHANGED:
		{
			const char* operation = nullptr;
			if (message->FindString("operation", &operation) == B_OK)
				_ApplyPlaylistChange(message);
			else
				ReloadPlaylists();
			break;
		}

		case MSG_LIBRARY_CHANGED:
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
			break;
		}

		case 'lAdd':
			_ApplyResolvedLibraryAddition(message);
			break;

		case 'pSyn':
			_ApplyPlaylistSnapshot(message);
			break;

		case 'rTab':
		{
			int32 tab = -1;
			if (message->FindInt32("tab", &tab) == B_OK)
				_ReloadTab(tab);
			break;
		}

		case MSG_OPEN_BROWSER:
		case MSG_OPEN_PLAYLIST:
		case MSG_INIT_AUTH:
			be_app->PostMessage(message);
			break;

		case 'sout':
			be_app->PostMessage('sout');
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
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
		auto sendUnavailable = [&]() {
			BMessage response(kMsgCacheLoaded);
			response.AddInt32("tab", tab);
			response.AddInt32("cols", (int32)kTabCols[tab].size());
			response.AddInt32("cache_generation", generation);
			response.AddString("account_id", accountId.c_str());
			response.AddBool("from_cache", true);
			response.AddBool("cache_available", false);
			response.AddBool("cache_first", true);
			response.AddBool("cache_last", true);
			self.SendMessage(&response);
		};

		BFile file(path.c_str(), B_READ_ONLY);
		if (file.InitCheck() != B_OK) {
			sendUnavailable();
			return;
		}
		off_t size = 0;
		if (file.GetSize(&size) != B_OK || size <= 0
				|| size > 50LL * 1024LL * 1024LL) {
			sendUnavailable();
			return;
		}
		std::string content((size_t)size, '\0');
		if (file.Read(&content[0], (size_t)size) != size) {
			sendUnavailable();
			return;
		}
		try {
			nlohmann::json cache = nlohmann::json::parse(content);
			if (!cache.is_object()
					|| cache.value("version", 0) != kDiscoverCacheVersion
					|| cache.value("account_id", "") != accountId
					|| !cache.contains("tabs") || !cache["tabs"].is_object()) {
				sendUnavailable();
				return;
			}
			std::vector<std::string> audiobookIds;
			if (cache.contains("audiobook_ids")
					&& cache["audiobook_ids"].is_array()) {
				for (const auto& id : cache["audiobook_ids"]) {
					if (id.is_string() && !id.get<std::string>().empty())
						audiobookIds.push_back(id.get<std::string>());
				}
			}

			auto found = cache["tabs"].find(kTabDefs[tab].id);
			if (found == cache["tabs"].end() || !found->is_array()) {
				sendUnavailable();
				return;
			}

			std::vector<RowData> cachedRows;
			for (const auto& row : *found) {
				if ((int32)cachedRows.size() >= kMaxDiscoverCachedRowsPerTab)
					break;
				if (!row.is_object() || !row.contains("values")
						|| !row.contains("uris") || !row.contains("titles")
						|| !row["values"].is_array()
						|| !row["uris"].is_array()
						|| !row["titles"].is_array()
						|| row["values"].size() != kTabCols[tab].size()
						|| row["uris"].size() != kTabCols[tab].size()
						|| row["titles"].size() != kTabCols[tab].size()
						|| row["uris"].empty()
						|| !row["uris"][0].is_string()
						|| !PrimaryUriMatchesTab(tab,
							row["uris"][0].get<std::string>()))
					continue;
				bool valid = true;
				for (size_t column = 0; column < kTabCols[tab].size(); column++) {
					valid = valid && row["values"][column].is_string()
						&& row["uris"][column].is_string()
						&& row["titles"][column].is_string();
				}
				if (!valid)
					continue;

				RowData cached;
				for (const auto& value : row["values"])
					cached.vals.push_back(value.get<std::string>());
				for (const auto& value : row["uris"])
					cached.uris.push_back(value.get<std::string>());
				for (const auto& value : row["titles"])
					cached.ttls.push_back(value.get<std::string>());
				cached.writable = !row.contains("writable")
					|| !row["writable"].is_boolean()
					|| row["writable"].get<bool>();
				cached.owned = row.contains("owned")
					&& row["owned"].is_boolean()
					&& row["owned"].get<bool>();
				cachedRows.push_back(std::move(cached));
			}

			size_t offset = 0;
			bool firstBatch = true;
			do {
				size_t end = std::min(offset
					+ (size_t)kDiscoverCacheBatchRows, cachedRows.size());
				BMessage rows(kMsgCacheLoaded);
				rows.AddInt32("tab", tab);
				rows.AddInt32("cols", (int32)kTabCols[tab].size());
				rows.AddInt32("cache_generation", generation);
				rows.AddString("account_id", accountId.c_str());
				rows.AddBool("from_cache", true);
				rows.AddBool("cache_available", true);
				rows.AddBool("cache_first", firstBatch);
				rows.AddBool("cache_last", end >= cachedRows.size());
				if (firstBatch && cache.contains("audiobook_ids")) {
					rows.AddBool("audiobook_ids_snapshot", true);
					for (const std::string& id : audiobookIds)
						rows.AddString("audiobook_id", id.c_str());
				}
				for (size_t index = offset; index < end; index++) {
					const RowData& row = cachedRows[index];
					for (const std::string& value : row.vals)
						rows.AddString("v", value.c_str());
					for (const std::string& value : row.uris)
						rows.AddString("u", value.c_str());
					for (const std::string& value : row.ttls)
						rows.AddString("t", value.c_str());
					rows.AddBool("writable", row.writable);
					rows.AddBool("owned", row.owned);
				}
				if (self.SendMessage(&rows) != B_OK)
					return;
				firstBatch = false;
				offset = end;
			} while (offset < cachedRows.size());
		} catch (...) {
			sendUnavailable();
		}
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
	nlohmann::json cache = {
		{"version", kDiscoverCacheVersion},
		{"account_id", fCacheAccountId},
		{"saved_at", (int64)time(nullptr)},
		{"tabs", nlohmann::json::object()}
	};
	if (fAudiobookIdsKnown) {
		cache["audiobook_ids"] = nlohmann::json::array();
		for (const std::string& id : fAudiobookIds)
			cache["audiobook_ids"].push_back(id);
	}
	for (int32 tab = 0; tab < TAB_COUNT; tab++) {
		if (!fLists[tab] || (!fFreshSnapshot[tab] && !fCacheBacked[tab]))
			continue;
		nlohmann::json rows = nlohmann::json::array();
		for (int32 index = 0; index < fLists[tab]->CountRows()
				&& (int32)rows.size() < kMaxDiscoverCachedRowsPerTab; index++) {
			DiscoverRow* row = dynamic_cast<DiscoverRow*>(
				fLists[tab]->RowAt(index));
			if (!row || row->fUris.empty()
					|| !PrimaryUriMatchesTab(tab, row->fUris[0]))
				continue;
			size_t columns = kTabCols[tab].size();
			if (row->fUris.size() < columns)
				continue;
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
			rows.push_back({{"values", std::move(values)},
				{"uris", std::move(uris)}, {"titles", std::move(titles)},
				{"writable", row->fWritable}, {"owned", row->fOwned}});
		}
		cache["tabs"][kTabDefs[tab].id] = std::move(rows);
	}
	WriteDiscoverCacheAsync(path, std::move(cache));
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
	int32 tab = -1;
	switch (SpotifyItemKindForUri(uri)) {
		case kSpotifyItemAlbum:
			tab = TAB_SAVED_ALBUMS;
			break;
		case kSpotifyItemShow:
			tab = TAB_PODCASTS;
			break;
		case kSpotifyItemArtist:
			tab = TAB_FOLLOWED_ARTISTS;
			break;
		case kSpotifyItemEpisode:
			tab = TAB_SAVED_EPISODES;
			break;
		case kSpotifyItemAudiobook:
			tab = TAB_AUDIOBOOKS;
			break;
		default:
			break;
	}
	if (tab < 0 || !fLists[tab])
		return;
	int32 generation = ++fLibraryChangeGenerations[uri];
	bool refreshPodcasts = false;
	if (tab == TAB_AUDIOBOOKS) {
		std::string id = SpotifyItemIdForUri(uri);
		if (!id.empty()) {
			fAudiobookIdsKnown = true;
			if (operation == "add") {
				fAudiobookIds.insert(id);
				_RemoveAudiobookDuplicatesFromPodcasts();
			} else if (operation == "remove") {
				refreshPodcasts = fAudiobookIds.erase(id) > 0;
			}
			_ScheduleCacheSave();
		}
	}

	if (operation == "remove") {
		DiscoverRow* row = _FindRow(tab, uri);
		if (row) {
			fLists[tab]->RemoveRow(row);
			delete row;
			_ScheduleCacheSave();
		}
		if (fLoaded[tab])
			fLoadTime[tab] = system_time();
	} else if (operation == "add") {
		if (!fLoaded[tab] || _FindRow(tab, uri))
			return;
		// Resolve just the newly saved object. This preserves the current list,
		// selection and scroll position instead of rebuilding the whole tab.
		_ResolveLibraryAddition(tab, uri, generation);
	}
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

		std::vector<std::string> values;
		std::vector<std::string> uris;
		std::vector<std::string> titles;
		std::string name = JsonString(item, "name");
		if (name.empty() && tab == TAB_SAVED_EPISODES) {
			result.ReplaceBool("ok", false);
			self.SendMessage(&result);
			return;
		}
		if (name.empty())
			name = "Unknown";
		if (tab == TAB_SAVED_ALBUMS) {
			std::string artist = "Unknown";
			std::string artistUri;
			if (item.contains("artists") && item["artists"].is_array()
					&& !item["artists"].empty()
					&& item["artists"][0].is_object()) {
				artist = JsonString(item["artists"][0], "name", "Unknown");
				artistUri = JsonString(item["artists"][0], "uri");
			}
			values = {name, artist};
			uris = {uri, artistUri};
			titles = {name, artist};
		} else if (tab == TAB_PODCASTS) {
			values = {name, JsonString(item, "publisher", "Unknown")};
			uris = {uri, ""};
			titles = {name, ""};
		} else if (tab == TAB_FOLLOWED_ARTISTS) {
			std::string genre = "Artist";
			if (item.contains("genres") && item["genres"].is_array()
					&& !item["genres"].empty()
					&& item["genres"][0].is_string()) {
				genre = item["genres"][0].get<std::string>();
			}
			values = {name, genre};
			uris = {uri, ""};
			titles = {name, ""};
		} else if (tab == TAB_SAVED_EPISODES) {
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
			values = {name, showName, JsonString(item, "release_date"),
				DurationText(JsonInt32(item, "duration_ms")), progress};
			uris = {uri, showUri, "", "", ""};
			titles = {name, showName, "", "", ""};
		} else if (tab == TAB_AUDIOBOOKS) {
			std::string author;
			if (item.contains("authors") && item["authors"].is_array()
					&& !item["authors"].empty()
					&& item["authors"][0].is_object()) {
				author = JsonString(item["authors"][0], "name");
			}
			values = {name, author};
			uris = {uri, ""};
			titles = {name, ""};
		}

		if (values.empty()) {
			result.ReplaceBool("ok", false);
		} else {
			for (const std::string& value : values)
				result.AddString("v", value.c_str());
			for (const std::string& value : uris)
				result.AddString("u", value.c_str());
			for (const std::string& value : titles)
				result.AddString("t", value.c_str());
		}
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
	if (tab < 0 || tab >= TAB_COUNT || !fLists[tab] || !fLoaded[tab]
			|| fLibraryChangeGenerations[uri] != generation
			|| _FindRow(tab, uri)) {
		return;
	}

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

	// Remove an empty-state row (for example "No followed artists") before
	// inserting the real object at the top without disturbing existing rows.
	for (int32 i = fLists[tab]->CountRows() - 1; i >= 0; i--) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(fLists[tab]->RowAt(i));
		if (row && (row->fUris.empty() || row->fUris[0].empty())) {
			fLists[tab]->RemoveRow(row);
			delete row;
		}
	}
	fLists[tab]->AddRow(new DiscoverRow(values, uris, titles), 0);
	fLoadTime[tab] = system_time();
	_ScheduleCacheSave();
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
		if (row) {
			fLists[TAB_PLAYLISTS]->RemoveRow(row);
			delete row;
			_ScheduleCacheSave();
		}
		return;
	}

	std::string name = message->GetString("name", "");
	std::string owner = message->GetString("owner", "");
	bool writable = message->GetBool("writable", true);
	bool owned = message->GetBool("owned", row ? row->fOwned : false);
	if (operation == "rename") {
		if (row) {
			_UpdatePlaylistRow(row, name, "", row->fWritable, row->fOwned);
			_ScheduleCacheSave();
		} else
			ReloadPlaylists();
		return;
	}
	if (operation != "add" || name.empty())
		return;

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
	if (!message || !fLists[TAB_PLAYLISTS] || !fLoaded[TAB_PLAYLISTS])
		return;
	int32 generation = -1;
	if (message->FindInt32("generation", &generation) != B_OK
			|| generation != fPlaylistSyncGeneration)
		return;

	std::set<std::string> serverUris;
	for (int32 i = 0;; i++) {
		const char* uri = nullptr;
		if (message->FindString("uri", i, &uri) != B_OK)
			break;
		const char* name = "Unknown";
		const char* owner = "Spotify";
		message->FindString("name", i, &name);
		message->FindString("owner", i, &owner);
		bool writable = true;
		message->FindBool("writable", i, &writable);
		bool owned = false;
		message->FindBool("owned", i, &owned);

		std::string playlistUri = uri ? uri : "";
		if (playlistUri.empty())
			continue;
		std::string id = SpotifyItemKindForUri(playlistUri)
			== kSpotifyItemPlaylist
			? SpotifyItemIdForUri(playlistUri) : "";
		serverUris.insert(playlistUri);
		if (!id.empty() && fPendingPlaylistRemovals.find(id)
				!= fPendingPlaylistRemovals.end())
			continue;

		DiscoverRow* row = _FindPlaylistRow(playlistUri);
		if (row) {
			_UpdatePlaylistRow(row, name, owner, writable, owned);
		} else {
			fLists[TAB_PLAYLISTS]->AddRow(new DiscoverRow(
				{name, owner}, {playlistUri, ""}, {name, ""}, writable,
				owned));
		}
	}

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
	fLoadTime[TAB_PLAYLISTS] = system_time();
	fFreshSnapshot[TAB_PLAYLISTS] = true;
	fCacheBacked[TAB_PLAYLISTS] = false;
	_ScheduleCacheSave();
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


void
DiscoverWindow::_LoadTab(int32 tab, bool nextPage)
{
	if (tab < 0 || tab >= TAB_COUNT || !_IsTabEffectivelyVisible(tab)
			|| !fLists[tab])
		return;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api) return;

	if (nextPage && (tab != TAB_FOLLOWED_ARTISTS
			&& tab != TAB_SAVED_EPISODES && tab != TAB_AUDIOBOOKS))
		return;
	if (nextPage && (fPageLoading[tab] || !fPageHasMore[tab]))
		return;
	if (!nextPage) {
		fTabLoadGeneration[tab]++;
		fLoaded[tab] = true;
		fLoadTime[tab] = system_time();
		fPageLoading[tab] = false;
		fPageHasMore[tab] = true;
		fPageOffset[tab] = 0;
		fPageCursor[tab].clear();
	}

	BMessenger messenger(this);

	int32 loadGeneration = fTabLoadGeneration[tab];
	bool snapshot = !nextPage && tab != TAB_PLAYLISTS;
	auto send = [messenger, tab, snapshot, loadGeneration](
			const std::vector<RowData>& rows) {
		BMessage* msg = new BMessage('uRow');
		msg->AddInt32("tab",  tab);
		msg->AddInt32("load_generation", loadGeneration);
		msg->AddInt32("cols", rows.empty()
			? (int32)kTabCols[tab].size() : (int32)rows[0].vals.size());
		msg->AddBool("snapshot", snapshot);
		for (const auto& row : rows) {
			for (const auto& v : row.vals) msg->AddString("v", v.c_str());
			for (const auto& u : row.uris) msg->AddString("u", u.c_str());
			for (const auto& t : row.ttls) msg->AddString("t", t.c_str());
			msg->AddBool("writable", row.writable);
			msg->AddBool("owned", row.owned);
		}
		messenger.SendMessage(msg);
		delete msg;
	};

	switch (tab) {
		case TAB_PLAYLISTS:
		{
			send({{{ B_TRANSLATE("Liked Songs"), "Spotify"},
			       {"spotify:collection", ""},
			       {B_TRANSLATE("Liked Songs"), ""}, true, true}});
			ReloadPlaylists();
			break;
		}

		case TAB_TOP_TRACKS:
			api->Content().GetTopItems("tracks", 20, [send](bool ok,
					const nlohmann::json& data) {
				if (!ok || !data.contains("items")) return;
				std::vector<RowData> rows;
				for (const auto& item : data["items"]) {
					if (!item.is_object()) continue;
					std::string name = item.value("name", "Unknown");
					std::string aName = "Unknown", aUri;
					if (item.contains("artists") && item["artists"].is_array()
					        && !item["artists"].empty()) {
						aName = item["artists"][0].value("name", "Unknown");
						aUri  = item["artists"][0].value("uri", "");
					}
					rows.push_back({{name, aName},
					                {item.value("uri", ""), aUri},
					                {name, aName}});
				}
				send(rows);
			});
			break;

		case TAB_TOP_ARTISTS:
			api->Content().GetTopItems("artists", 20, [send](bool ok,
					const nlohmann::json& data) {
				if (!ok || !data.contains("items")) return;
				std::vector<RowData> rows;
				for (const auto& item : data["items"]) {
					if (!item.is_object()) continue;
					std::string name = item.value("name", "Unknown");
					std::string genre = "Artist";
					if (item.contains("genres") && item["genres"].is_array()
					        && !item["genres"].empty())
						genre = item["genres"][0].get<std::string>();
					rows.push_back({{name, genre},
					                {item.value("uri", ""), ""},
					                {name, ""}});
				}
				send(rows);
			});
			break;

		case TAB_NEW_RELEASES:
			api->Content().GetNewReleases(20, [send](bool ok,
					const nlohmann::json& data) {
				if (!ok || !data.contains("albums")
				        || !data["albums"].contains("items")) return;
				std::vector<RowData> rows;
				for (const auto& item : data["albums"]["items"]) {
					if (!item.is_object()) continue;
					std::string name = item.value("name", "Unknown");
					std::string aName = "Unknown", aUri;
					if (item.contains("artists") && item["artists"].is_array()
					        && !item["artists"].empty()) {
						aName = item["artists"][0].value("name", "Unknown");
						aUri  = item["artists"][0].value("uri", "");
					}
					rows.push_back({{name, aName},
					                {item.value("uri", ""), aUri},
					                {name, aName}});
				}
				send(rows);
			});
			break;

		case TAB_SAVED_ALBUMS:
			api->Library().GetSavedAlbums(20, [send](bool ok,
					const nlohmann::json& data) {
				if (!ok || !data.contains("items")) return;
				std::vector<RowData> rows;
				for (const auto& item : data["items"]) {
					if (!item.contains("album") || !item["album"].is_object()) continue;
					const auto& a = item["album"];
					std::string name = a.value("name", "Unknown");
					std::string aName = "Unknown", aUri;
					if (a.contains("artists") && a["artists"].is_array()
					        && !a["artists"].empty()) {
						aName = a["artists"][0].value("name", "Unknown");
						aUri  = a["artists"][0].value("uri", "");
					}
					rows.push_back({{name, aName},
					                {a.value("uri", ""), aUri},
					                {name, aName}});
				}
				send(rows);
			});
			break;

		case TAB_PODCASTS:
		{
			std::set<std::string> cachedAudiobookIds = fAudiobookIds;
			auto loadShows = [api, send, messenger, loadGeneration](
					const std::set<std::string>& audiobookIds, bool freshIds) {
				if (freshIds) {
					BMessage ids(kMsgAudiobookIdsUpdated);
					ids.AddInt32("load_generation", loadGeneration);
					for (const std::string& id : audiobookIds)
						ids.AddString("audiobook_id", id.c_str());
					messenger.SendMessage(&ids);
				}
				api->Library().GetSavedShows(20, [send, audiobookIds](bool ok,
						const nlohmann::json& data) {
					if (!ok || !data.contains("items")
							|| !data["items"].is_array()) return;
					std::vector<RowData> rows;
					for (const auto& item : data["items"]) {
						if (!item.is_object() || !item.contains("show")
								|| !item["show"].is_object()) continue;
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
					send(rows);
				});
			};
			api->Library().GetAllSavedAudiobooks(
				[loadShows, cachedAudiobookIds](bool ok,
						const nlohmann::json& books) {
				if (!ok || !books.is_array()) {
					loadShows(cachedAudiobookIds, false);
					return;
				}
				std::set<std::string> audiobookIds;
				for (const auto& book : books) {
					if (!book.is_object()
							|| JsonString(book, "type") != "audiobook") continue;
					std::string id = JsonString(book, "id");
					if (id.empty())
						id = SpotifyItemIdForUri(JsonString(book, "uri"));
					if (!id.empty())
						audiobookIds.insert(id);
				}
				loadShows(audiobookIds, true);
			});
			break;
		}

		case TAB_FOLLOWED_ARTISTS:
		{
			fPageLoading[tab] = true;
			std::string after = fPageCursor[tab];
			api->Artists().GetFollowedArtists(after, 50,
				[send, messenger, loadGeneration](bool ok,
						const nlohmann::json& data) {
					BMessage done(kMsgPageDone);
					done.AddInt32("tab", TAB_FOLLOWED_ARTISTS);
					done.AddInt32("load_generation", loadGeneration);
					if (!ok) {
						if (SpotifyResponseStatus(data) == 403)
							send({{{B_TRANSLATE("Permission to read followed artists is missing"),
								B_TRANSLATE("Reconnect Spotify in Settings")},
								{"", ""}, {"", ""}}});
						done.AddBool("has_more", false);
						messenger.SendMessage(&done);
						return;
					}
					if (!data.contains("artists")
							|| !data["artists"].is_object()
							|| !data["artists"].contains("items")
							|| !data["artists"]["items"].is_array()) {
						done.AddBool("has_more", false);
						messenger.SendMessage(&done);
						return;
					}
					std::vector<RowData> rows;
					for (const auto& item : data["artists"]["items"]) {
						if (!item.is_object()) continue;
						std::string name = item.contains("name")
							&& item["name"].is_string()
							? item["name"].get<std::string>() : "Unknown";
						std::string uri = item.contains("uri")
							&& item["uri"].is_string()
							? item["uri"].get<std::string>() : "";
						std::string genre = "Artist";
						if (item.contains("genres") && item["genres"].is_array()
								&& !item["genres"].empty()
								&& item["genres"][0].is_string())
							genre = item["genres"][0].get<std::string>();
						rows.push_back({{name, genre}, {uri, ""}, {name, ""}});
					}
					send(rows);
					std::string next;
					if (data["artists"].contains("cursors")
							&& data["artists"]["cursors"].is_object()) {
						const auto& cursors = data["artists"]["cursors"];
						// Spotify returns JSON null rather than an empty string on
						// the final cursor page. json::value<string>() throws for
						// that valid response and previously terminated the app.
						if (cursors.contains("after")
								&& cursors["after"].is_string())
							next = cursors["after"].get<std::string>();
					}
					if (next.empty() && rows.empty())
						send({{{B_TRANSLATE("No followed artists"), ""},
							{"", ""}, {"", ""}}});
					done.AddString("next_cursor", next.c_str());
					done.AddBool("has_more", !next.empty());
					messenger.SendMessage(&done);
				});
			break;
		}

		case TAB_SAVED_EPISODES:
		{
			HaifySettings accountSettings = SettingsController::Load();
			bool showProgress = accountSettings.grantedScopes.find(
				"user-read-playback-position") != std::string::npos;
			fPageLoading[tab] = true;
			int32 offset = fPageOffset[tab];
			api->Library().GetSavedEpisodes(offset, 50,
				[send, messenger, offset, showProgress, loadGeneration](bool ok,
						const nlohmann::json& data) {
					BMessage done(kMsgPageDone);
					done.AddInt32("tab", TAB_SAVED_EPISODES);
					done.AddInt32("load_generation", loadGeneration);
					if (!ok || !data.is_object() || !data.contains("items")
							|| !data["items"].is_array()) {
						done.AddBool("has_more", false);
						messenger.SendMessage(&done);
						return;
					}
					std::vector<RowData> rows;
					for (const auto& saved : data["items"]) {
						if (!saved.is_object() || !saved.contains("episode")
								|| !saved["episode"].is_object()) continue;
						const auto& episode = saved["episode"];
						if (JsonString(episode, "type") != "episode")
							continue;
						std::string showName, showUri;
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
						if (!PrimaryUriMatchesTab(TAB_SAVED_EPISODES,
								episodeUri))
							continue;
						std::string episodeName = JsonString(episode, "name");
						if (episodeName.empty())
							continue;
						std::string progress;
						if (showProgress && episode.contains("resume_point")
								&& episode["resume_point"].is_object()) {
							const auto& resume = episode["resume_point"];
							progress = JsonBool(resume, "fully_played")
								? B_TRANSLATE("Done")
								: DurationText(JsonInt32(resume,
									"resume_position_ms"));
						}
						rows.push_back({{episodeName, showName,
							JsonString(episode, "release_date"),
							DurationText(JsonInt32(episode, "duration_ms")), progress},
							{episodeUri, showUri, "", "", ""},
							{episodeName, showName, "", "", ""}});
					}
					send(rows);
					int32 count = (int32)data["items"].size();
					int32 total = JsonInt32(data, "total", offset + count);
					done.AddInt32("next_offset", offset + count);
					done.AddBool("has_more", count > 0
						&& offset + count < total);
					messenger.SendMessage(&done);
				});
			break;
		}

		case TAB_AUDIOBOOKS:
		{
			fPageLoading[tab] = true;
			int32 offset = fPageOffset[tab];
			api->Library().GetSavedAudiobooks(offset, 50,
				[send, messenger, offset, loadGeneration](bool ok,
						const nlohmann::json& data) {
					BMessage done(kMsgPageDone);
					done.AddInt32("tab", TAB_AUDIOBOOKS);
					done.AddInt32("load_generation", loadGeneration);
					if (!ok || !data.is_object() || !data.contains("items")
							|| !data["items"].is_array()) {
						done.AddBool("has_more", false);
						messenger.SendMessage(&done);
						return;
					}
					std::vector<RowData> rows;
					for (const auto& book : data["items"]) {
						if (!book.is_object()) continue;
						if (JsonString(book, "type") != "audiobook") continue;
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
					send(rows);
					int32 count = (int32)data["items"].size();
					int32 total = JsonInt32(data, "total", offset + count);
					done.AddInt32("next_offset", offset + count);
					done.AddBool("has_more", count > 0
						&& offset + count < total);
					messenger.SendMessage(&done);
				});
			break;
		}
	}
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
