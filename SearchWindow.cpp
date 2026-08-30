#include "SearchWindow.h"
#include "App.h"
#include "Messages.h"
#include "SettingsController.h"
#include "DiscoverListView.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"
#include "UiLogic.h"
#include <nlohmann/json.hpp>

#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <GroupView.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>
#include <Catalog.h>
#include <cstring>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SearchWindow"

static const uint32 kMsgSearch   = 'srch';
static const uint32 kMsgChkAll   = 'ckAl';
static const uint32 kMsgChkType  = 'ckTy';
static const uint32 kMsgResults  = 'sRes';

static std::string
SearchJsonString(const nlohmann::json& object, const char* key,
	const char* fallback = "")
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_string())
		return fallback;
	return object[key].get<std::string>();
}

static void
ShowSearchItemContextMenu(const std::string& uri, const std::string& title,
	bool saved, BPoint screenPoint, BMessenger target, SpotifyApi* api)
{
	SpotifyItemKind kind = SpotifyItemKindForUri(uri);
	if (kind == kSpotifyItemUnknown) return;
	BPopUpMenu* menu = new BPopUpMenu("searchItem", false, false);
	if (SpotifyItemIsPlayable(kind)) {
		BMessage* play = new BMessage('play');
		play->AddString("uri", uri.c_str());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Play"), play));
		if (kind == kSpotifyItemEpisode) {
			BMessage* open = new BMessage('open');
			open->AddString("uri", uri.c_str());
			open->AddString("title", title.c_str());
			menu->AddItem(new BMenuItem(B_TRANSLATE("Open Details"), open));
		}
		if (api) {
			BMessage* queue = new BMessage('sQue');
			queue->AddString("uri", uri.c_str());
			menu->AddItem(new BMenuItem(B_TRANSLATE("Add to Queue"), queue));
		}
	} else {
		BMessage* open = new BMessage('open');
		open->AddString("uri", uri.c_str());
		open->AddString("title", title.c_str());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Open"), open));
	}

	menu->AddSeparatorItem();
	const char* libraryLabel = "";
	switch (kind) {
		case kSpotifyItemTrack:
			libraryLabel = saved ? B_TRANSLATE("Remove from Liked Songs")
				: B_TRANSLATE("Add to Liked Songs"); break;
		case kSpotifyItemEpisode:
			libraryLabel = saved ? B_TRANSLATE("Remove from Saved Episodes")
				: B_TRANSLATE("Add to Saved Episodes"); break;
		case kSpotifyItemAlbum:
			libraryLabel = saved ? B_TRANSLATE("Remove from Saved Albums")
				: B_TRANSLATE("Add to Saved Albums"); break;
		case kSpotifyItemShow:
			libraryLabel = saved ? B_TRANSLATE("Remove from Podcasts")
				: B_TRANSLATE("Add to Podcasts"); break;
		case kSpotifyItemArtist:
			libraryLabel = saved ? B_TRANSLATE("Remove from Followed Artists")
				: B_TRANSLATE("Add to Followed Artists"); break;
		case kSpotifyItemAudiobook:
			libraryLabel = saved ? B_TRANSLATE("Remove from Audiobooks")
				: B_TRANSLATE("Add to Audiobooks"); break;
		case kSpotifyItemPlaylist:
			libraryLabel = saved ? B_TRANSLATE("Remove from Playlists")
				: B_TRANSLATE("Add to Playlists"); break;
		default: break;
	}
	BMessage* library = new BMessage(saved ? 'sRem' : 'sAdd');
	library->AddString("uri", uri.c_str());
	menu->AddItem(new BMenuItem(libraryLabel, library));

	if (api && SpotifyItemCanAddToPlaylist(kind)) {
		auto playlists = api->Playlists().GetCachedPlaylists();
		if (!playlists.empty()) {
			BMenu* addMenu = new BMenu(B_TRANSLATE("Add to Playlist"));
			for (const auto& playlist : playlists) {
				BMessage* add = new BMessage('sPlA');
				add->AddString("uri", uri.c_str());
				add->AddString("playlist_id", playlist.first.c_str());
				addMenu->AddItem(new BMenuItem(playlist.second.c_str(), add));
			}
			menu->AddItem(addMenu);
		}
	}

	BMenuItem* selected = menu->Go(screenPoint, false, true);
	if (selected && selected->Message()) {
		BMessage* message = selected->Message();
		if (message->what == 'sQue' && api) {
			api->Playback().AddToQueue(uri, nullptr);
		} else if ((message->what == 'sAdd' || message->what == 'sRem') && api) {
			bool add = message->what == 'sAdd';
			auto complete = [target, add, uri](bool ok, const nlohmann::json&) {
				BMessage result('sAct');
				result.AddBool("ok", ok);
				result.AddBool("add", add);
				result.AddString("uri", uri.c_str());
				target.SendMessage(&result);
			};
			if (add) api->Library().SaveLibraryItems({uri}, complete);
			else api->Library().RemoveLibraryItems({uri}, complete);
		} else if (message->what == 'sPlA' && api) {
			api->Playlists().AddTrackToPlaylist(
				message->GetString("playlist_id", ""), uri,
				[target](bool ok, const nlohmann::json&) {
					BMessage result('sPlR');
					result.AddBool("ok", ok);
					target.SendMessage(&result);
				});
		} else {
			target.SendMessage(message);
		}
	}
	delete menu;
}


