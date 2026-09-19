#include "ui/windows/DiscoverWindow.h"
#include "ui/views/DiscoverListView.h"
#include "discover/DiscoverRowFactory.h"
#include "discover/DiscoverCacheRepository.h"
#include "discover/DiscoverMessages.h"
#include "discover/DiscoverLibraryRequests.h"
#include "discover/DiscoverPlaylistRequests.h"
#include "ui/drag/HaifyDragState.h"
#include "ui/dialogs/TextInputDialog.h"
#include "messages/Messages.h"
#include "messages/MessageContracts.h"
#include "settings/SettingsController.h"
#include "app/App.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"
#include "spotify/api/SpotifyResponse.h"
#include <nlohmann/json.hpp>
#include "ui/menus/TrackContextMenu.h"
#include "policy/UiLogic.h"

#include <Alert.h>
#include <Application.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
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
#include <cmath>
#include <cstring>
#include <cstdio>
#include <set>
#include <time.h>
#include <utility>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DiscoverWindow"

static const bigtime_t kDropTabSwitchDelay = 350000LL;

static const char* const kTabLabels[TAB_COUNT] = {
	B_TRANSLATE_MARK("Playlists"),
	B_TRANSLATE_MARK("Top Tracks"),
	B_TRANSLATE_MARK("Top Artists"),
	B_TRANSLATE_MARK("New Releases"),
	B_TRANSLATE_MARK("Saved Albums"),
	B_TRANSLATE_MARK("Podcasts"),
	B_TRANSLATE_MARK("Followed Artists"),
	B_TRANSLATE_MARK("Saved Episodes"),
	B_TRANSLATE_MARK("Audiobooks"),
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
HaifyDragButtonStillDown(BView* view, BPoint* currentWhere = nullptr)
{
	BMessage drag;
	if (!GetHaifyActiveDragMessage(drag) || !view)
		return false;

	BPoint where;
	uint32 buttons = 0;
	view->GetMouse(&where, &buttons, false);
	if (currentWhere)
		*currentWhere = where;
	return (buttons & B_PRIMARY_MOUSE_BUTTON) != 0;
}


static std::string
PlaylistUri(const std::string& id)
{
	return SpotifyItemKindForUri(id) == kSpotifyItemPlaylist
		? id : SpotifyUriForItemKind(kSpotifyItemPlaylist, id);
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



class DiscoverTabView : public BTabView {
public:
	DiscoverTabView() : BTabView("tabs", B_WIDTH_FROM_LABEL) {}

	void SetValidDropTargets(const std::vector<int32>& targets)
	{
		if (fValidDropTargets == targets)
			return;
		fValidDropTargets = targets;
		Invalidate();
	}

	void SetPendingDropTarget(int32 visual)
	{
		if (fPendingDropTarget == visual)
			return;
		fPendingDropTarget = visual;
		Invalidate();
	}

	void ClearDropTarget()
	{
		if (fValidDropTargets.empty() && fPendingDropTarget < 0)
			return;
		fValidDropTargets.clear();
		fPendingDropTarget = -1;
		Invalidate();
	}

	int32 VisualTabAt(BPoint where) const
	{
		return _TabAt(where);
	}

	int32 DropTargetTabAt(BPoint where) const
	{
		if (where.y < 0.0f || where.y > TabHeight() + 1.0f)
			return -1;
		return _TabAt(where);
	}

	virtual void Select(int32 tab) {
		BTabView::Select(tab);
		if (Window()) {
			BMessage msg(MSG_DISCOVER_TAB_SELECTED);
			msg.AddInt32(MessageFields::Tab, tab);
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
		if (dragMessage && dragMessage->what == MSG_DRAG_ITEM) {
			BPoint currentWhere;
			if (!HaifyDragButtonStillDown(this, &currentWhere)) {
				ClearHaifyActiveDragMessage();
				if (be_app)
					be_app->PostMessage(MSG_HAIFY_DRAG_ENDED);
				BTabView::MouseMoved(where, transit, dragMessage);
				return;
			}
			BPoint screen = currentWhere;
			ConvertToScreen(&screen);
			BMessage hover(*dragMessage);
			hover.what = MSG_DISCOVER_DRAG_HOVER;
			hover.AddPoint(MessageFields::ScreenPoint, screen);
			int32 visualTab = DropTargetTabAt(currentWhere);
			if (visualTab >= 0)
				hover.AddInt32(MessageFields::VisualTab, visualTab);
			if (Window())
				Window()->PostMessage(&hover);
			BTabView::MouseMoved(where, transit, dragMessage);
			return;
		}
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
		BMessage drag;
		if (GetHaifyActiveDragMessage(drag)) {
			ClearHaifyActiveDragMessage();
			if (be_app)
				be_app->PostMessage(MSG_HAIFY_DRAG_ENDED);
		}
		if (fDragging && fSource >= 0 && fTarget >= 0
				&& fSource != fTarget && Window()) {
			BMessage reorder(MSG_DISCOVER_TAB_MOVED);
			reorder.AddInt32(MessageFields::SourceVisualTab, fSource);
			reorder.AddInt32(MessageFields::TargetVisualTab, fTarget);
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
		for (int32 target : fValidDropTargets) {
			if (target < 0 || target >= CountTabs())
				continue;
			BRect frame = TabFrame(target);
			SetHighColor(tint_color(ui_color(B_CONTROL_HIGHLIGHT_COLOR),
				B_LIGHTEN_1_TINT));
			StrokeRect(frame.InsetByCopy(2, 2));
		}
		int32 target = fDragging ? fTarget : fPendingDropTarget;
		if (target >= 0 && target < CountTabs()) {
			BRect frame = TabFrame(target);
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
	int32 fPendingDropTarget = -1;
	std::vector<int32> fValidDropTargets;
	BPoint fStart;
	bool fDragging = false;
};

class DiscoverWindowDropFilter : public BMessageFilter {
public:
	explicit DiscoverWindowDropFilter(DiscoverWindow* window)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE), fWindow(window) {}

	filter_result Filter(BMessage* message, BHandler** target) override {
		if (!fWindow || !message || message->what != MSG_DRAG_ITEM
				|| !message->WasDropped())
			return B_DISPATCH_MESSAGE;
		if (target && fWindow->ForwardDroppedMessage(message, *target))
			return B_SKIP_MESSAGE;
		ClearHaifyActiveDragMessage();
		BMessage drop(*message);
		drop.what = MSG_DISCOVER_DROP;
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
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	memset(fTabs,         0, sizeof(fTabs));
	memset(fLists,        0, sizeof(fLists));
	memset(fTabMenuItems, 0, sizeof(fTabMenuItems));
	HaifySettings s = SettingsController::Load();
	fCache.SetAccountIfEmpty(s.spotifyAccountId);
	fAsync.Reset(s.spotifyAccountId, _AudiobooksEnabled());
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
	BMessage lazy(MSG_DISCOVER_CHECK_LAZY_LOAD);
	fLazyLoadRunner = new BMessageRunner(BMessenger(this), &lazy, 500000LL);
}


DiscoverWindow::~DiscoverWindow()
{
	delete fDropTabSwitchRunner;
	fDropTabSwitchRunner = nullptr;
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
	return fTabView ? fTabLayout.LogicalTab(visual) : TAB_NONE;
}


BColumnListView*
DiscoverWindow::_MakeList(int32 i)
{
	DiscoverListView* list = new DiscoverListView(kTabLabels[i],
		kTabCols[i], i, true);
	list->SetDropFeedbackFlags(i == TAB_PLAYLISTS ? kDropFeedbackTargetRow
		: kDropFeedbackNone);
	return list;
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

	fTabLayout = MakeDiscoverTabLayout(fTabOrder, fTabVisible, _AudiobooksEnabled());
	int32 visual = 0;
	for (DiscoverTab i : fTabLayout.tabs) {
		if (!fLists[i])
			fLists[i] = _MakeList(i);
		fTabView->AddTab(fLists[i], fTabs[i]);
		fTabs[i] = fTabView->TabAt(visual);
		fTabView->TabAt(visual)->SetLabel(B_TRANSLATE(kTabLabels[i]));
		visual++;
	}

	if (fTabView->CountTabs() > 0)
		fTabView->Select(fTabLayout.RestoreSelection(prevLogical));
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

	EnsureVisibleDiscoverTab(fTabVisible, _AudiobooksEnabled());
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
	fTabOrder = NormalizeDiscoverTabOrder(settings.discoverTabOrder);
}


void
DiscoverWindow::_SaveTabOrder(HaifySettings& settings) const
{
	settings.discoverTabOrder = DiscoverTabOrderIds(fTabOrder);
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
	return IsDiscoverTabVisible(logical, fTabVisible, _AudiobooksEnabled());
}


void
DiscoverWindow::_MoveTab(int32 sourceVisual, int32 targetVisual)
{
	if (!fTabView || !MoveDiscoverTab(fTabOrder,
			fTabLayout.LogicalTab(sourceVisual), fTabLayout.LogicalTab(targetVisual)))
		return;
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
	_QueueLibraryWrite({uri, DiscoverLibraryWriteKind::EnsureSaved, 0, true});
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

	DiscoverPlaylistRequests::AddItem(*api, SpotifyItemIdForUri(targetUri), itemUri,
		fAsync.Begin(TAB_PLAYLISTS), BMessenger(this));
	return true;
}


void
DiscoverWindow::_SelectLibraryTarget(const std::string& uri)
{
	DiscoverTab logical = DiscoverLibraryTargetTab(SpotifyItemKindForUri(uri),
		_AudiobooksEnabled());
	if (logical == TAB_NONE)
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
	int32 visual = _VisualTabForLogical(logical);
	if (visual >= 0)
		fTabView->Select(visual);
	// Selecting an untouched tab still needs its initial load. A tab that is
	// already populated must remain intact; the library delta adds its new row.
	if (!fCache.State(logical).loaded)
		_LoadTab(logical);
}


void
DiscoverWindow::MessageReceived(BMessage* message)
{
	_EnsureAsyncContext();
	auto context = DiscoverMessages::ReadContext(*message);
	if (context && !DiscoverMessages::ReadAsyncToken(*message) && !fAsync.Accepts(*context))
		return;
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
		case MSG_DISCOVER_TAB_TOGGLED:
			_ToggleTabVisibility(message);
			return true;

		case MSG_DISCOVER_TAB_MOVED:
			_MoveTab(message->GetInt32(MessageFields::SourceVisualTab, -1),
				message->GetInt32(MessageFields::TargetVisualTab, -1));
			return true;

		case MSG_DISCOVER_TAB_ORDER_RESET:
			_ResetTabOrder();
			return true;

		case MSG_SPOTIFY_CAPABILITIES_CHANGED:
			_ApplySpotifyCapabilities();
			return true;

		case MSG_DISCOVER_TAB_SELECTED:
			_SelectTab(message);
			return true;

		case MSG_DISCOVER_AUDIOBOOK_IDS:
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
		case MSG_DISCOVER_CACHE_LOADED:
		case MSG_DISCOVER_ROWS:
			_ApplyDiscoverRows(message);
			return true;

		case MSG_DISCOVER_CHECK_LAZY_LOAD:
			_CheckLazyLoad();
			return true;

		case MSG_DISCOVER_PAGE_DONE:
			_ApplyPageDone(message);
			return true;

		case MSG_DISCOVER_CACHE_SAVE:
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
		case MSG_QUEUE_ITEM:
			be_app->PostMessage(message);
			return true;
		case MSG_PLAY_URI:
			_ForwardPlayback(message);
			return true;

		case 'open':
			_ForwardOpenRequest(message);
			return true;

		case MSG_CURRENT_TRACK_UPDATE:
			_ApplyPlayingTrackUpdate(message);
			return true;

		case 'rClk':
			_ShowDiscoverContextMenu(message);
			return true;

		case 'iCmR':
			_ShowPlayableContextMenu(message);
			return true;

		case MSG_DISCOVER_MEMBERSHIP_CACHED:
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
	return _HandleDiscoverDropActionMessage(message)
		|| _HandleLibraryMutationMessage(message);
}


bool
DiscoverWindow::_HandleDiscoverDropActionMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_DISCOVER_DRAG_HOVER:
			_HandleDiscoverDragHover(message);
			return true;

		case MSG_DISCOVER_DRAG_EXIT:
			_ClearDropMarkers();
			return true;

		case MSG_HAIFY_DRAG_ENDED:
			_ClearDropMarkers();
			return true;

		case MSG_DISCOVER_DROP_TAB_SWITCH:
			if (message->GetInt32(MessageFields::Tab, -1) == fPendingDropTab
					&& HaifyActiveDragGenerationMatches(
						message->GetInt32(MessageFields::DragGeneration, -1))
					&& _IsPointerOverDropTargetTab(fPendingDropTab)) {
				_SelectDropTargetTab(fPendingDropTab);
				_CancelDropTabSwitch();
			} else {
				_ClearDropMarkers();
			}
			return true;

		case MSG_DISCOVER_DROP:
			_HandleDiscoverDrop(message);
			return true;

		case MSG_DISCOVER_PLAYLIST_DROP_RESULT:
			_ApplyPlaylistDropResult(message);
			return true;

		default:
			return false;
	}
}


bool
DiscoverWindow::_HandleLibraryMutationMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_DISCOVER_LIBRARY_WRITE_RESULT:
			_ApplyLibraryWriteResult(message);
			return true;

		case 'savA': case 'remA': case 'remI': case 'remL': case 'likT':
			_ApplyLibraryCommand(message);
			return true;

		case 'addP':
			if (_AcceptDialogContext(*message))
				_HandlePlaylistDrop(message->GetString(MessageFields::TrackUri, ""),
					"spotify:playlist:" + std::string(message->GetString(MessageFields::PlaylistId, "")), true);
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

		case MSG_DISCOVER_PLAYLIST_CREATE_RESULT:
			_ApplyPlaylistCreateResult(message);
			return true;

		case 'plRn':
			_ShowRenamePlaylistDialog(message);
			return true;

		case 'plRc':
			_RenamePlaylist(message);
			return true;

		case MSG_DISCOVER_PLAYLIST_MUTATION_RESULT:
			_ApplyPlaylistMutationResult(message);
			return true;

		case 'plDl':
			_DeletePlaylist(message);
			return true;


		case MSG_PLAYLISTS_CHANGED:
			_ApplyPlaylistsChanged(message);
			return true;

		case MSG_LIBRARY_CHANGED:
			_ApplyLibraryChanged(message);
			return true;

		case MSG_DISCOVER_LIBRARY_RESOLVED:
			_ApplyResolvedLibraryAddition(message);
			return true;

		case MSG_DISCOVER_PLAYLIST_SNAPSHOT:
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
	if (message->FindInt32(MessageFields::Tab, &tab) != B_OK
			|| !ToggleDiscoverTabVisibility(fTabVisible, tab, _AudiobooksEnabled()))
		return;
	fTabMenuItems[tab]->SetMarked(fTabVisible[tab]);
	SettingsController::Update([&](HaifySettings& settings) {
		_SaveTabVisibility(settings);
	});
	_RebuildTabs();
}


void
DiscoverWindow::_ResetTabOrder()
{
	fTabOrder = NormalizeDiscoverTabOrder({});
	SettingsController::Update([&](HaifySettings& settings) {
		_SaveTabOrder(settings);
	});
	_RebuildTabs();
}


void
DiscoverWindow::_ApplySpotifyCapabilities()
{
	EnsureVisibleDiscoverTab(fTabVisible, _AudiobooksEnabled());
	if (fTabMenuItems[TAB_PLAYLISTS])
		fTabMenuItems[TAB_PLAYLISTS]->SetMarked(fTabVisible[TAB_PLAYLISTS]);
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
	message->FindInt32(MessageFields::Tab, &visual);
	int32 logical = _LogicalTab(visual);
	if (logical < 0)
		return;
	_LoadPersistentCache(logical);
	if (fLists[logical])
		static_cast<DiscoverListView*>(fLists[logical])->SetPlayingUri(
			fCurrentTrackUri);
	bool expired = fCache.Expired(logical, system_time());
	if (expired && logical == TAB_PLAYLISTS) {
		ReloadPlaylists();
	} else if (!fCache.State(logical).loaded || expired) {
		if (expired)
			_InvalidateTabCache(logical);
		fCache.MarkUnloaded(logical);
		_LoadTab(logical);
	}
}


void
DiscoverWindow::_ApplyPageDone(BMessage* message)
{
	auto result = DiscoverMessages::ReadPageDone(*message);
	if (!result)
		return;
	if (fCache.FinishPage(result->tab, result->loadGeneration, result->hasMore,
			result->nextOffset.value_or(fCache.State(result->tab).pageOffset),
			result->nextCursor))
		_CheckLazyLoad();
}


void
DiscoverWindow::_ApplyDiscoverRows(BMessage* message)
{
	RowUpdateData update;
	if (!_ReadRowUpdate(message, update))
		return;
	if (message->GetBool(MessageFields::AudiobookIdsSnapshot, false))
		_ApplyAudiobookIdSnapshot(message);

	if (update.tab == TAB_AUDIOBOOKS && update.snapshotMessage) {
		fLibrary.SetAudiobookSnapshot({});
	}
	_ApplyRowUpdateRows(message, update);
	_PruneSnapshotRows(update);
	_ReorderSnapshotRows(update);
	_FinishRowUpdate(message, update);
}


bool
DiscoverWindow::_ReadRowUpdate(BMessage* message, RowUpdateData& update)
{
	if (message->FindInt32(MessageFields::Tab, &update.tab) != B_OK)
		return false;
	if (message->FindInt32(MessageFields::Columns, &update.cols) != B_OK || update.cols <= 0)
		return false;
	if (update.tab < 0 || update.tab >= TAB_COUNT || !fLists[update.tab])
		return false;
	DiscoverRowData columns;
	if (!DiscoverMessages::ReadRowColumns(*message, update.tab, columns))
		return false;
	update.allV = std::move(columns.vals);
	update.allU = std::move(columns.uris);
	update.allT = std::move(columns.ttls);
	update.nRows = (int32)update.allV.size() / update.cols;

	update.fromCache = message->GetBool(MessageFields::FromCache, false);
	update.snapshotMessage = message->GetBool(MessageFields::Snapshot, false);
	if (update.fromCache) {
		update.snapshotMessage = false;
		if (!_ApplyCacheRowUpdateStart(message, update))
			return false;
	} else if (!_ApplyFreshRowUpdateStart(message, update)) {
		return false;
	}
	return true;
}


bool
DiscoverWindow::_ApplyCacheRowUpdateStart(BMessage* message, RowUpdateData& update)
{
	DiscoverCacheReadRequest request{message->GetString(MessageFields::AccountId, ""),
		update.tab, message->GetInt32(MessageFields::CacheGeneration, -1)};
	update.cacheLast = message->GetBool(MessageFields::CacheLast, true);
	int32 selected = _LogicalTab(fTabView ? fTabView->Selection() : -1);
	if (!fCache.AcceptCacheBatch(request, message->GetBool(MessageFields::CacheAvailable, true),
			update.cacheLast, selected))
		return false;
	if (message->GetBool(MessageFields::CacheFirst, false))
		fLists[update.tab]->Clear();
	return true;
}


bool
DiscoverWindow::_ApplyFreshRowUpdateStart(BMessage* message, const RowUpdateData& update)
{
	int32 generation;
	std::optional<int32_t> supplied;
	if (message->FindInt32(MessageFields::LoadGeneration, &generation) == B_OK)
		supplied = generation;
	return fCache.AcceptFreshRows(update.tab, supplied);
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
	message->FindBool(MessageFields::Writable, rowIndex, &writable);
	bool owned = false;
	message->FindBool(MessageFields::Owned, rowIndex, &owned);
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
DiscoverWindow::_AcceptRowUpdatePrimaryUri(RowUpdateData& update, const std::string& uri)
{
	if (!fLibrary.AcceptPrimaryRow(update.tab, uri))
		return false;
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
		static_cast<DiscoverListView*>(fLists[update.tab])->SetPlayingUri(
			fCurrentTrackUri);
	if (update.tab == TAB_AUDIOBOOKS)
		_RemoveAudiobookDuplicatesFromPodcasts();
	fCache.CompleteRows(update.tab, update.fromCache, update.snapshotMessage);
	if (!update.fromCache)
		_ScheduleCacheSave();
	_CheckLazyLoad();
}


void
DiscoverWindow::_ForwardPlayback(BMessage* message)
{
	MessageContracts::PlayCommand command;
	if (!MessageContracts::ReadPlayCommand(*message, command))
		return;
	const char* uri = command.uri.c_str();
	if (SpotifyItemIsPlayable(SpotifyItemKindForUri(uri))) {
		if (fCurrentTrackUri != uri) {
			fCurrentTrackUri = uri;
			int32 tab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
			if (tab >= 0 && fLists[tab])
				static_cast<DiscoverListView*>(fLists[tab])->SetPlayingUri(
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
	message->FindString(MessageFields::Uri, &uri);
	message->FindString(MessageFields::Title, &title);
	if (!uri || !uri[0])
		return;

	BMessage forward('open');
	forward.AddString(MessageFields::Uri, uri);
	forward.AddString(MessageFields::Title, title ? title : "");
	const char* coverUrl = message->GetString("coverUrl", "");
	if (!coverUrl || !coverUrl[0])
		coverUrl = message->GetString("cover_url", "");
	if (coverUrl && coverUrl[0])
		forward.AddString("coverUrl", coverUrl);
	be_app->PostMessage(&forward);
}


void
DiscoverWindow::_ApplyPlayingTrackUpdate(BMessage* message)
{
	MessageContracts::CurrentTrackUpdate update;
	if (!MessageContracts::ReadCurrentTrackUpdate(*message, update))
		return;
	const std::string& uri = update.uri;
	if (fCurrentTrackUri == uri)
		return;
	fCurrentTrackUri = uri;
	int32 tab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
	if (tab >= 0 && fLists[tab])
		static_cast<DiscoverListView*>(fLists[tab])->SetPlayingUri(uri);
}


void
DiscoverWindow::_ShowDiscoverContextMenu(BMessage* message)
{
	const char* uri = nullptr;
	const char* title = nullptr;
	BPoint screen;
	if (message->FindString(MessageFields::Uri, &uri) != B_OK)
		return;
	message->FindString(MessageFields::Title, &title);
	if (message->FindPoint(MessageFields::ScreenPoint, &screen) != B_OK)
		return;
	int32 sourceTab = -1;
	message->FindInt32(MessageFields::Tab, &sourceTab);
	std::string uriString = uri;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();

	SpotifyItemKind kind = SpotifyItemKindForUri(uriString);
	if (SpotifyItemIsPlayable(kind)) {
		std::string context = sourceTab == TAB_SAVED_EPISODES
			? "spotify:saved-episodes" : "";
		bool saved = sourceTab == TAB_SAVED_EPISODES;
		auto state = fLibrary.KnownMembership(uriString);
		if (state)
			saved = *state;
		else
			_RequestPlayableLibraryState(uriString);
		BMessage commandContext;
		DiscoverMessages::AddContext(commandContext, fAsync.Context());
		::ShowPlayableItemContextMenu(uriString, context, screen,
			BMessenger(this), api, false, true, saved, &commandContext);
	} else if (kind == kSpotifyItemPlaylist) {
		_ShowPlaylistContextMenu(SpotifyItemIdForUri(uriString),
			message->GetBool(MessageFields::Owned, false), screen);
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
	if (api)
		DiscoverLibraryRequests::CheckMembership(*api, fLibrary.BeginMembership(uri), BMessenger(this));
}


void
DiscoverWindow::_ShowBrowsableItemContextMenu(const std::string& uri,
	const std::string& title, int32 sourceTab, BPoint screen)
{
	auto context = fAsync.Context();
	BPopUpMenu* menu = new BPopUpMenu("item", false, false);
	BMessage* openMsg = new BMessage('open');
	openMsg->AddString(MessageFields::Uri, uri.c_str());
	openMsg->AddString(MessageFields::Title, title.c_str());
	menu->AddItem(new BMenuItem(B_TRANSLATE("Open"), openMsg));

	BMessage* playMsg = new BMessage(MessageContracts::MakePlayCommand({uri.c_str()}));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Play"), playMsg));

	_AddAlbumContextActions(menu, uri, sourceTab);
	_AddLibraryRemovalContextAction(menu, uri, sourceTab);

	BMenuItem* selected = menu->Go(screen, false, true);
	if (selected && selected->Message()) {
		DiscoverMessages::AddContext(*selected->Message(), context);
		PostMessage(selected->Message());
	}
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
		removeMsg->AddString(MessageFields::Uri, uri.c_str());
		menu->AddItem(new BMenuItem(
			B_TRANSLATE("Remove from Saved Albums"), removeMsg));
	} else {
		BMessage* saveMsg = new BMessage('savA');
		saveMsg->AddString(MessageFields::Uri, uri.c_str());
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
	removeMsg->AddString(MessageFields::Uri, uri.c_str());
	const char* label = kind == kSpotifyItemShow
		? B_TRANSLATE("Unsubscribe") : kind == kSpotifyItemArtist
		? B_TRANSLATE("Unfollow Artist") : B_TRANSLATE("Remove from Audiobooks");
	menu->AddItem(new BMenuItem(label, removeMsg));
}


void
DiscoverWindow::_ShowPlayableContextMenu(BMessage* message)
{
	if (!_AcceptDialogContext(*message))
		return;
	BMessage commandContext;
	DiscoverMessages::AddContext(commandContext, fAsync.Context());
	App* app = dynamic_cast<App*>(be_app);
	ShowPlayableItemContextMenu(message->GetString(MessageFields::Uri, ""),
		message->GetString(MessageFields::ContextUri, ""),
		message->GetPoint("screen_point", BPoint()), BMessenger(this),
		app ? app->GetApi() : nullptr,
		message->GetBool("library_only", false), true,
		message->GetBool(MessageFields::Saved, false), &commandContext);
}


void
DiscoverWindow::_ApplyLibraryStateCached(BMessage* message)
{
	DiscoverLibraryRequest request{message->GetInt32(MessageFields::Tab, -1),
		message->GetString(MessageFields::Uri, ""), message->GetInt32(MessageFields::Generation, -1)};
	fLibrary.AcceptMembership(request, message->GetBool(MessageFields::Ok, false),
		message->GetBool(MessageFields::Saved, false));
}


void
DiscoverWindow::_HandleDiscoverDragHover(BMessage* message)
{
	MessageContracts::DragItem item;
	if (!MessageContracts::ReadDragItem(*message, item)) {
		_ClearDropMarkers();
		return;
	}
	int32 visualUnderMouse = message->GetInt32(MessageFields::VisualTab, -1);
	DiscoverDragHover hover = ResolveDiscoverDragHover(item, fTabVisible,
		_AudiobooksEnabled(), _LogicalTab(fTabView ? fTabView->Selection() : -1),
		_LogicalTab(visualUnderMouse));
	if (hover.target == TAB_NONE) {
		_ClearDropMarkers();
		return;
	}
	_SetValidDropTargetTab(hover.target);

	if (hover.scheduleTabSwitch)
		_ScheduleDropTabSwitch(hover.target);
	else
		_CancelDropTabSwitch();

	if (!hover.showRowMarker) {
		for (int32 i = 0; i < TAB_COUNT; i++) {
			if (DiscoverListView* list =
					dynamic_cast<DiscoverListView*>(fLists[i])) {
				list->ClearDropMarker();
			}
		}
		return;
	}

	BPoint screenWhere = message->GetPoint(MessageFields::ScreenPoint, BPoint(-1.0f, -1.0f));
	_UpdateDropMarkers(hover.target, screenWhere);
}


void
DiscoverWindow::_HandleDiscoverDrop(BMessage* message)
{
	_ClearDropMarkers();
	ClearHaifyActiveDragMessage();
	MessageContracts::DragItem item;
	if (!MessageContracts::ReadDragItem(*message, item))
		return;
	MessageContracts::DiscoverDropTarget target;
	if (!MessageContracts::ReadDiscoverDropTarget(*message, target))
		return;
	int32 targetTab = target.tab;
	DiscoverTab expectedTab = DiscoverDropTargetTab(item, _AudiobooksEnabled());
	if (expectedTab < 0 || targetTab != expectedTab)
		return;

	if (targetTab == TAB_PLAYLISTS) {
		if (!target.uri.empty())
			_HandlePlaylistDrop(item.uri, target.uri, target.writable);
		return;
	}

	_HandleLibraryDrop(item.uri);
}


bool
DiscoverWindow::_SelectDropTargetTab(int32 logicalTab)
{
	if (logicalTab < 0 || logicalTab >= TAB_COUNT || !fTabView)
		return false;
	if (!_IsTabEffectivelyVisible(logicalTab))
		return false;

	int32 selected = _LogicalTab(fTabView->Selection());
	if (selected == logicalTab)
		return true;
	int32 visual = _VisualTabForLogical(logicalTab);
	if (visual >= 0) {
		fTabView->Select(visual);
		return true;
	}
	return false;
}


int32
DiscoverWindow::_VisualTabForLogical(int32 logicalTab) const
{
	return fTabView ? fTabLayout.VisualTab(logicalTab) : -1;
}


bool
DiscoverWindow::_IsPointerOverDropTargetTab(int32 logicalTab) const
{
	DiscoverTabView* tabs = dynamic_cast<DiscoverTabView*>(fTabView);
	BMessage drag;
	if (!tabs || !GetHaifyActiveDragMessage(drag))
		return false;

	BPoint where;
	uint32 buttons = 0;
	tabs->GetMouse(&where, &buttons, false);
	if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0)
		return false;

	return _LogicalTab(tabs->DropTargetTabAt(where)) == logicalTab;
}


void
DiscoverWindow::_SetValidDropTargetTab(int32 logicalTab)
{
	DiscoverTabView* tabs = dynamic_cast<DiscoverTabView*>(fTabView);
	if (!tabs)
		return;
	std::vector<int32> targets;
	int32 visual = _VisualTabForLogical(logicalTab);
	if (visual >= 0)
		targets.push_back(visual);
	tabs->SetValidDropTargets(targets);
}


void
DiscoverWindow::_ScheduleDropTabSwitch(int32 logicalTab)
{
	if (_LogicalTab(fTabView ? fTabView->Selection() : -1) == logicalTab)
		return;
	if (fPendingDropTab == logicalTab && fDropTabSwitchRunner)
		return;
	int32 dragGeneration = HaifyActiveDragGeneration();
	if (dragGeneration < 0)
		return;
	_CancelDropTabSwitch();
	fPendingDropTab = logicalTab;
	if (DiscoverTabView* tabs = dynamic_cast<DiscoverTabView*>(fTabView))
		tabs->SetPendingDropTarget(_VisualTabForLogical(logicalTab));
	BMessage message(MSG_DISCOVER_DROP_TAB_SWITCH);
	message.AddInt32(MessageFields::Tab, logicalTab);
	message.AddInt32(MessageFields::DragGeneration, dragGeneration);
	fDropTabSwitchRunner = new BMessageRunner(BMessenger(this), &message,
		kDropTabSwitchDelay, 1);
}


void
DiscoverWindow::_CancelDropTabSwitch()
{
	delete fDropTabSwitchRunner;
	fDropTabSwitchRunner = nullptr;
	fPendingDropTab = -1;
	if (DiscoverTabView* tabs = dynamic_cast<DiscoverTabView*>(fTabView))
		tabs->SetPendingDropTarget(-1);
}


void
DiscoverWindow::_UpdateDropMarkers(int32 logicalTab, BPoint screenWhere)
{
	_SetValidDropTargetTab(logicalTab);
	for (int32 i = 0; i < TAB_COUNT; i++) {
		DiscoverListView* list = dynamic_cast<DiscoverListView*>(fLists[i]);
		if (!list)
			continue;
		if (i == logicalTab)
			list->UpdateDropTarget(screenWhere);
		else
			list->ClearDropMarker();
	}
}


void
DiscoverWindow::_ClearDropMarkers()
{
	_CancelDropTabSwitch();
	if (DiscoverTabView* tabs = dynamic_cast<DiscoverTabView*>(fTabView))
		tabs->ClearDropTarget();
	for (int32 i = 0; i < TAB_COUNT; i++) {
		DiscoverListView* list = dynamic_cast<DiscoverListView*>(fLists[i]);
		if (list)
			list->ClearDropMarker();
	}
}


void
DiscoverWindow::_ApplyPlaylistDropResult(BMessage* message)
{
	if (!_AcceptAsyncResult(*message))
			return;
	if (message->GetBool(MessageFields::Ok, false))
		return;
	const char* text = message->GetInt32(MessageFields::Status, -1) == 403
		? B_TRANSLATE("This playlist cannot be modified.")
		: B_TRANSLATE("Spotify could not add this item to the playlist.");
	BAlert* alert = new BAlert("", text, B_TRANSLATE("OK"), nullptr, nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
}


void
DiscoverWindow::_ApplyLibraryCommand(BMessage* message)
{
	if (!_AcceptDialogContext(*message))
		return;
	auto command = DiscoverMessages::ReadLibraryCommand(*message);
	if (command)
		_QueueLibraryWrite(*command);
}


void
DiscoverWindow::_QueueLibraryWrite(const DiscoverLibraryWriteRequest& command)
{
	App* app = dynamic_cast<App*>(be_app);
	if (!app || !app->GetApi())
		return;
	auto plan = fLibraryWrites.Queue(command.uri, command.kind,
		command.selectTarget, _AudiobooksEnabled());
	if (plan.dispatch)
		_DispatchLibraryWrite(*plan.dispatch);
}


void
DiscoverWindow::_DispatchLibraryWrite(const DiscoverLibraryWriteRequest& request)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	int32 tab = DiscoverLibraryChangeController::TargetTab(request.uri);
	if (tab == TAB_NONE)
		tab = TAB_PLAYLISTS;
	auto token = fAsync.Begin(tab);
	if (api) {
		DiscoverLibraryRequests::Write(*api, request, token, BMessenger(this));
	} else {
		BMessage failure = DiscoverMessages::OperationResult(
			MSG_DISCOVER_LIBRARY_WRITE_RESULT, token, false, -1);
		DiscoverMessages::AddLibraryWrite(failure, request);
		PostMessage(&failure);
	}
}


void
DiscoverWindow::_ApplyLibraryWriteResult(BMessage* message)
{
	auto request = DiscoverMessages::ReadLibraryWrite(*message);
	if (!request || !_AcceptAsyncResult(*message))
		return;
	auto plan = fLibraryWrites.Complete(*request, message->GetBool(MessageFields::Ok, false),
		message->GetBool(MessageFields::Saved, false));
	if (!plan.accepted)
		return;
	if (plan.confirmed) {
		BMessage changed = DiscoverMessages::LibraryChanged(*plan.confirmed, fAsync.Context().accountId);
		be_app->PostMessage(&changed);
	}
	if (plan.selectTarget)
		_SelectLibraryTarget(request->uri);
	if (plan.dispatch)
		_DispatchLibraryWrite(*plan.dispatch);
	if (plan.failed) {
		const char* text = request->kind == DiscoverLibraryWriteKind::EnsureSaved
			? B_TRANSLATE("Spotify could not check this item's library status.")
			: request->kind == DiscoverLibraryWriteKind::Remove
			? B_TRANSLATE("Spotify could not remove this item from your library.")
			: B_TRANSLATE("Spotify could not add this item to your library.");
		(new BAlert("", text, B_TRANSLATE("OK"), nullptr, nullptr,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go();
	}
}

void
DiscoverWindow::_PlayTrackFromMessage(BMessage* message)
{
	const char* trackUri = message->GetString(MessageFields::TrackUri, "");
	if (!*trackUri)
		return;
	BMessage play = MessageContracts::MakePlayCommand({trackUri});
	be_app->PostMessage(&play);
}


void
DiscoverWindow::_ShowNewPlaylistDialog()
{
	BMessage confirm('plNc');
	DiscoverMessages::AddContext(confirm, fAsync.Context());
	TextInputDialog* dialog = new TextInputDialog(
		B_TRANSLATE("New Playlist"), B_TRANSLATE("Name:"), "",
		BMessenger(this), confirm);
	dialog->Show();
}


void
DiscoverWindow::_CreatePlaylist(BMessage* message)
{
	if (!_AcceptDialogContext(*message))
		return;
	std::string name = message->GetString(MessageFields::Name, "");
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (name.empty() || !api)
		return;
	DiscoverPlaylistRequests::Create(*api, name, fAsync.Begin(TAB_PLAYLISTS), BMessenger(this));
}


void
DiscoverWindow::_ApplyPlaylistCreateResult(BMessage* message)
{
	if (!_AcceptAsyncResult(*message))
		return;
	if (!message->GetBool(MessageFields::Ok, false)) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not create the playlist."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return;
	}
	std::string id = message->GetString(MessageFields::Id, "");
	DiscoverPlaylistChange change;
	if (!id.empty()) {
		change = {DiscoverChangeOperation::Add, PlaylistUri(id),
			message->GetString(MessageFields::Name, "Unknown"),
			message->GetString(MessageFields::Owner, "Spotify"), true, true};
	}
	BMessage changed = DiscoverMessages::PlaylistChanged(change, fAsync.Context().accountId);
	be_app->PostMessage(&changed);
}


void
DiscoverWindow::_ShowRenamePlaylistDialog(BMessage* message)
{
	if (!_AcceptDialogContext(*message))
		return;
	const char* id = message->GetString(MessageFields::Id, "");
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
	DiscoverMessages::AddContext(confirm, fAsync.Context());
	confirm.AddString(MessageFields::Id, id);
	TextInputDialog* dialog = new TextInputDialog(
		B_TRANSLATE("Rename Playlist"), B_TRANSLATE("Name:"),
		currentName.c_str(), BMessenger(this), confirm);
	dialog->Show();
}


void
DiscoverWindow::_RenamePlaylist(BMessage* message)
{
	if (!_AcceptDialogContext(*message))
		return;
	_QueuePlaylistMutation(DiscoverChangeOperation::Rename,
		message->GetString(MessageFields::Id, ""), message->GetString(MessageFields::Name, ""));
}


void
DiscoverWindow::_ApplyPlaylistMutationResult(BMessage* message)
{
	if (!_AcceptAsyncResult(*message))
		return;
	std::string id = message->GetString(MessageFields::Id, "");
	auto plan = fPlaylistMutations.CompleteMutation(id,
		message->GetInt32(MessageFields::Generation, -1), message->GetBool(MessageFields::Ok, false));
	if (!plan.accepted)
		return;
	_RenderPlaylistMutation(id, plan);
	if (plan.confirmed) {
		BMessage changed = DiscoverMessages::PlaylistChanged(*plan.confirmed, fAsync.Context().accountId);
		be_app->PostMessage(&changed);
	}
	if (plan.dispatch)
		_DispatchPlaylistMutation(*plan.dispatch);
	else
		ReloadPlaylists();
	_ScheduleCacheSave();
	if (plan.failed) {
		const char* text = DiscoverOperation(message->GetString(MessageFields::Operation, ""))
			== DiscoverChangeOperation::Rename ? B_TRANSLATE("Spotify could not rename the playlist.")
			: B_TRANSLATE("Spotify could not remove the playlist.");
		(new BAlert("", text, B_TRANSLATE("OK"), nullptr, nullptr,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go();
	}
}


void
DiscoverWindow::_DeletePlaylist(BMessage* message)
{
	if (!_AcceptDialogContext(*message))
		return;
	const char* id = message->GetString(MessageFields::Id, "");
	bool owned = message->GetBool(MessageFields::Owned, false);
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
	_EnsureAsyncContext();
	if (!_AcceptDialogContext(*message))
		return;
	_QueuePlaylistMutation(DiscoverChangeOperation::Remove, id);
}

void
DiscoverWindow::_ApplyPlaylistsChanged(BMessage* message)
{
	if (!MessageContracts::MatchesAccount(*message, fAsync.Context().accountId))
		return;
	const char* operation = nullptr;
	if (message->FindString(MessageFields::Operation, &operation) == B_OK)
		_ApplyPlaylistChange(message);
	else
		_ReloadTab(TAB_PLAYLISTS);
}


void
DiscoverWindow::_ApplyLibraryChanged(BMessage* message)
{
	if (!MessageContracts::MatchesAccount(*message, fAsync.Context().accountId))
		return;
	const char* operation = nullptr;
	const char* uri = nullptr;
	if (message->FindString(MessageFields::Operation, &operation) == B_OK
			&& message->FindString(MessageFields::Uri, &uri) == B_OK) {
		fLibrary.ObserveMembership({DiscoverOperation(operation), uri});
		_ApplyLibraryChange(message);
	} else {
		int32 tab = _LogicalTab(fTabView ? fTabView->Selection() : -1);
		if (tab == TAB_PLAYLISTS)
			ReloadPlaylists();
		else if (tab >= 0) {
			_InvalidateTabCache(tab);
			fCache.MarkUnloaded(tab);
			_LoadTab(tab);
		}
	}
}


void
DiscoverWindow::_ReloadTabFromMessage(BMessage* message)
{
	int32 tab = -1;
	if (message->FindInt32(MessageFields::Tab, &tab) == B_OK)
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
		BMessage* msg = new BMessage(MSG_DISCOVER_TAB_TOGGLED);
		msg->AddInt32(MessageFields::Tab, i);
		fTabMenuItems[i] = new BMenuItem(B_TRANSLATE(kTabLabels[i]), msg);
		fTabMenuItems[i]->SetMarked(fTabVisible[i]);
		if (i == TAB_AUDIOBOOKS)
			fTabMenuItems[i]->SetEnabled(_AudiobooksEnabled());
		viewMenu->AddItem(fTabMenuItems[i]);
	}
	viewMenu->AddSeparatorItem();
	viewMenu->AddItem(new BMenuItem(B_TRANSLATE("Reset Tab Order"),
		new BMessage(MSG_DISCOVER_TAB_ORDER_RESET)));
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
			|| !_IsTabEffectivelyVisible(tab) || !fCache.CanRead(tab))
		return;
	DiscoverCacheRepository::LoadAsync(BMessenger(this), fCache.BeginRead(tab));
}


void
DiscoverWindow::_ScheduleCacheSave()
{
	if (fCache.AccountId().empty()) {
		HaifySettings settings = SettingsController::Load();
		if (settings.spotifyAccountId.empty())
			return;
		fCache.SetAccountIfEmpty(settings.spotifyAccountId);
	}
	delete fCacheSaveRunner;
	BMessage save(MSG_DISCOVER_CACHE_SAVE);
	fCacheSaveRunner = new BMessageRunner(BMessenger(this), &save,
		750000LL, 1);
}


static DiscoverRowData
CapturePlaylistRow(DiscoverRow* row)
{
	DiscoverRowData data;
	for (int32 column = 0; column < 2; column++) {
		BStringField* field = dynamic_cast<BStringField*>(row->GetField(column));
		data.vals.emplace_back(field ? field->String() : "");
		data.uris.push_back(column < (int32)row->fUris.size() ? row->fUris[column] : "");
		data.ttls.push_back(column < (int32)row->fTitles.size() ? row->fTitles[column] : "");
	}
	data.writable = row->fWritable;
	data.owned = row->fOwned;
	return data;
}

static std::vector<DiscoverRowData>
CaptureDiscoverRows(BColumnListView* list, int32 tab)
{
	std::vector<DiscoverRowData> rows;
	for (int32 index = 0; index < list->CountRows(); index++) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(list->RowAt(index));
		size_t columns = kTabCols[tab].size();
		if (!row || row->fUris.size() < columns)
			continue;
		DiscoverRowData data;
		data.writable = row->fWritable;
		data.owned = row->fOwned;
		for (size_t column = 0; column < columns; column++) {
			BStringField* field = dynamic_cast<BStringField*>(row->GetField((int32)column));
			data.vals.emplace_back(field ? field->String() : "");
			data.uris.push_back(row->fUris[column]);
			data.ttls.push_back(column < row->fTitles.size() ? row->fTitles[column] : "");
		}
		rows.push_back(std::move(data));
	}
	return rows;
}

void
DiscoverWindow::_WriteCacheNow()
{
	if (fCache.AccountId().empty())
		return;
	DiscoverCacheSnapshot snapshot;
	snapshot.accountId = fCache.AccountId();
	snapshot.savedAt = time(nullptr);
	if (fLibrary.AudiobookIdsKnown())
		snapshot.audiobookIds = fLibrary.AudiobookIds();
	for (int32 tab = 0; tab < TAB_COUNT; tab++) {
		if (fCache.State(tab).invalidated)
			snapshot.invalidatedTabs.insert(tab);
		if (tab == TAB_PLAYLISTS && !fPlaylistMutations.CanPersist())
			continue;
		if (fLists[tab] && fCache.ShouldPersist(tab))
			snapshot.tabs[tab] = CaptureDiscoverRows(fLists[tab], tab);
	}
	DiscoverCacheRepository::WriteAsync(snapshot);
}


void
DiscoverWindow::LoadData()
{
	_WriteCacheNow();
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	std::string account = api ? api->AccountId() : fCache.AccountId();
	fAsync.Reset(account, _AudiobooksEnabled());
	fCache.Reset(account);
	fPlaylistMutations.Reset();
	for (auto& pending : fPendingPlaylistRemovals)
		delete pending.second.row;
	fPendingPlaylistRemovals.clear();
	fLibrary.Reset();
	fLibraryWrites.Reset();
	for (int i = 0; i < TAB_COUNT; i++) {
		if (fLists[i]) fLists[i]->Clear();
	}
	int32 logical = _LogicalTab(fTabView ? fTabView->Selection() : 0);
	if (logical >= 0) {
		_LoadPersistentCache(logical);
		_LoadTab(logical);
	}
}


void
DiscoverWindow::_EnsureAsyncContext()
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	std::string account = api ? api->AccountId() : fCache.AccountId();
	if (!fAsync.Matches(account, _AudiobooksEnabled()))
		LoadData();
}


bool
DiscoverWindow::_AcceptAsyncResult(const BMessage& message)
{
	auto token = DiscoverMessages::ReadAsyncToken(message);
	if (!token)
		return false;
	// Transport cancellation cannot undo an already submitted remote write.
	bool mayHaveChanged = message.GetBool(MessageFields::Ok, false)
		|| message.GetInt32(MessageFields::Status, 0) == -1;
	auto completed = fAsync.Complete(*token, mayHaveChanged);
	if (completed.disposition == DiscoverAsyncDisposition::Reconcile) {
		for (int32 tab : DiscoverAffectedTabs(completed.tab))
			_ReloadTab(tab);
	}
	return completed.disposition == DiscoverAsyncDisposition::Current;
}


bool
DiscoverWindow::_AcceptDialogContext(const BMessage& message) const
{
	auto context = DiscoverMessages::ReadContext(message);
	return context && fAsync.Accepts(*context);
}


void
DiscoverWindow::_ReloadTab(int32 tab)
{
	if (tab < 0 || tab >= TAB_COUNT)
		return;
	_InvalidateTabCache(tab);
	_ScheduleCacheSave();
	if (!_IsTabEffectivelyVisible(tab) || !fLists[tab]) {
		fCache.MarkUnloaded(tab);
		return;
	}
	if (tab == TAB_PLAYLISTS) {
		if (fCache.State(tab).loaded)
			ReloadPlaylists();
		else
			_LoadTab(tab);
		return;
	}
	fCache.MarkUnloaded(tab);
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
	DiscoverLibraryChange change{DiscoverOperation(message->GetString(MessageFields::Operation, "")),
		message->GetString(MessageFields::Uri, "")};
	int32 tab = DiscoverLibraryChangeController::TargetTab(change.uri);
	if (tab < 0)
		return;
	auto plan = fLibrary.ApplyChange(change, fCache.State(tab).loaded,
		_FindRow(tab, change.uri) != nullptr, fCache.State(TAB_PODCASTS).loaded);
	if (plan.request.tab == TAB_NONE)
		return;
	bool reload = fCache.State(tab).loaded;
	_InvalidateTabCache(tab);
	if (plan.removeRow)
		_ApplyLibraryRemoval(tab, change.uri);
	if (plan.resolveAddition)
		_ResolveLibraryAddition(tab, change.uri, plan.request.generation);
	if (plan.removePodcastDuplicates)
		_RemoveAudiobookDuplicatesFromPodcasts();
	if (plan.invalidatePodcasts)
		_InvalidateTabCache(TAB_PODCASTS);
	_ScheduleCacheSave();
	if (reload)
		_ReloadTab(tab);
	if (plan.refreshPodcasts)
		_ReloadTab(TAB_PODCASTS);
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
	if (fCache.State(tab).loaded)
		fCache.Touch(tab, system_time());
}


void
DiscoverWindow::_ApplyAudiobookIdSnapshot(BMessage* message)
{
	if (!message || (message->GetBool(MessageFields::FromCache, false)
			&& fLibrary.AudiobookIdsKnown()))
		return;
	int32 generation = -1;
	if (message->FindInt32(MessageFields::LoadGeneration, &generation) == B_OK
			&& generation != fCache.State(TAB_PODCASTS).loadGeneration) {
		return;
	}
	std::set<std::string> ids;
	const char* value = nullptr;
	for (int32 index = 0;
			message->FindString(MessageFields::AudiobookId, index, &value) == B_OK; index++) {
		if (value && value[0])
			ids.insert(value);
	}
	fLibrary.SetAudiobookSnapshot(std::move(ids));
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
		if (!fLibrary.IsAudiobook(id)) {
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
DiscoverWindow::_ResolveLibraryAddition(int32 tab, const std::string& uri, int32 generation)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;
	HaifySettings settings = SettingsController::Load();
	bool showProgress = settings.grantedScopes.find("user-read-playback-position") != std::string::npos;
	DiscoverLibraryRequests::ResolveAddition(*api, {tab, uri, generation}, showProgress,
		B_TRANSLATE("Done"), BMessenger(this));
}


void
DiscoverWindow::_ApplyResolvedLibraryAddition(BMessage* message)
{
	if (!message)
		return;
	int32 tab = message->GetInt32(MessageFields::Tab, -1);
	int32 generation = message->GetInt32(MessageFields::Generation, -1);
	std::string uri = message->GetString(MessageFields::Uri, "");
	if (!fLibrary.AcceptsAddition({tab, uri, generation}))
		return;
	fLibrary.CompleteAddition({tab, uri, generation});
	if (!message->GetBool(MessageFields::Ok, false)) {
		std::fprintf(stderr, "Haify: discover library row resolution failed (%ld, API %s)\n",
			(long)message->GetInt32(MessageFields::Status, 0),
			message->GetBool(MessageFields::ApiOk, false) ? "response malformed" : "request failed");
		return;
	}
	if (!fLists[tab] || !fCache.State(tab).loaded || _FindRow(tab, uri))
		return;

	auto row = DiscoverMessages::ReadResolvedLibraryRow(*message, tab, uri);
	if (!row)
		return;

	_RemoveEmptyRows(tab);
	fLists[tab]->AddRow(new DiscoverRow(row->vals, row->uris, row->ttls,
		row->writable, row->owned), 0);
	fCache.Touch(tab, system_time());
	_ScheduleCacheSave();
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
DiscoverWindow::_QueuePlaylistMutation(DiscoverChangeOperation operation,
	const std::string& id, const std::string& name)
{
	App* app = dynamic_cast<App*>(be_app);
	if (!app || !app->GetApi())
		return;
	DiscoverRow* row = _FindPlaylistRow(PlaylistUri(id));
	std::optional<DiscoverRowData> existing;
	if (row)
		existing = CapturePlaylistRow(row);
	auto plan = fPlaylistMutations.QueueMutation(operation, id, name, existing);
	if (!plan.accepted)
		return;
	_InvalidateTabCache(TAB_PLAYLISTS);
	_RenderPlaylistMutation(id, plan);
	if (plan.dispatch)
		_DispatchPlaylistMutation(*plan.dispatch);
}


void
DiscoverWindow::_DispatchPlaylistMutation(const DiscoverPlaylistMutationRequest& request)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	auto token = fAsync.Begin(TAB_PLAYLISTS);
	if (api) {
		DiscoverPlaylistRequests::Mutate(*api, request, token, BMessenger(this));
	} else {
		BMessage failure = DiscoverMessages::OperationResult(
			MSG_DISCOVER_PLAYLIST_MUTATION_RESULT, token, false, -1);
		failure.AddString(MessageFields::Id, request.id.c_str());
		failure.AddInt32(MessageFields::Generation, request.generation);
		failure.AddString(MessageFields::Operation, DiscoverOperationName(request.operation));
		PostMessage(&failure);
	}
}


void
DiscoverWindow::_DetachPlaylistRow(const std::string& id)
{
	if (fPendingPlaylistRemovals.count(id))
		return;
	DiscoverRow* row = _FindPlaylistRow(PlaylistUri(id));
	if (!row)
		return;
	auto* list = fLists[TAB_PLAYLISTS];
	fPendingPlaylistRemovals[id] = {row, list->IndexOf(row), list->CurrentSelection() == row};
	list->RemoveRow(row);
}


DiscoverRow*
DiscoverWindow::_RestorePlaylistRow(const std::string& id)
{
	auto found = fPendingPlaylistRemovals.find(id);
	if (found == fPendingPlaylistRemovals.end())
		return _FindPlaylistRow(PlaylistUri(id));
	auto pending = found->second;
	fPendingPlaylistRemovals.erase(found);
	auto* list = fLists[TAB_PLAYLISTS];
	list->AddRow(pending.row, std::max(int32(0), std::min(pending.index, list->CountRows())));
	if (pending.selected)
		list->AddToSelection(pending.row);
	return pending.row;
}


void
DiscoverWindow::_DiscardDetachedPlaylistRow(const std::string& id)
{
	auto found = fPendingPlaylistRemovals.find(id);
	if (found == fPendingPlaylistRemovals.end())
		return;
	delete found->second.row;
	fPendingPlaylistRemovals.erase(found);
}


void
DiscoverWindow::_RenderPlaylistMutation(const std::string& id,
	const DiscoverPlaylistMutationPlan& plan)
{
	if (!fLists[TAB_PLAYLISTS])
		return;
	if (plan.row) {
		DiscoverRow* row = _RestorePlaylistRow(id);
		_AddOrUpdatePlaylistRow(row, PlaylistUri(id), plan.row->vals[0], plan.row->vals[1],
			plan.row->writable, plan.row->owned);
	} else if (plan.pending) {
		_DetachPlaylistRow(id);
	} else {
		_DiscardDetachedPlaylistRow(id);
		_RemovePlaylistRow(_FindPlaylistRow(PlaylistUri(id)));
	}
	static_cast<DiscoverListView*>(fLists[TAB_PLAYLISTS])->SetPlayingUri(fCurrentTrackUri);
}


void
DiscoverWindow::_ApplyPlaylistChange(BMessage* message)
{
	if (!message)
		return;
	auto change = DiscoverMessages::ReadPlaylistChange(*message);
	if (!change)
		return;
	_InvalidateTabCache(TAB_PLAYLISTS);
	_ScheduleCacheSave();
	fPlaylistMutations.InvalidateSnapshot();
	if (!fLists[TAB_PLAYLISTS] || !fCache.State(TAB_PLAYLISTS).loaded)
		return;
	fCache.Touch(TAB_PLAYLISTS, system_time());
	DiscoverRow* row = _FindPlaylistRow(change->uri);
	std::optional<DiscoverRowData> existing;
	if (row)
		existing = CapturePlaylistRow(row);
	auto plan = fPlaylistMutations.PlanChange(*change, existing);
	switch (plan.action) {
		case DiscoverPlaylistRowAction::Remove:
			_DiscardDetachedPlaylistRow(SpotifyItemIdForUri(change->uri));
			_RemovePlaylistRow(row);
			break;
		case DiscoverPlaylistRowAction::Upsert:
			_AddOrUpdatePlaylistRow(row, change->uri, plan.row.vals[0],
				plan.row.vals[1], plan.row.writable, plan.row.owned);
			break;
		case DiscoverPlaylistRowAction::Reload:
			break;
		case DiscoverPlaylistRowAction::Ignore:
			break;
	}
	ReloadPlaylists();
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
	fPlaylistMutations.InvalidateSnapshot();
	auto plan = fPlaylistMutations.PlanSnapshot(CaptureDiscoverRows(fLists[TAB_PLAYLISTS], TAB_PLAYLISTS),
		DiscoverMessages::ReadPlaylistSnapshot(*message));
	for (const auto& data : plan.upserts) {
		DiscoverRow* row = _FindPlaylistRow(data.uris[0]);
		if (row)
			_UpdatePlaylistRow(row, data.vals[0], data.vals[1], data.writable, data.owned);
		else
			fLists[TAB_PLAYLISTS]->AddRow(new DiscoverRow(data.vals, data.uris, data.ttls,
				data.writable, data.owned));
	}
	for (const auto& uri : plan.removals)
		_RemovePlaylistRow(_FindPlaylistRow(uri));
	_ReorderPlaylistRows(plan.order);
	static_cast<DiscoverListView*>(fLists[TAB_PLAYLISTS])->SetPlayingUri(fCurrentTrackUri);
	fCache.Touch(TAB_PLAYLISTS, system_time());
	fCache.CompleteRows(TAB_PLAYLISTS, false, true);
	_ScheduleCacheSave();
}


void
DiscoverWindow::_ReorderPlaylistRows(const std::vector<std::string>& order)
{
	auto* list = fLists[TAB_PLAYLISTS];
	int32 target = 0;
	for (const auto& uri : order) {
		DiscoverRow* row = _FindPlaylistRow(uri);
		if (!row)
			continue;
		if (list->IndexOf(row) != target) {
			bool selected = list->CurrentSelection() == row;
			list->RemoveRow(row);
			list->AddRow(row, target);
			if (selected)
				list->AddToSelection(row);
		}
		target++;
	}
}


bool
DiscoverWindow::_CanApplyPlaylistSnapshot(BMessage* message) const
{
	if (!message || !fLists[TAB_PLAYLISTS] || !fCache.State(TAB_PLAYLISTS).loaded)
		return false;
	int32 generation = -1;
	return message->FindInt32(MessageFields::Generation, &generation) == B_OK
		&& fPlaylistMutations.AcceptsSnapshot(generation);
}


void
DiscoverWindow::ReloadPlaylists()
{
	if (!fLists[TAB_PLAYLISTS] || !fCache.State(TAB_PLAYLISTS).loaded)
		return;
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;

	std::string accountId = api->AccountId();
	api->Playlists().InvalidatePlaylists();
	int32 generation = fPlaylistMutations.BeginSnapshot();
	BMessenger self(this);
	api->Playlists().GetPlaylists([self, accountId, generation](bool ok,
			const nlohmann::json& data) {
		if (!ok)
			return;
		auto rows = DiscoverRowFactory::PlaylistRows(data, accountId);
		if (!rows)
			return;
		BMessage snapshot = DiscoverMessages::PlaylistSnapshot(*rows, generation);
		self.SendMessage(&snapshot);
	});
}


void
DiscoverWindow::_InvalidateTabCache(int32 tab)
{
	fCache.Invalidate(tab);
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;
	switch (tab) {
		case TAB_PLAYLISTS: api->Playlists().InvalidatePlaylists(); break;
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
	if (!fCache.State(tab).loaded || fCache.State(tab).pageLoading || !fCache.State(tab).pageHasMore
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


static void
SendDiscoverTabRows(BMessenger messenger, int32 tab, bool snapshot,
	int32 loadGeneration, const std::vector<DiscoverRowData>& rows)
{
	BMessage message(MSG_DISCOVER_ROWS);
	message.AddInt32(MessageFields::Tab, tab);
	message.AddInt32(MessageFields::LoadGeneration, loadGeneration);
	message.AddInt32(MessageFields::Columns, DiscoverTabColumnCount(tab));
	message.AddBool(MessageFields::Snapshot, snapshot);
	DiscoverMessages::AppendRows(message, rows, 0, rows.size());
	messenger.SendMessage(&message);
}

static void
SendDiscoverPageDone(BMessenger messenger, int32 tab, int32 loadGeneration,
	const std::string& nextCursor, bool hasMore,
	std::optional<int32_t> nextOffset = std::nullopt)
{
	BMessage done = DiscoverMessages::PageDone(
		{tab, loadGeneration, hasMore, nextOffset, nextCursor});
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
	BMessage ids(MSG_DISCOVER_AUDIOBOOK_IDS);
	ids.AddInt32(MessageFields::LoadGeneration, loadGeneration);
	for (const std::string& id : audiobookIds)
		ids.AddString(MessageFields::AudiobookId, id.c_str());
	messenger.SendMessage(&ids);
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
	std::vector<DiscoverRowData> rows = DiscoverRowFactory::FollowedArtistRows(data);
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

static void
HandleSavedEpisodesResponse(bool ok, const nlohmann::json& data,
	BMessenger messenger, int32 offset, bool showProgress, bool snapshot,
	int32 loadGeneration)
{
	if (!ok) {
		SendDiscoverPageDone(messenger, TAB_SAVED_EPISODES, loadGeneration, "",
			false);
		return;
	}
	auto rows = DiscoverRowFactory::SavedEpisodeRows(data, showProgress,
		B_TRANSLATE("Done"));
	if (!rows) {
		SendDiscoverPageDone(messenger, TAB_SAVED_EPISODES, loadGeneration, "",
			false);
		return;
	}
	SendDiscoverTabRows(messenger, TAB_SAVED_EPISODES, snapshot,
		loadGeneration, *rows);
	// Paging counts source items, including entries the mapper cannot display.
	int32 count = (int32)data["items"].size();
	int32 total = JsonInt32(data, "total", offset + count);
	auto page = AdvanceDiscoverPage(offset, count, total);
	SendDiscoverPageDone(messenger, TAB_SAVED_EPISODES, loadGeneration, "",
		page.hasMore, page.nextOffset);
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
	std::vector<DiscoverRowData> rows = DiscoverRowFactory::AudiobookRows(data);
	SendDiscoverTabRows(messenger, TAB_AUDIOBOOKS, snapshot, loadGeneration,
		rows);
	int32 count = (int32)data["items"].size();
	int32 total = JsonInt32(data, "total", offset + count);
	auto page = AdvanceDiscoverPage(offset, count, total);
	SendDiscoverPageDone(messenger, TAB_AUDIOBOOKS, loadGeneration, "",
		page.hasMore, page.nextOffset);
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
	return !nextPage || fCache.CanLoadNext(tab);
}


void
DiscoverWindow::_PrepareLoadTab(int32 tab, bool nextPage)
{
	fCache.PrepareLoad(tab, nextPage, system_time());
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
				loadGeneration, DiscoverRowFactory::TopTrackRows(data));
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
				loadGeneration, DiscoverRowFactory::TopArtistRows(data));
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
				loadGeneration, DiscoverRowFactory::NewReleaseRows(data));
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
				loadGeneration, DiscoverRowFactory::SavedAlbumRows(data));
	});
}


void
DiscoverWindow::_LoadPodcastsTab(SpotifyApi* api, const BMessenger& messenger,
	bool, int32 loadGeneration)
{
	std::set<std::string> cachedAudiobookIds = fLibrary.AudiobookIds();
	auto loadShows = [api, messenger, loadGeneration](
			const std::set<std::string>& audiobookIds, bool freshIds) {
		if (freshIds)
			SendAudiobookIdsSnapshot(messenger, loadGeneration, audiobookIds);
		api->Library().GetSavedShows(20,
			[messenger, audiobookIds, loadGeneration](bool ok,
					const nlohmann::json& data) {
			if (ok) {
				SendDiscoverTabRows(messenger, TAB_PODCASTS, true,
					loadGeneration, DiscoverRowFactory::PodcastRows(data, audiobookIds));
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
	fCache.BeginPage(TAB_FOLLOWED_ARTISTS);
	std::string after = fCache.State(TAB_FOLLOWED_ARTISTS).pageCursor;
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
	fCache.BeginPage(TAB_SAVED_EPISODES);
	int32 offset = fCache.State(TAB_SAVED_EPISODES).pageOffset;
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
	fCache.BeginPage(TAB_AUDIOBOOKS);
	int32 offset = fCache.State(TAB_AUDIOBOOKS).pageOffset;
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
	(this->*loaders[tab])(api, messenger, snapshot, fCache.State(tab).loadGeneration);
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
		renMsg->AddString(MessageFields::Id, playlistId.c_str());
		DiscoverMessages::AddContext(*renMsg, fAsync.Context());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Rename" B_UTF8_ELLIPSIS), renMsg));
	}

	BMessage* delMsg = new BMessage('plDl');
	delMsg->AddString(MessageFields::Id,    playlistId.c_str());
	DiscoverMessages::AddContext(*delMsg, fAsync.Context());
	delMsg->AddBool(MessageFields::Owned, owned);
	menu->AddItem(new BMenuItem(
		owned ? B_TRANSLATE("Delete Playlist") : B_TRANSLATE("Unfollow Playlist"),
		delMsg));

	BMenuItem* sel = menu->Go(screen, false, true);
	if (sel) PostMessage(sel->Message());
	delete menu;
}