SearchWindow::SearchWindow()
	: BWindow(BRect(200, 150,
		200 + kDefaultSearchWindowWidth,
		150 + kDefaultSearchWindowHeight), B_TRANSLATE("Search"),
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	HaifySettings s = SettingsController::Load();
	if (s.searchWindowW > 0) {
		MoveTo(s.searchWindowX, s.searchWindowY);
		ResizeTo(s.searchWindowW, s.searchWindowH);
	}
	_InitLayout();

	if (s.searchFilterAll) {
		_SetAllMode();
	} else {
		fChkAll->SetValue(B_CONTROL_OFF);
		fChkTracks->SetValue(s.searchFilterTracks ? B_CONTROL_ON : B_CONTROL_OFF);
		fChkArtists->SetValue(s.searchFilterArtists ? B_CONTROL_ON : B_CONTROL_OFF);
		fChkAlbums->SetValue(s.searchFilterAlbums ? B_CONTROL_ON : B_CONTROL_OFF);
		fChkPlaylists->SetValue(s.searchFilterPlaylists ? B_CONTROL_ON : B_CONTROL_OFF);
		fChkShows->SetValue(s.searchFilterShows ? B_CONTROL_ON : B_CONTROL_OFF);
		fChkEpisodes->SetValue(s.searchFilterEpisodes ? B_CONTROL_ON : B_CONTROL_OFF);
		fChkAudiobooks->SetValue(s.searchFilterAudiobooks ? B_CONTROL_ON : B_CONTROL_OFF);
	}
	_UpdateCapabilityFilters();
	_EnsureValidSelection();
}


bool
SearchWindow::QuitRequested()
{
	App* app = dynamic_cast<App*>(be_app);
	BRect f = Frame();
	SettingsController::Update([&](HaifySettings& s) {
		s.searchWindowX = f.left;  s.searchWindowY = f.top;
		s.searchWindowW = f.Width(); s.searchWindowH = f.Height();
		s.searchFilterAll       = fChkAll->Value()       == B_CONTROL_ON;
		s.searchFilterTracks    = fChkTracks->Value()    == B_CONTROL_ON;
		s.searchFilterArtists   = fChkArtists->Value()   == B_CONTROL_ON;
		s.searchFilterAlbums    = fChkAlbums->Value()    == B_CONTROL_ON;
		s.searchFilterPlaylists = fChkPlaylists->Value() == B_CONTROL_ON;
		s.searchFilterShows     = fChkShows->Value()     == B_CONTROL_ON;
		s.searchFilterEpisodes  = fChkEpisodes->Value()  == B_CONTROL_ON;
		s.searchFilterAudiobooks = fChkAudiobooks->Value() == B_CONTROL_ON
			&& !fChkAudiobooks->IsHidden();
		if (!(app && app->IsQuitting()))
			s.searchWindowOpen = false;
	});
	return true;
}


void
SearchWindow::_InitLayout()
{
	fSearchBar = new BTextControl("searchBar",
		nullptr, "", nullptr);
	fSearchBar->SetExplicitMinSize(BSize(200, B_SIZE_UNSET));

	BButton* searchBtn = new BButton("searchBtn",
		B_TRANSLATE("Search"), new BMessage(kMsgSearch));
	SetDefaultButton(searchBtn);


	fChkAll        = new BCheckBox("all",        B_TRANSLATE("All"),              new BMessage(kMsgChkAll));
	fChkTracks     = new BCheckBox("tracks",     B_TRANSLATE("Songs"),            new BMessage(kMsgChkType));
	fChkArtists    = new BCheckBox("artists",    B_TRANSLATE("Artists"),          new BMessage(kMsgChkType));
	fChkAlbums     = new BCheckBox("albums",     B_TRANSLATE("Albums"),           new BMessage(kMsgChkType));
	fChkPlaylists  = new BCheckBox("playlists",  B_TRANSLATE("Playlists"),        new BMessage(kMsgChkType));
	fChkShows      = new BCheckBox("shows",      B_TRANSLATE("Podcasts & Shows"), new BMessage(kMsgChkType));
	fChkEpisodes   = new BCheckBox("episodes",   B_TRANSLATE("Episodes"),         new BMessage(kMsgChkType));
	fChkAudiobooks = new BCheckBox("audiobooks", B_TRANSLATE("Audiobooks"),       new BMessage(kMsgChkType));

	fChkAll->SetValue(B_CONTROL_ON);

	fStatusLabel = new BStringView("status", "");

	fList = new DiscoverListView("SearchList", {
		{ B_TRANSLATE("Name"),   280, kColRouteOnDouble },
		{ B_TRANSLATE("Type"),    80, kColNone         },
		{ B_TRANSLATE("Artist"), 160, kColOpenOnDouble },
		{ B_TRANSLATE("Album"),  160, kColOpenOnDouble },
	}, -1, true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fSearchBar, 1.0f)
			.Add(searchBtn, 0.0f)
		.End()

		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING, 0,
				B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fChkAll)
			.Add(fChkTracks)
			.Add(fChkArtists)
			.Add(fChkAlbums)
			.Add(fChkPlaylists)
			.Add(fChkShows)
			.Add(fChkEpisodes)
			.Add(fChkAudiobooks)
			.AddGlue()
		.End()

		.AddGroup(B_HORIZONTAL, 0)
			.SetInsets(B_USE_DEFAULT_SPACING, 0,
				B_USE_DEFAULT_SPACING, 2)
			.Add(fStatusLabel)
			.AddGlue()
		.End()
		.Add(fList, 1.0f)
	.End();

	SetSizeLimits(400, 100000, 250, 100000);
}


void
SearchWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSearch:
			_DoSearch();
			break;

		case kMsgChkAll:
			if (!fUpdatingAll)
				_SetAllMode();
			break;

		case kMsgChkType:
			_ApplyTypeSelection();
			break;

		case MSG_SPOTIFY_CAPABILITIES_CHANGED:
			_ApplyCapabilityChange();
			break;

		case kMsgResults:
			_ApplyResults(message);
			break;

		case 'pStU':
			_ApplyPlayingTrack(message);
			break;

		case 'open':
			_ForwardOpen(message);
			break;

		case 'rClk':
			_PrepareContextMenu(message);
			break;

		case 'sCmR':
			_ShowContextMenu(message);
			break;

		case 'sAct':
			_ApplyLibraryActionResult(message);
			break;

		case 'sPlR':
			_ApplyPlaylistActionResult(message);
			break;

		case 'tply':
			_PlayTrackFromMessage(message);
			break;

		case 'play':
			_ForwardPlay(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
SearchWindow::_ApplyTypeSelection()
{
	if (fUpdatingAll)
		return;

	fUpdatingAll = true;
	fChkAll->SetValue(B_CONTROL_OFF);
	fUpdatingAll = false;
	_EnsureValidSelection();
}


void
SearchWindow::_ApplyCapabilityChange()
{
	_UpdateCapabilityFilters();
	_EnsureValidSelection();
}


void
SearchWindow::_ApplyResults(BMessage* message)
{
	if (message->GetInt32("generation", -1) != fSearchGeneration)
		return;

	fList->Clear();
	int32 count = 0;
	message->FindInt32("count", &count);
	if (count < 0) {
		fStatusLabel->SetText(B_TRANSLATE(
			"Search failed - check connection or sign in."));
		return;
	}

	const char* name;
	int32 index = 0;
	while (message->FindString("name", index, &name) == B_OK) {
		const char* type = message->FindString("type", index);
		const char* artist = message->FindString("artist", index);
		const char* album = message->FindString("album", index);
		const char* uri = message->FindString("uri", index);
		const char* artistUri = message->FindString("artUri", index);
		const char* albumUri = message->FindString("albUri", index);

		std::string nameStr = name ? name : "";
		std::string typeStr = type ? type : "";
		std::string artistStr = artist ? artist : "";
		std::string albumStr = album ? album : "";
		std::string uriStr = uri ? uri : "";
		std::string artistUriStr = artistUri ? artistUri : "";
		std::string albumUriStr = albumUri ? albumUri : "";

		fList->AddRow(new DiscoverRow(
			{ nameStr, typeStr, artistStr, albumStr },
			{ uriStr, "", artistUriStr, albumUriStr },
			{ nameStr, "", artistStr, albumStr }
		));
		index++;
	}
	if (!fCurrentTrackUri.empty())
		((DiscoverListView*)fList)->SetPlayingUri(fCurrentTrackUri);

	char buf[64];
	snprintf(buf, sizeof(buf), B_TRANSLATE("%d result(s)"), count);
	fStatusLabel->SetText(buf);
}


void
SearchWindow::_ApplyPlayingTrack(BMessage* message)
{
	const char* uri = nullptr;
	if (message->FindString("trackUri", &uri) == B_OK && uri) {
		fCurrentTrackUri = uri;
		((DiscoverListView*)fList)->SetPlayingUri(uri);
	}
}


void
SearchWindow::_ForwardOpen(BMessage* message)
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
SearchWindow::_PrepareContextMenu(BMessage* message)
{
	const char* uri = nullptr;
	const char* title = nullptr;
	BPoint screenPt;
	if (message->FindString("uri", &uri) != B_OK)
		return;
	message->FindString("title", &title);
	if (message->FindPoint("screenPt", &screenPt) != B_OK)
		return;
	if (!uri || !uri[0])
		return;

	std::string uriStr = uri;
	SpotifyItemKind kind = SpotifyItemKindForUri(uriStr);
	if (kind == kSpotifyItemUnknown)
		return;
	if (kind == kSpotifyItemAudiobook && !_AudiobooksEnabled()) {
		fStatusLabel->SetText(B_TRANSLATE(
			"Audiobooks are not available for this account or market."));
		return;
	}

	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;

	BMessenger self(this);
	std::string titleStr = title ? title : "";
	api->Library().CheckLibraryItems({uriStr}, [self, uriStr, titleStr,
			screenPt](bool ok, const nlohmann::json& data) {
		BMessage result('sCmR');
		result.AddString("uri", uriStr.c_str());
		result.AddString("title", titleStr.c_str());
		result.AddPoint("screen_point", screenPt);
		result.AddBool("known", ok);
		result.AddBool("saved", ok && data.is_array()
			&& !data.empty() && data[0].is_boolean()
			&& data[0].get<bool>());
		self.SendMessage(&result);
	});
}


void
SearchWindow::_ShowContextMenu(BMessage* message)
{
	if (!message->GetBool("known", false)) {
		fStatusLabel->SetText(B_TRANSLATE(
			"Spotify could not check this item's library status."));
		return;
	}

	std::string uri = message->GetString("uri", "");
	if (SpotifyItemKindForUri(uri) == kSpotifyItemAudiobook
			&& !_AudiobooksEnabled()) {
		fStatusLabel->SetText(B_TRANSLATE(
			"Audiobooks are not available for this account or market."));
		return;
	}

	App* app = dynamic_cast<App*>(be_app);
	ShowSearchItemContextMenu(uri,
		message->GetString("title", ""),
		message->GetBool("saved", false),
		message->GetPoint("screen_point", BPoint()), BMessenger(this),
		app ? app->GetApi() : nullptr);
}


void
SearchWindow::_ApplyLibraryActionResult(BMessage* message)
{
	bool ok = message->GetBool("ok", false);
	fStatusLabel->SetText(ok
		? (message->GetBool("add", false)
			? B_TRANSLATE("Added to your Spotify library.")
			: B_TRANSLATE("Removed from your Spotify library."))
		: B_TRANSLATE("Spotify could not update your library."));
	if (!ok)
		return;

	BMessage changed(MSG_LIBRARY_CHANGED);
	changed.AddString("operation",
		message->GetBool("add", false) ? "add" : "remove");
	changed.AddString("uri", message->GetString("uri", ""));
	be_app->PostMessage(&changed);
}


void
SearchWindow::_ApplyPlaylistActionResult(BMessage* message)
{
	bool ok = message->GetBool("ok", false);
	fStatusLabel->SetText(ok
		? B_TRANSLATE("Added to playlist.")
		: B_TRANSLATE("Spotify could not update the playlist."));
}


void
SearchWindow::_PlayTrackFromMessage(BMessage* message)
{
	const char* uri = message->GetString("trackUri", "");
	if (!*uri)
		return;

	BMessage forward('play');
	forward.AddString("uri", uri);
	be_app->PostMessage(&forward);
}


void
SearchWindow::_ForwardPlay(BMessage* message)
{
	const char* uri = nullptr;
	if (message->FindString("uri", &uri) != B_OK || !uri)
		return;

	BMessage forward('play');
	forward.AddString("uri", uri);
	be_app->PostMessage(&forward);
}


std::string
SearchWindow::_BuildTypeParam() const
{
	struct SearchTypeControl {
		const BCheckBox* checkbox;
		const char* type;
	};

	std::string types;
	auto add = [&](const char* t) {
		if (!types.empty()) types += ',';
		types += t;
	};
	bool all = fChkAll->Value() == B_CONTROL_ON;
	const SearchTypeControl controls[] = {
		{ fChkTracks, "track" },
		{ fChkArtists, "artist" },
		{ fChkAlbums, "album" },
		{ fChkPlaylists, "playlist" },
		{ fChkShows, "show" },
		{ fChkEpisodes, "episode" }
	};
	for (const SearchTypeControl& control : controls) {
		if (all || control.checkbox->Value() == B_CONTROL_ON)
			add(control.type);
	}
	if (!fChkAudiobooks->IsHidden()
			&& (all || fChkAudiobooks->Value() == B_CONTROL_ON))
		add("audiobook");
	return types.empty() ? "track" : types;
}


void
SearchWindow::_SetAllMode()
{
	std::vector<bool> selections(7, false);
	bool all = NormalizeSearchFilters(true, selections);
	fUpdatingAll = true;
	fChkAll->SetValue(all ? B_CONTROL_ON : B_CONTROL_OFF);
	fChkTracks->SetValue(selections[0] ? B_CONTROL_ON : B_CONTROL_OFF);
	fChkArtists->SetValue(selections[1] ? B_CONTROL_ON : B_CONTROL_OFF);
	fChkAlbums->SetValue(selections[2] ? B_CONTROL_ON : B_CONTROL_OFF);
	fChkPlaylists->SetValue(selections[3] ? B_CONTROL_ON : B_CONTROL_OFF);
	fChkShows->SetValue(selections[4] ? B_CONTROL_ON : B_CONTROL_OFF);
	fChkEpisodes->SetValue(selections[5] ? B_CONTROL_ON : B_CONTROL_OFF);
	fChkAudiobooks->SetValue(selections[6] ? B_CONTROL_ON : B_CONTROL_OFF);
	fUpdatingAll = false;
}


void
SearchWindow::_EnsureValidSelection()
{
	if (fChkAll->Value() == B_CONTROL_ON)
		return;
	std::vector<bool> selections = {
		fChkTracks->Value() == B_CONTROL_ON,
		fChkArtists->Value() == B_CONTROL_ON,
		fChkAlbums->Value() == B_CONTROL_ON,
		fChkPlaylists->Value() == B_CONTROL_ON,
		fChkShows->Value() == B_CONTROL_ON,
		fChkEpisodes->Value() == B_CONTROL_ON,
		!fChkAudiobooks->IsHidden()
			&& fChkAudiobooks->Value() == B_CONTROL_ON
	};
	if (NormalizeSearchFilters(false, selections))
		_SetAllMode();
}


bool
SearchWindow::_AudiobooksEnabled() const
{
	App* app = dynamic_cast<App*>(be_app);
	return app && app->GetCapabilities()
		&& app->GetCapabilities()->AudiobooksEnabled();
}


void
SearchWindow::_UpdateCapabilityFilters()
{
	bool enabled = _AudiobooksEnabled();
	if (enabled) {
		if (fChkAudiobooks->IsHidden())
			fChkAudiobooks->Show();
	} else {
		fChkAudiobooks->SetValue(B_CONTROL_OFF);
		if (!fChkAudiobooks->IsHidden())
			fChkAudiobooks->Hide();
	}
}


void
SearchWindow::_DoSearch()
{
	if (!fSearchBar) return;
	std::string query = fSearchBar->Text();
	if (query.empty()) return;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api) return;

	fStatusLabel->SetText(B_TRANSLATE("Searching…"));
	int32 generation = ++fSearchGeneration;

	std::string types = _BuildTypeParam();
	BMessenger self(this);

	api->Content().Search(query, types,
		[self, generation](bool ok, const nlohmann::json& data) {
			if (!ok) {
				BMessage err(kMsgResults);
				err.AddInt32("generation", generation);
				err.AddInt32("count", -1);
				self.SendMessage(&err);
				return;
			}

			BMessage* msg = new BMessage(kMsgResults);
			msg->AddInt32("generation", generation);
			int32 count = 0;

			auto addItems = [&](const char* key, const char* typeLabel) {
				if (!data.contains(key)) return;
				const auto& section = data[key];
				if (!section.is_object() || !section.contains("items")
						|| !section["items"].is_array()) return;
				for (const auto& item : section["items"]) {
					if (!item.is_object()) continue;
					std::string name = SearchJsonString(item, "name");
					std::string uri = SearchJsonString(item, "uri");
					if (std::string(key) == "audiobooks"
							&& item.contains("id") && item["id"].is_string()) {
						uri = SpotifyUriForItemKind(kSpotifyItemAudiobook,
							item["id"].get<std::string>());
					}
					std::string artist, artUri, album, albUri;

					if (item.contains("artists") && item["artists"].is_array()
							&& !item["artists"].empty()
							&& item["artists"][0].is_object()) {
						artist = SearchJsonString(item["artists"][0], "name");
						artUri = SearchJsonString(item["artists"][0], "uri");
					}
					if (item.contains("album") && item["album"].is_object()) {
						album  = SearchJsonString(item["album"], "name");
						albUri = SearchJsonString(item["album"], "uri");
					}

					if (item.contains("publisher") && item["publisher"].is_string()) {
						artist = item["publisher"].get<std::string>();
						artUri = uri;
					}
					if (item.contains("show") && item["show"].is_object()) {
						artist = SearchJsonString(item["show"], "name");
						artUri = SearchJsonString(item["show"], "uri");
					}
					if (item.contains("authors") && item["authors"].is_array()
							&& !item["authors"].empty()
							&& item["authors"][0].is_object()) {
						artist = SearchJsonString(item["authors"][0], "name");
					}

					if (std::string(key) == "artists"
							&& item.contains("genres")
							&& item["genres"].is_array()
							&& !item["genres"].empty()
							&& item["genres"][0].is_string()) {
						artist = item["genres"][0].get<std::string>();
					}

					msg->AddString("name",   name.c_str());
					msg->AddString("type",   typeLabel);
					msg->AddString("artist", artist.c_str());
					msg->AddString("album",  album.c_str());
					msg->AddString("uri",    uri.c_str());
					msg->AddString("artUri", artUri.c_str());
					msg->AddString("albUri", albUri.c_str());
					count++;
				}
			};

			addItems("tracks",     B_TRANSLATE("Song"));
			addItems("artists",    B_TRANSLATE("Artist"));
			addItems("albums",     B_TRANSLATE("Album"));
			addItems("playlists",  B_TRANSLATE("Playlist"));
			addItems("audiobooks", B_TRANSLATE("Audiobook"));
			addItems("shows",      B_TRANSLATE("Podcast"));
			addItems("episodes",   B_TRANSLATE("Episode"));
			addItems("users",      B_TRANSLATE("Profile"));

			msg->AddInt32("count", count);
			self.SendMessage(msg);
			delete msg;
		});
}
