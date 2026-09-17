#include "PlaylistWindow.h"
#include "ArtworkView.h"
#include "DescriptionTextFormatter.h"
#include "HaifyDragState.h"
#include "MediaDescriptionView.h"
#include "TrackContextMenu.h"
#include "TextInputDialog.h"
#include "MediaHeaderStyle.h"
#include "Messages.h"
#include "MessageContracts.h"
#include "NowPlayingFields.h"
#include "SettingsController.h"
#include "HaifyDebug.h"
#include "App.h"
#include "UiLogic.h"
#include "playlist/PlaylistCacheDocument.h"
#include "playlist/PlaylistCacheFiles.h"
#include "playlist/PlaylistCacheRows.h"
#include "playlist/PlaylistContent.h"
#include "playlist/PlaylistMetadataMessages.h"
#include "playlist/PlaylistMetadataRequests.h"
#include "playlist/PlaylistPageRequests.h"
#include "playlist/PlaylistPageMessages.h"
#include "playlist/PlaylistRemovalMessages.h"
#include "playlist/PlaylistRemovalRequests.h"
#include "playlist/PlaylistReorderMessages.h"
#include "playlist/PlaylistReorderRequests.h"
#include "playlist/PlaylistWriteMessages.h"
#include "playlist/PlaylistWriteRequests.h"
#include "playlist/PlaylistCoverMessages.h"
#include "playlist/PlaylistCoverRequests.h"
#include "playlist/PlaylistTrackRow.h"
#include "playlist/PlaylistEpisodeRows.h"
#include "playlist/PlaylistTrackListView.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"
#include "spotify/api/SpotifyResponse.h"
#include "UiScale.h"

#include <Alert.h>
#include <Alignment.h>
#include <Application.h>
#include <Bitmap.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Button.h>
#include <CheckBox.h>
#include <FilePanel.h>
#include <MessageRunner.h>
#include <MessageFilter.h>
#include <ScrollBar.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <Font.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <Resources.h>
#include <String.h>
#include <Catalog.h>

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <algorithm>
#include <cstdio>
#include <utility>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaylistWindow"

static const uint32 kMsgCheckLazyLoad = 'ckLm';
static const uint32 kMsgToggleAlbumSaved = 'tAlS';
static const uint32 kMsgAlbumSavedState = 'aSvS';
static const uint32 kMsgShowAlbumMenu = 'sAlM';
static const uint32 kMsgReloadArtwork = 'rArt';
static const uint32 kMsgShowPlaylistMenu = 'sPlM';
static const uint32 kMsgDeletePlaylist = 'dPls';
static const uint32 kMsgPlaylistDeleted = 'pDlD';
static const uint32 kMsgPlaylistDeleteFailed = 'pDlF';
static const uint32 kMsgEditPlaylist = 'pEdt';
static const uint32 kMsgPlaylistDetails = 'pEdS';
static const uint32 kMsgChoosePlaylistCover = 'pCov';
static const uint32 kMsgPlaylistCoverSelected = 'pCvS';
static const uint32 kMsgClearPlaylist = 'pClr';
static const uint32 kMsgMovePlaylistItemUp = 'pMvU';
static const uint32 kMsgMovePlaylistItemDown = 'pMvD';
static const uint32 kMsgSaveCache = 'sCch';
static const int32 kLikedSongsIconResource = 2015;
static const int32 kSearchIconResource = 2016;

class ResourceIconView : public BView {
public:
	ResourceIconView(const char* name, int32 resourceId, float size)
		:
		BView(name, B_WILL_DRAW),
		fIcon(_LoadIcon(resourceId, size))
	{
		SetExplicitMinSize(BSize(size, size));
		SetExplicitPreferredSize(BSize(size, size));
		SetExplicitMaxSize(BSize(size, size));
		SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
			B_ALIGN_VERTICAL_CENTER));
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	virtual ~ResourceIconView()
	{
		delete fIcon;
	}

	virtual void Draw(BRect updateRect) override
	{
		SetHighColor(ViewColor());
		FillRect(updateRect);
		if (!fIcon)
			return;
		SetDrawingMode(B_OP_ALPHA);
		BRect bounds = Bounds();
		BRect iconBounds = fIcon->Bounds();
		DrawBitmap(fIcon, BPoint(
			(bounds.Width() - iconBounds.Width()) / 2.0f,
			(bounds.Height() - iconBounds.Height()) / 2.0f));
		SetDrawingMode(B_OP_COPY);
	}

private:
	static BBitmap* _LoadIcon(int32 resourceId, float size)
	{
		BResources* resources = be_app ? be_app->AppResources() : nullptr;
		if (!resources || size <= 0)
			return nullptr;
		size_t dataSize = 0;
		const void* data = resources->LoadResource('VICN', resourceId,
			&dataSize);
		if (!data || dataSize == 0)
			return nullptr;
		int32 side = std::max<int32>(1, (int32)(size + 0.5f));
		BBitmap* icon = new BBitmap(BRect(0, 0, side - 1, side - 1),
			B_RGBA32);
		if (icon->InitCheck() != B_OK) {
			delete icon;
			return nullptr;
		}
		memset(icon->Bits(), 0, icon->BitsLength());
		if (BIconUtils::GetVectorIcon((const uint8*)data, dataSize,
				icon) != B_OK) {
			delete icon;
			return nullptr;
		}
		return icon;
	}

	BBitmap* fIcon;
};

static BBitmap*
LoadLikedSongsArtwork(float size)
{
	BResources* resources = be_app ? be_app->AppResources() : nullptr;
	if (!resources || size <= 0)
		return nullptr;

	size_t dataSize = 0;
	const void* data = resources->LoadResource('VICN',
		kLikedSongsIconResource, &dataSize);
	if (!data || dataSize == 0)
		return nullptr;

	int32 side = std::max<int32>(1, (int32)size);
	int32 iconSide = std::max<int32>(1, (int32)(side * 0.72f + 0.5f));
	BBitmap* icon = new BBitmap(BRect(0, 0, iconSide - 1, iconSide - 1),
		B_RGBA32);
	if (icon->InitCheck() != B_OK) {
		delete icon;
		return nullptr;
	}
	memset(icon->Bits(), 0, icon->BitsLength());
	if (BIconUtils::GetVectorIcon((const uint8*)data, dataSize, icon) != B_OK) {
		delete icon;
		return nullptr;
	}

	BRect bounds(0, 0, side - 1, side - 1);
	BBitmap* artwork = new BBitmap(bounds, B_RGB32, true);
	if (artwork->InitCheck() != B_OK) {
		delete icon;
		delete artwork;
		return nullptr;
	}

	BView* canvas = new BView(bounds, "liked songs artwork", B_FOLLOW_NONE, 0);
	artwork->AddChild(canvas);
	canvas->LockLooper();
	rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);
	float luminance = (0.299f * panelBg.red + 0.587f * panelBg.green
		+ 0.114f * panelBg.blue) / 255.0f;
	rgb_color placeholderBg = luminance > 0.5f
		? tint_color(panelBg, B_DARKEN_1_TINT)
		: tint_color(panelBg, B_LIGHTEN_1_TINT);
	rgb_color mutedText = tint_color(ui_color(B_PANEL_TEXT_COLOR),
		B_DISABLED_LABEL_TINT);
	rgb_color border = tint_color(mutedText, B_LIGHTEN_2_TINT);

	canvas->SetHighColor(placeholderBg);
	canvas->FillRect(bounds);
	float inset = (side - iconSide) / 2.0f;
	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->DrawBitmap(icon, BPoint(inset, inset));
	canvas->SetDrawingMode(B_OP_COPY);
	canvas->SetHighColor(border);
	canvas->StrokeRect(bounds);
	canvas->Sync();
	canvas->UnlockLooper();
	artwork->RemoveChild(canvas);
	delete canvas;
	delete icon;
	return artwork;
}

class PlaylistDetailsDialog : public BWindow {
public:
	PlaylistDetailsDialog(const std::string& name,
		const std::string& description, bool isPublic, BMessenger target)
		: BWindow(BRect(220, 180, 650, 460), B_TRANSLATE("Edit Playlist"),
			B_TITLED_WINDOW, B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_ASYNCHRONOUS_CONTROLS),
		  fTarget(target)
	{
		fName = new BTextControl("playlistName", B_TRANSLATE("Name:"),
			name.c_str(), nullptr);
		fDescription = new BTextView("playlistDescription");
		fDescription->SetText(description.c_str());
		fDescription->SetWordWrap(true);
		fPublic = new BCheckBox("playlistPublic", B_TRANSLATE("Public playlist"),
			nullptr);
		fPublic->SetValue(isPublic ? B_CONTROL_ON : B_CONTROL_OFF);
		BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));
		BButton* save = new BButton("save", B_TRANSLATE("Save"),
			new BMessage('pDsv'));
		SetDefaultButton(save);
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(fName)
			.Add(new BStringView("descriptionLabel", B_TRANSLATE("Description:")))
			.Add(new BScrollView("descriptionScroll", fDescription, 0,
				false, true), 1.0f)
			.Add(fPublic)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.AddGlue()
				.Add(cancel)
				.Add(save)
			.End()
		.End();
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what != 'pDsv') {
			BWindow::MessageReceived(message);
			return;
		}
		std::string name = fName->Text();
		std::string description = fDescription->Text();
		if (name.empty() || name.size() > 100 || description.size() > 300) {
			BAlert* alert = new BAlert("", B_TRANSLATE(
				"The name must contain 1-100 characters and the description at most 300 characters."),
				B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->Go();
			return;
		}
		BMessage result(kMsgPlaylistDetails);
		result.AddString("name", name.c_str());
		result.AddString("description", description.c_str());
		result.AddBool("public", fPublic->Value() == B_CONTROL_ON);
		fTarget.SendMessage(&result);
		Quit();
	}

private:
	BMessenger fTarget;
	BTextControl* fName;
	BTextView* fDescription;
	BCheckBox* fPublic;
};


static void
SetCachedTrackInfo(BStringView* infoView, bool isPlaylist, int32 total,
	int32 rowCount)
{
	char info[64];
	const char* typeStr = isPlaylist
		? B_TRANSLATE("Playlist") : B_TRANSLATE("Liked Songs");
	if (total > rowCount)
		snprintf(info, sizeof(info), "%s \xC2\xB7 %ld/%ld %s", typeStr,
			(long)rowCount, (long)total, "Songs");
	else
		snprintf(info, sizeof(info), "%s \xC2\xB7 %ld %s", typeStr,
			(long)rowCount, "Songs");
	infoView->SetText(info);
}

static void
SendShowSubscriptionMessage(const BMessenger& messenger, bool ok,
	const nlohmann::json& data)
{
	bool valid = ok && data.is_array() && !data.empty()
		&& data[0].is_boolean();
	BMessage subMsg('subU');
	subMsg.AddBool("ok", valid);
	if (valid)
		subMsg.AddBool("following", data[0].get<bool>());
	messenger.SendMessage(&subMsg);
}

PlaylistWindow::PlaylistWindow(const char* playlistName, const char* uri, const char* coverUrl)
	: BWindow(BRect(200, 200,
		200 + kDefaultPlaylistWindowWidth,
		200 + kDefaultPlaylistWindowHeight), playlistName,
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS), fUri(uri), fCoverUrl(coverUrl)
{
	HaifySettings s = SettingsController::Load();
	if (s.playlistWindowW > 0) {
		MoveTo(s.playlistWindowX, s.playlistWindowY);
		ResizeTo(s.playlistWindowW, s.playlistWindowH);
	}

	SetTitle((std::string(ResolvePlaylistHeader(fUri).titlePrefix)
		+ playlistName).c_str());

	_InitMenu();
	_InitLayout(playlistName);
	if (!fCoverUrl.empty() && fCoverView)
		static_cast<ArtworkView*>(fCoverView)->LoadUrl(fCoverUrl);
	_LoadData();
	BMessage lazyMessage(kMsgCheckLazyLoad);
	fLazyLoadRunner = new BMessageRunner(BMessenger(this), &lazyMessage,
		500000LL);

}


void
PlaylistWindow::SetCoverUrl(const std::string& coverUrl)
{
	if (coverUrl.empty())
		return;
	fCoverUrl = coverUrl;
	if (fCoverView)
		static_cast<ArtworkView*>(fCoverView)->LoadUrl(fCoverUrl);
}


bool
PlaylistWindow::_HandleTrackActionMessage(BMessage* message)
{
	return _HandleTrackPlaybackActionMessage(message)
		|| _HandleTrackLibraryActionMessage(message)
		|| _HandleTrackDragActionMessage(message);
}


bool
PlaylistWindow::_HandleTrackPlaybackActionMessage(BMessage* message)
{
	switch (message->what) {
		case 'tply':
			_PlayTrackFromMessage(message);
			return true;
		case MSG_PLAY_PAUSE:
			be_app->PostMessage(message);
			return true;
		case MSG_TRACK_INVOKED:
			_PlayCurrentTrack();
			return true;
		default:
			return false;
	}
}


bool
PlaylistWindow::_HandleTrackLibraryActionMessage(BMessage* message)
{
	switch (message->what) {
		case 'remL':
			_RemoveTrackFromLibrary(message);
			return true;
		case 'iCmR':
			_ShowPlayableContextMenu(message);
			return true;
		case 'likT':
			_SavePlayableItemToLibrary(message);
			return true;
		case 'addP':
			_AddMessageTrackToPlaylist(message);
			return true;
		case 'remT':
			_RemoveSelectedTracksFromPlaylist(message);
			return true;
		case MSG_PLAYLIST_REMOVAL_RESULT:
			_ApplyTrackRemovalResult(message);
			return true;
		case MSG_PLAYLIST_REORDER_RESULT:
			_ApplyTrackReorderResult(message);
			return true;
		case MSG_PLAYLIST_CLEAR_RESULT:
		case MSG_PLAYLIST_ADD_RESULT:
			_ApplyPlaylistWriteResult(message);
			return true;
		case MSG_PLAYLIST_SNAPSHOT_RESULT:
			_ApplyPlaylistSnapshot(message);
			return true;
		default:
			return false;
	}
}


bool
PlaylistWindow::_HandleTrackDragActionMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_PLAYLIST_DROP:
			_HandleTrackDrop(message);
			return true;
		case MSG_HAIFY_DRAG_ENDED:
			if (fTrackList)
				fTrackList->ClearDropMarker();
			return true;
		default:
			return false;
	}
}


bool
PlaylistWindow::_HandleDataMessage(BMessage* message)
{
	switch (message->what) {
		case 'lddt':
			_ReloadDataIfIdle();
			return true;
		case 'rfEp':
			_RefreshEpisodes();
			return true;
		case kMsgCheckLazyLoad:
			_CheckLazyLoad();
			_UpdatePlaylistMenuState();
			return true;
		case kMsgSaveCache:
			_SaveCacheNowFromMessage();
			return true;
		case MSG_PLAYLIST_PAGE_FAILED:
			_ApplyPageLoadFailure(message);
			return true;
		case MSG_PLAYLIST_TRACK_PAGE:
			_ApplyTrackPage(message);
			return true;
		case MSG_PLAYLIST_TITLE_UPDATE:
			_ApplyTitleUpdate(message);
			return true;
		case MSG_CURRENT_TRACK_UPDATE:
			_ApplyPlayingTrackUpdate(message);
			return true;
		case MSG_PLAYLIST_COVER_UPDATE:
			_ApplyCoverUpdate(message);
			return true;
		case kMsgToggleAlbumSaved:
			_ToggleAlbumSaved();
			return true;
		case kMsgReloadArtwork:
			_ReloadArtwork();
			return true;
		case MSG_LIBRARY_CHANGED:
			_ApplyLibraryChange(message);
			return true;
		case kMsgAlbumSavedState:
			_ApplyAlbumSavedState(message);
			return true;
		default:
			return false;
	}
}


bool
PlaylistWindow::_HandlePlaylistEditMessage(BMessage* message)
{
	switch (message->what) {
		case 'plRn':
			_ShowRenamePlaylistDialog(message);
			return true;
		case 'plRc':
			_RenamePlaylist(message);
			return true;
		case MSG_PLAYLIST_METADATA_RESULT:
			_ApplyMetadataResult(message);
			return true;
		case kMsgEditPlaylist:
			_ShowPlaylistDetailsDialog();
			return true;
		case kMsgPlaylistDetails:
			_UpdatePlaylistDetails(message);
			return true;
		case 'pEdR':
			_ApplyPlaylistEditResult(message);
			return true;
		case kMsgChoosePlaylistCover:
			_ChoosePlaylistCover();
			return true;
		case kMsgPlaylistCoverSelected:
			_UploadPlaylistCoverFromMessage(message);
			return true;
		case MSG_PLAYLIST_COVER_RESULT:
			_ApplyPlaylistCoverUploadResult(message);
			return true;
		case kMsgClearPlaylist:
			_ClearPlaylist();
			return true;
		case kMsgMovePlaylistItemUp:
			_MoveSelectedItem(-1);
			return true;
		case kMsgMovePlaylistItemDown:
			_MoveSelectedItem(1);
			return true;
		case 'pSnC':
			_ShowPlaylistSnapshotConflict();
			return true;
		default:
			return false;
	}
}


bool
PlaylistWindow::_HandlePlaylistMenuMessage(BMessage* message)
{
	switch (message->what) {
		case kMsgShowAlbumMenu:
			_ShowAlbumMenuFromMessage(message);
			return true;
		case kMsgShowPlaylistMenu:
			_ShowPlaylistMenuFromMessage(message);
			return true;
		case kMsgDeletePlaylist:
			_DeletePlaylist();
			return true;
		case kMsgPlaylistDeleted:
			_NotifyPlaylistDeleted();
			return true;
		case kMsgPlaylistDeleteFailed:
			_ApplyPlaylistDeleteFailed();
			return true;
		default:
			return false;
	}
}


bool
PlaylistWindow::_HandlePodcastMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_PLAYLIST_EPISODE_PAGE:
			_ApplyEpisodePage(message);
			return true;
		case MSG_PLAYLIST_PODCAST_HEAD_PAGE:
			_ApplyPodcastHeadPage(message);
			return true;
		case 'subU':
			_ApplySubscriptionState(message);
			return true;
		case 'subS':
			_TogglePodcastSubscription();
			return true;
		case 'srch':
			_ScheduleEpisodeSearch();
			return true;
		case MSG_PLAYLIST_APPLY_SEARCH:
			_ApplyEpisodeSearch(message);
			return true;
		case MSG_PLAYLIST_RETRY_SEARCH:
			_RetryEpisodeSearch(message);
			return true;
		case 'epSl':
			_ApplyEpisodeSelection(message);
			return true;
		default:
			return false;
	}
}


bool
PlaylistWindow::_HandleAppForwardMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_OPEN_BROWSER:
		case MSG_OPEN_PLAYLIST:
		case MSG_SHOW_ARTIST:
		case MSG_SHOW_ALBUM:
		case MSG_INIT_AUTH:
		case MSG_PLAY_PAUSE:
		case 'open':
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
PlaylistWindow::_ApplyPlaylistSnapshot(BMessage* message)
{
	PlaylistMetadataResult result;
	if (_PlaylistMutationPending() || !ReadPlaylistSnapshotMessage(*message, result)
			|| !result.ok || result.request.id != _PlaylistId())
		return;

	fPlaylistSnapshotId = result.snapshotId;
	fCachedPlaylistSnapshotId = result.snapshotId;
	PlaylistPagePosition position = fPaging.Position();
	int32 total = result.total;
	if (total >= 0)
		position.total = total;
	position.hasMore = position.offset < position.total;
	fPaging.RestorePosition(position);
	_UpdatePlaylistTrackInfo();
	if (!fPlaylistSnapshotId.empty())
		_SaveCache();
}


void
PlaylistWindow::_ReloadDataIfIdle()
{
	if (!_PlaylistMutationPending())
		_LoadData();
}


void
PlaylistWindow::_RefreshEpisodes()
{
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemShow)
		return;

	_DeleteCache();
	_LoadData(true);
}


void
PlaylistWindow::_SaveCacheNowFromMessage()
{
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	_WriteCacheNow();
}


void
PlaylistWindow::_ApplyTitleUpdate(BMessage* message)
{
	const char* title;
	if (message->FindString("title", &title) == B_OK) {
		SetTitle((std::string(ResolvePlaylistHeader(fUri).titlePrefix) + title).c_str());
		fPlaylistName->SetText(title);
	}
}


void
PlaylistWindow::_UploadPlaylistCoverFromMessage(BMessage* message)
{
	entry_ref ref;
	if (message->FindRef("refs", &ref) == B_OK)
		_UploadPlaylistCover(ref);
}


void
PlaylistWindow::_ShowPlaylistSnapshotConflict()
{
	BAlert* alert = new BAlert("", B_TRANSLATE(
		"The playlist changed on Spotify. Haify will reload it before you try again."),
		B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->Go();
	PostMessage('lddt');
}


void
PlaylistWindow::_ApplyPlayingTrackUpdate(BMessage* message)
{
	MessageContracts::CurrentTrackUpdate update;
	if (MessageContracts::ReadCurrentTrackUpdate(*message, update))
		SetPlayingTrack(update.uri.c_str());
}


void
PlaylistWindow::_ApplyCoverUpdate(BMessage* message)
{
	const char* url;
	if (message->FindString("url", &url) != B_OK)
		return;
	SetCoverUrl(url);
}


void
PlaylistWindow::_ShowAlbumMenuFromMessage(BMessage* message)
{
	BPoint screenWhere;
	if (message->FindPoint("screen_where", &screenWhere) == B_OK)
		_ShowAlbumContextMenu(screenWhere);
}


void
PlaylistWindow::_ShowPlaylistMenuFromMessage(BMessage* message)
{
	BPoint screenWhere;
	if (message->FindPoint("screen_where", &screenWhere) == B_OK)
		_ShowPlaylistContextMenu(screenWhere);
}


void
PlaylistWindow::_ApplyPlaylistDeleteFailed()
{
	fPlaylistDeletePending = false;
	_UpdatePlaylistMenuState();
}


void
PlaylistWindow::MessageReceived(BMessage* message)
{
	if (_HandleTrackActionMessage(message) || _HandleDataMessage(message)
			|| _HandlePlaylistEditMessage(message)
			|| _HandlePlaylistMenuMessage(message)
			|| _HandlePodcastMessage(message)
			|| _HandleAppForwardMessage(message)) {
		return;
	}

	BWindow::MessageReceived(message);
}

void
PlaylistWindow::_ShowRenamePlaylistDialog(BMessage* message)
{
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemPlaylist)
		return;
	std::string id = SpotifyItemIdForUri(fUri);
	const char* idFromMsg = message->GetString("id", "");
	if (*idFromMsg)
		id = idFromMsg;
	std::string currentName = Title();

	const std::string prefix = "Playlist: ";
	if (currentName.find(prefix) == 0)
		currentName = currentName.substr(prefix.size());
	BMessage confirm('plRc');
	confirm.AddString("id", id.c_str());
	TextInputDialog* dialog = new TextInputDialog(
		B_TRANSLATE("Rename Playlist"), B_TRANSLATE("Name:"),
		currentName.c_str(), BMessenger(this), confirm);
	dialog->Show();
}


void
PlaylistWindow::_RenamePlaylist(BMessage* message)
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
	std::string sname = name;
	api->Playlists().RenamePlaylist(sid, sname,
		[self, sid, sname](bool ok, const nlohmann::json&) {
		if (!ok)
			return;
		BMessage msg = MakePlaylistTitleMessage(sname);
		self.SendMessage(&msg);

		BMessage changed(MSG_PLAYLISTS_CHANGED);
		changed.AddString("operation", "rename");
		changed.AddString("id", sid.c_str());
		std::string playlistUri = SpotifyUriForItemKind(
			kSpotifyItemPlaylist, sid);
		changed.AddString("uri", playlistUri.c_str());
		changed.AddString("name", sname.c_str());
		be_app->PostMessage(&changed);
	});
}


void
PlaylistWindow::_PlayTrackFromMessage(BMessage* message)
{
	const char* trackUri = message->GetString(MessageFields::TrackUri, "");
	if (!*trackUri)
		return;
	BMessage play = MessageContracts::MakePlayCommand({trackUri, fUri});
	bool podcast = SpotifyItemKindForUri(fUri) == kSpotifyItemShow;
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		TrackRow* row = (TrackRow*)fTrackList->RowAt(i);
		if (!row || row->fTrackUri != trackUri)
			continue;
		BStringField* title = dynamic_cast<BStringField*>(row->GetField(1));
		BStringField* artist = dynamic_cast<BStringField*>(row->GetField(2));
		if (title)
			play.AddString(MessageFields::Title, title->String());
		if (!podcast && artist)
			play.AddString(MessageFields::Artist, artist->String());
		break;
	}
	_AddFollowingTrackQueue(play, trackUri);
	_AddPodcastNowPlayingContext(play);
	be_app->PostMessage(&play);
}


void
PlaylistWindow::_PlayCurrentTrack()
{
	TrackRow* row = (TrackRow*)fTrackList->CurrentSelection();
	if (!row || row->fTrackUri.empty())
		return;

	BMessage play = MessageContracts::MakePlayCommand({row->fTrackUri, fUri});
	bool podcast = SpotifyItemKindForUri(fUri) == kSpotifyItemShow;
	BStringField* title = dynamic_cast<BStringField*>(row->GetField(1));
	BStringField* artist = dynamic_cast<BStringField*>(row->GetField(2));
	if (title)
		play.AddString(MessageFields::Title, title->String());
	if (!podcast && artist)
		play.AddString(MessageFields::Artist, artist->String());
	_AddFollowingTrackQueue(play, row->fTrackUri);
	_AddPodcastNowPlayingContext(play);
	be_app->PostMessage(&play);
	SetPlayingTrack(row->fTrackUri.c_str());
}


void
PlaylistWindow::_RemoveTrackFromLibrary(BMessage* message)
{
	const char* trackUri = message->GetString("trackUri", "");
	if (!*trackUri)
		return;

	std::string uri = trackUri;
	bool removeFromVisibleList = fUri == "spotify:collection";
	if (removeFromVisibleList) {
		for (int32 i = 0; i < fTrackList->CountRows(); i++) {
			TrackRow* row = (TrackRow*)fTrackList->RowAt(i);
			if (row && row->fTrackUri == uri) {
				fTrackList->RemoveRow(row);
				delete row;
				break;
			}
		}
	}
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	api->Library().RemoveLibraryItems({uri}, [uri](bool ok,
			const nlohmann::json&) {
		if (!ok || SpotifyItemKindForUri(uri) != kSpotifyItemEpisode)
			return;
		BMessage changed(MSG_LIBRARY_CHANGED);
		changed.AddString("operation", "remove");
		changed.AddString("uri", uri.c_str());
		be_app->PostMessage(&changed);
	});
	if (removeFromVisibleList)
		_DeleteCache();
}


void
PlaylistWindow::_SavePlayableItemToLibrary(BMessage* message)
{
	const char* trackUri;
	if (message->FindString("trackUri", &trackUri) != B_OK)
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	std::string uri = trackUri;
	if (!SpotifyItemIsPlayable(SpotifyItemKindForUri(uri)))
		return;
	api->Library().SaveLibraryItems({uri}, [uri](bool ok,
			const nlohmann::json&) {
		if (!ok || SpotifyItemKindForUri(uri) != kSpotifyItemEpisode)
			return;
		BMessage changed(MSG_LIBRARY_CHANGED);
		changed.AddString("operation", "add");
		changed.AddString("uri", uri.c_str());
		be_app->PostMessage(&changed);
	});
	_DeleteCache();
}


void
PlaylistWindow::_AddMessageTrackToPlaylist(BMessage* message)
{
	const char* trackUri;
	const char* playlistId;
	if (message->FindString("trackUri", &trackUri) != B_OK
			|| message->FindString("playlistId", &playlistId) != B_OK) {
		return;
	}
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	api->Playlists().AddTrackToPlaylist(playlistId, trackUri, nullptr);
	PlaylistCacheFiles::RemovePlaylist(playlistId);
}


void
PlaylistWindow::_RemoveSelectedTracksFromPlaylist(BMessage* message)
{
	const char* trackUri;
	if (message->FindString("trackUri", &trackUri) != B_OK)
		return;
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api || !fTrackList || fRemoval.Pending())
		return;

	std::vector<std::pair<std::string, int>> items;
	if (!_CollectPendingTrackRemovals(items))
		return;
	PlaylistRemovalContext context;
	context.playlistId = _PlaylistId();
	context.snapshotId = fPlaylistSnapshotId;
	context.page = fPaging.Position();
	context.rowCount = fTrackList->CountRows();
	context.owned = fMetadata.IsOwned();
	context.otherMutationPending = _PlaylistMutationPending();
	PlaylistRemovalCommand command;
	if (!fRemoval.Begin(context, items, _VisiblePlaylistUris(), command)) {
		fPendingTrackRemovals.clear();
		return;
	}

	// Old reads must not append rows to the optimistic list or enter its cache.
	fPaging.RestorePosition(fPaging.Position());
	_DeleteCache();
	_RemovePendingTrackRows();
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();
	if (!PlaylistRemovalRequests::Send(api->Playlists(), command, BMessenger(this))) {
		PlaylistRemovalResult failure;
		failure.requestId = command.requestId;
		failure.playlistId = command.playlistId;
		BMessage result = MakePlaylistRemovalMessage(failure);
		_ApplyTrackRemovalResult(&result);
	}
}


bool
PlaylistWindow::_CollectPendingTrackRemovals(
	std::vector<std::pair<std::string, int>>& items)
{
	items.clear();
	fPendingTrackRemovals.clear();
	for (BRow* selected = fTrackList->CurrentSelection(); selected;
			selected = fTrackList->CurrentSelection(selected)) {
		TrackRow* row = dynamic_cast<TrackRow*>(selected);
		if (!row || row->fTrackUri.empty() || row->fPlaylistPosition < 0)
			continue;
		int32 listIndex = fTrackList->IndexOf(row);
		if (listIndex < 0)
			continue;
		items.push_back({row->fTrackUri, row->fPlaylistPosition});
		fPendingTrackRemovals.push_back({row, listIndex, true});
	}
	return !items.empty() && !fPendingTrackRemovals.empty();
}


std::vector<std::string>
PlaylistWindow::_VisiblePlaylistUris() const
{
	std::vector<std::string> uris;
	for (int32 index = 0; index < fTrackList->CountRows(); index++) {
		TrackRow* row = dynamic_cast<TrackRow*>(fTrackList->RowAt(index));
		if (!row || row->fTrackUri.empty())
			return {};
		uris.push_back(row->fTrackUri);
	}
	return uris;
}


void
PlaylistWindow::_RemovePendingTrackRows()
{
	std::sort(fPendingTrackRemovals.begin(), fPendingTrackRemovals.end(),
		[](const PendingTrackRemoval& left,
				const PendingTrackRemoval& right) {
			return left.listIndex < right.listIndex;
		});
	for (auto pending = fPendingTrackRemovals.rbegin();
			pending != fPendingTrackRemovals.rend(); ++pending) {
		fTrackList->RemoveRow(pending->row);
	}
}


void
PlaylistWindow::_HandleTrackDrop(BMessage* message)
{
	ClearHaifyActiveDragMessage();
	MessageContracts::DragItem item;
	if (!MessageContracts::ReadDragItem(*message, item))
		return;
	bool mutationPending = _PlaylistMutationPending();
	PlaylistDropAction action = ResolvePlaylistDropAction(fUri, item,
		fMetadata.IsOwned(), mutationPending);
	if (action == kPlaylistDropReorder) {
		_HandleTrackReorderDrop(message, item);
		return;
	}
	if (action == kPlaylistDropAddPlayableItem) {
		_AddDroppedPlayableItem(message, item.uri.c_str());
		return;
	}
	DEBUG_PRINT("PlaylistWindow: Drop received without playable track uri\n");
}

bool
PlaylistWindow::QuitRequested()
{
	// A group reorder advances on this looper; let it finish before closing.
	if (fReorder.Pending()) {
		fCloseAfterReorder = true;
		return false;
	}
	return BWindow::QuitRequested();
}


void
PlaylistWindow::_HandleTrackReorderDrop(BMessage* message,
	const MessageContracts::DragItem& item)
{
	std::vector<int32_t> indices = item.sourceIndices;
	if (indices.empty()) {
		auto row = dynamic_cast<TrackRow*>(fTrackList->RowAt(item.sourceIndex));
		if (!row || row->fTrackUri != item.uri)
			return;
		indices.push_back(item.sourceIndex);
	} else {
		if (item.sourceSnapshot != fPlaylistSnapshotId)
			return;
		for (size_t i = 0; i < indices.size(); i++) {
			auto row = dynamic_cast<TrackRow*>(fTrackList->RowAt(indices[i]));
			if (!row || row->fTrackUri != item.sourceUris[i])
				return;
		}
	}
	BPoint point = message->DropPoint();
	if (BView* outline = fTrackList->ScrollView())
		outline->ConvertFromScreen(&point);
	else
		fTrackList->ConvertFromScreen(&point);
	BRow* targetRow = fTrackList->RowAt(point);
	int32 insertBefore = fTrackList->CountRows();
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		if (fTrackList->RowAt(i) == targetRow) {
			insertBefore = i;
			BRect rowRect;
			if (fTrackList->GetRowRect(targetRow, &rowRect)
					&& point.y >= rowRect.top + rowRect.Height() / 2.0f) {
				insertBefore++;
			}
			break;
		}
	}
	_BeginTrackReorder(indices, insertBefore);
}


void
PlaylistWindow::_AddDroppedPlayableItem(BMessage* message, const char* trackUri)
{
	App* app = dynamic_cast<App*>(be_app);
	if (!app || !app->GetApi() || !fTrackList || !trackUri)
		return;
	PlaylistWriteCommand command;
	if (!fAdd.Begin(_PlaylistWriteContext(), trackUri, command))
		return;
	if (command.appendVisible) {
		TrackRow* row = new TrackRow(trackUri, command.appendPosition);
		row->SetField(new TrackIntegerField(command.appendPosition + 1), 0);
		row->SetField(new TrackStringField(message->GetString(MessageFields::Title, "")), 1);
		row->SetField(new TrackStringField(message->GetString(MessageFields::Artist, "")), 2);
		row->SetField(new TrackStringField(""), 3);
		row->SetField(new TrackStringField(""), 4);
		row->SetField(new TrackStringField(message->GetString(MessageFields::Album, "")), 5);
		row->SetField(new TrackStringField(message->GetString(MessageFields::Duration, "")), 6);
		row->SetPlaying(!fCurrentPlayingTrackUri.empty()
			&& row->fTrackUri == fCurrentPlayingTrackUri);
		fTrackList->AddRow(row);
		fPendingPlaylistAdd = row;
	}
	fPaging.RestorePosition(command.optimisticPage);
	_DeleteCache();
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();
	_SendPlaylistWrite(command);
}


void
PlaylistWindow::_ApplyPageLoadFailure(BMessage* message)
{
	PlaylistPageResult result;
	if (!ReadPlaylistPageHeader(*message, result))
		return;
	PlaylistPageUpdate update = fPaging.FinishPage(result);
	if (!update.accepted)
		return;
	if (update.retryDelay > 0) {
		delete fEpisodeSearchRetryRunner;
		BMessage retry = MakePlaylistSearchMessage(true, fPaging.SearchGeneration());
		fEpisodeSearchRetryRunner = new BMessageRunner(BMessenger(this),
			&retry, update.retryDelay, 1);
	}
	if (result.request.source == PlaylistPageSource::Podcast)
		_UpdateEpisodeInfo();
	if (update.continueLoading)
		_CheckLazyLoad();
}


void
PlaylistWindow::_ApplyMetadataResult(BMessage* message)
{
	PlaylistMetadataResult result;
	if (!ReadPlaylistMetadataMessage(*message, result))
		return;
	if (!result.ok) {
		DEBUG_PRINT("Playlist metadata read failed: status=%ld, valid=%d\n",
			(long)result.status, result.responseValid);
		return;
	}
	fMetadata.Apply(result);
	switch (result.request.kind) {
		case PlaylistMetadataKind::Playlist:
			_ApplyPlaylistMetadata(result);
			break;
		case PlaylistMetadataKind::CurrentUser:
			_UpdatePlaylistMenuState();
			break;
		case PlaylistMetadataKind::Album:
		case PlaylistMetadataKind::Podcast: {
			BMessage title = MakePlaylistTitleMessage(result.title);
			_ApplyTitleUpdate(&title);
			SetCoverUrl(result.coverUrl);
			break;
		}
		default: break;
	}
}


void
PlaylistWindow::_ApplyPlaylistMetadata(const PlaylistMetadataResult& result)
{
	const char* title = result.title.c_str();
	if (title && title[0]) {
		SetTitle((std::string("Playlist: ") + title).c_str());
		fPlaylistName->SetText(title);
	}

	const char* coverUrl = result.coverUrl.c_str();
	if (coverUrl && coverUrl[0]) {
		BMessage cover = MakePlaylistCoverMessage(coverUrl);
		PostMessage(&cover);
	}

	const std::string& snapshot = result.snapshotId;
	_UpdatePlaylistMenuState();
	if (_PlaylistMutationPending())
		return;
	int32 total = result.total;
	fPlaylistSnapshotId = snapshot;

	if (ShouldReloadPlaylistRowsForSnapshot(fCachedPlaylistSnapshotId,
			snapshot)) {
		_DeleteCache();
		fCachedPlaylistSnapshotId.clear();
		fPaging.Restart(total);
		_LoadNextPage();
		return;
	}

	fPaging.ReconcileMetadata(total, fTrackList ? fTrackList->CountRows() : 0);
	if (fTrackList && fTrackList->CountRows() > 0)
		_SaveCache();
	_CheckLazyLoad();
}


void
PlaylistWindow::_ApplyPlaylistEditResult(BMessage* message)
{
	if (!message->GetBool("ok", false)) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not update the playlist details."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return;
	}

	std::string name = message->GetString("name", "");
	fMetadata.UpdateDetails(message->GetString("description", ""),
		message->GetBool("public", false));
	fPlaylistName->SetText(name.c_str());
	SetTitle((std::string("Playlist: ") + name).c_str());
	std::string id = _PlaylistId();
	if (id.empty())
		return;
	BMessage changed(MSG_PLAYLISTS_CHANGED);
	changed.AddString("operation", "rename");
	changed.AddString("id", id.c_str());
	changed.AddString("uri", fUri.c_str());
	changed.AddString("name", name.c_str());
	be_app->PostMessage(&changed);
}


void
PlaylistWindow::_ApplyLibraryChange(BMessage* message)
{
	std::string uri = message->GetString("uri", "");
	std::string operation = message->GetString("operation", "");
	if (uri != fUri || (operation != "add" && operation != "remove"))
		return;
	bool saved = operation == "add";
	SpotifyItemKind kind = SpotifyItemKindForUri(fUri);
	if (kind == kSpotifyItemAlbum) {
		fAlbumSaved = saved;
		fAlbumSavedKnown = true;
		fAlbumSavePending = false;
		_UpdateAlbumMenuItem();
	} else if (kind == kSpotifyItemShow) {
		fIsSubscribed = saved;
		fSubscriptionKnown = true;
		fSubscriptionPending = false;
		if (fSubscribeButton) {
			fSubscribeButton->SetLabel(fIsSubscribed
				? B_TRANSLATE("Unsubscribe") : B_TRANSLATE("Subscribe"));
			fSubscribeButton->SetEnabled(true);
		}
	}
}


void
PlaylistWindow::_ApplyAlbumSavedState(BMessage* message)
{
	bool ok = message->GetBool("ok", true);
	if (ok) {
		fAlbumSaved = message->GetBool("saved", false);
		fAlbumSavedKnown = true;
		if (message->GetBool("changed", false)) {
			BMessage changed(MSG_LIBRARY_CHANGED);
			changed.AddString("operation", fAlbumSaved ? "add" : "remove");
			changed.AddString("uri", fUri.c_str());
			be_app->PostMessage(&changed);
		}
	} else if (message->GetBool("show_error", false)) {
		BAlert* alert = new BAlert(B_TRANSLATE("Album"),
			B_TRANSLATE("Saved Albums could not be updated."),
			B_TRANSLATE("OK"));
		alert->Go();
	}
	fAlbumSavePending = false;
	_UpdateAlbumMenuItem();
}


void
PlaylistWindow::_ApplySubscriptionState(BMessage* message)
{
	bool ok = message->GetBool("ok", true);
	if (ok) {
		fIsSubscribed = message->GetBool("following", false);
		fSubscriptionKnown = true;
		if (message->GetBool("changed", false)) {
			BMessage changed(MSG_LIBRARY_CHANGED);
			changed.AddString("operation",
				fIsSubscribed ? "add" : "remove");
			changed.AddString("uri", fUri.c_str());
			be_app->PostMessage(&changed);
		}
	} else if (message->GetBool("show_error", false)) {
		BAlert* alert = new BAlert(B_TRANSLATE("Podcast"),
			B_TRANSLATE("The podcast subscription could not be updated."),
			B_TRANSLATE("OK"));
		alert->Go();
	}
	fSubscriptionPending = false;
	if (fSubscribeButton) {
		fSubscribeButton->SetLabel(fIsSubscribed
			? B_TRANSLATE("Unsubscribe") : B_TRANSLATE("Subscribe"));
		fSubscribeButton->SetEnabled(fSubscriptionKnown);
	}
}


void
PlaylistWindow::_ApplyTrackPage(BMessage* message)
{
	PlaylistPageResult result;
	if (!ReadPlaylistPageHeader(*message, result) || !fPaging.FinishPage(result).applied)
		return;
	bool append = result.request.offset > 0;
	BScrollBar* scrollBar = TrackVerticalScrollBar(fTrackList);
	float scrollValue = scrollBar ? scrollBar->Value() : 0.0f;
	if (!append)
		fTrackList->Clear();

	_AddTrackPageRows(message);

	_UpdatePlaylistTrackInfo();
	if (append && scrollBar)
		scrollBar->SetValue(scrollValue);
	if (fUri == "spotify:collection"
			|| (SpotifyItemKindForUri(fUri) == kSpotifyItemPlaylist
				&& !fPlaylistSnapshotId.empty()))
		_SaveCache();
	_CheckLazyLoad();
}


void
PlaylistWindow::_AddTrackPageRows(BMessage* message)
{
	int32 number;
	const char *title, *artist, *artistUri, *bpm, *key, *album, *albumUri,
		*duration, *trackUri;
	for (int i = 0; message->FindInt32("number", i, &number) == B_OK; i++) {
		title = message->FindString("title", i);
		artist = message->FindString("artist", i);
		artistUri = message->FindString("artistUri", i);
		bpm = message->FindString("bpm", i);
		key = message->FindString("key", i);
		album = message->FindString("album", i);
		albumUri = message->FindString("albumUri", i);
		duration = message->FindString("duration", i);
		trackUri = message->FindString("trackUri", i);

		TrackRow* row = new TrackRow(trackUri ? trackUri : "", number - 1);
		row->fArtistUri = artistUri ? artistUri : "";
		row->fAlbumUri = albumUri ? albumUri : "";
		row->SetField(new TrackIntegerField(number), 0);
		row->SetField(new TrackStringField(title ? title : ""), 1);
		row->SetField(new TrackStringField(artist ? artist : ""), 2);
		row->SetField(new TrackStringField(bpm ? bpm : ""), 3);
		row->SetField(new TrackStringField(key ? key : ""), 4);
		row->SetField(new TrackStringField(album ? album : ""), 5);
		row->SetField(new TrackStringField(duration ? duration : ""), 6);
		row->SetPlaying(!fCurrentPlayingTrackUri.empty()
			&& row->fTrackUri == fCurrentPlayingTrackUri);
		fTrackList->AddRow(row);
	}
}


void
PlaylistWindow::_ShowPlayableContextMenu(BMessage* message)
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
PlaylistWindow::_PlayContextUri()
{
	if (fUri.empty())
		return;
	BMessage play = MessageContracts::MakePlayCommand({fUri});
	be_app->PostMessage(&play);
}


void
PlaylistWindow::_ApplyTrackRemovalResult(BMessage* message)
{
	PlaylistRemovalResult result;
	if (!ReadPlaylistRemovalMessage(*message, result))
		return;
	PlaylistRemovalUpdate update = fRemoval.Complete(result, fPaging.Position(),
		fTrackList ? fTrackList->CountRows() : 0);
	if (update.action == PlaylistRemovalAction::Ignore)
		return;
	_FinishTrackRemoval(update);
	if (update.action == PlaylistRemovalAction::Commit) {
		_RefreshPlaylistSnapshot();
	} else if (update.action == PlaylistRemovalAction::Reload) {
		PostMessage('pSnC');
	} else {
		_SaveCache();
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not remove the selected songs."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
}


void
PlaylistWindow::_ApplyTrackReorderResult(BMessage* message)
{
	PlaylistReorderResult result;
	if (!ReadPlaylistReorderMessage(*message, result))
		return;
	auto update = fReorder.Complete(result);
	if (update.action == PlaylistReorderAction::Ignore)
		return;
	if (update.action == PlaylistReorderAction::Continue) {
		_SendTrackReorder(update.next);
		return;
	}
	_FinishTrackReorder(update);
	if (update.action == PlaylistReorderAction::Commit) {
		if (update.refreshSnapshot)
			_RefreshPlaylistSnapshot();
	} else if (update.action == PlaylistReorderAction::Reload) {
		if (update.partialUpdate) {
			BAlert* alert = new BAlert("", B_TRANSLATE(
				"Only part of the selection could be moved. Haify will reload the playlist's current order."),
				B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->Go();
			PostMessage('lddt');
		} else
			PostMessage('pSnC');
	} else {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not move the selected songs."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
}


void
PlaylistWindow::_ApplyPlaylistWriteResult(BMessage* message)
{
	PlaylistWriteResult result;
	if (!ReadPlaylistWriteMessage(*message, result))
		return;
	PlaylistWriteUpdate update = result.kind == PlaylistWriteKind::Clear
		? fClear.Complete(result) : fAdd.Complete(result);
	if (update.action == PlaylistWriteAction::Ignore)
		return;
	bool committed = update.action == PlaylistWriteAction::Commit;
	if (result.kind == PlaylistWriteKind::Clear)
		_FinishClearPlaylist(committed);
	else {
		if (!committed && fPendingPlaylistAdd) {
			fTrackList->RemoveRow(fPendingPlaylistAdd);
			delete fPendingPlaylistAdd;
		}
		fPendingPlaylistAdd = nullptr;
	}
	fPaging.RestorePosition(update.page);
	fPlaylistSnapshotId = update.snapshotId;
	fCachedPlaylistSnapshotId = update.snapshotId;
	_DeleteCache();
	if (!update.snapshotId.empty())
		_SaveCache();
	if (update.refreshSnapshot)
		_RefreshPlaylistSnapshot();
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();
	if (update.action == PlaylistWriteAction::Reload)
		PostMessage('pSnC');
	else if (!committed) {
		const char* text = result.kind == PlaylistWriteKind::Clear
			? B_TRANSLATE("Spotify could not clear the playlist.")
			: B_TRANSLATE("Spotify could not add the song to the playlist.");
		(new BAlert("", text, B_TRANSLATE("OK"), nullptr, nullptr,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go();
	}
	_CheckLazyLoad();
}


void
PlaylistWindow::_UpdatePlaylistDetails(BMessage* message)
{
	if (!fMetadata.IsOwned())
		return;
	std::string name = message->GetString("name", "");
	std::string description = message->GetString("description", "");
	bool isPublic = message->GetBool("public", false);
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api || name.empty())
		return;
	BMessenger self(this);
	api->Playlists().UpdatePlaylistDetails(_PlaylistId(), name, description,
		isPublic, [self, name, description, isPublic](bool ok,
				const nlohmann::json& data) {
		BMessage result('pEdR');
		result.AddBool("ok", ok);
		result.AddString("name", name.c_str());
		result.AddString("description", description.c_str());
		result.AddBool("public", isPublic);
		result.AddString("error", SpotifyResponseErrorReason(data).c_str());
		self.SendMessage(&result);
	});
}


void
PlaylistWindow::_ApplyPlaylistCoverUploadResult(BMessage* message)
{
	PlaylistCoverResult result;
	if (!ReadPlaylistCoverResultMessage(*message, result) || !fCover.Complete(result))
		return;
	_UpdatePlaylistMenuState();
	if (result.error == PlaylistCoverError::None) {
		SetCoverUrl(result.coverUrl);
		_ReloadArtwork();
		return;
	}
	const char* text = nullptr;
	switch (result.error) {
		case PlaylistCoverError::FileRead:
			text = B_TRANSLATE("The selected cover file could not be read."); break;
		case PlaylistCoverError::NotJpeg:
			text = B_TRANSLATE("The selected file is not a JPEG."); break;
		case PlaylistCoverError::TooLarge:
			text = B_TRANSLATE("The Base64-encoded JPEG exceeds Spotify's 256 KB limit."); break;
		case PlaylistCoverError::RefreshFailed:
			text = B_TRANSLATE("The cover was uploaded, but its preview could not be refreshed. Please reopen the playlist."); break;
		default:
			text = B_TRANSLATE("Spotify could not upload the playlist cover."); break;
	}
	(new BAlert("", text, B_TRANSLATE("OK"), nullptr, nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go();
}


void
PlaylistWindow::_NotifyPlaylistDeleted()
{
	std::string id = _PlaylistId();
	if (!id.empty()) {
		BMessage changed(MSG_PLAYLISTS_CHANGED);
		changed.AddString("operation", "remove");
		changed.AddString("id", id.c_str());
		changed.AddString("uri", fUri.c_str());
		be_app->PostMessage(&changed);
	}
	PostMessage(B_QUIT_REQUESTED);
}


void
PlaylistWindow::_ApplyEpisodePage(BMessage* message)
{
	PlaylistPageResult result;
	if (!ReadPlaylistPageHeader(*message, result) || !fPaging.FinishPage(result).applied)
		return;
	bool append = result.request.offset > 0;
	BScrollBar* scrollBar = TrackVerticalScrollBar(fTrackList);
	float scrollValue = scrollBar ? scrollBar->Value() : 0.0f;

	if (!append)
		fEpisodes.clear();
	size_t firstNewEpisode = _AppendEpisodePageItems(message);
	_RenumberEpisodes();

	if (append)
		_AppendEpisodeRows(firstNewEpisode, fPaging.Filter());
	else
		_RebuildEpisodeList(fPaging.Filter());
	if (append && scrollBar)
		scrollBar->SetValue(scrollValue);

	_UpdateEpisodeInfo();

	_SaveCache();
	_CheckLazyLoad();
}


size_t
PlaylistWindow::_AppendEpisodePageItems(BMessage* message)
{
	return AppendMissingPlaylistEpisodes(fEpisodes,
		PlaylistEpisodesFromMessage(message));
}


void
PlaylistWindow::_ApplyPodcastHeadPage(BMessage* message)
{
	PlaylistPageResult result;
	if (!ReadPlaylistPageHeader(*message, result) || !fPaging.FinishHeadPage(result))
		return;
	if (!result.ok || !fPaging.HeadRefreshing()) {
		fPendingPodcastHeadEpisodes.clear();
		return;
	}
	bool reachedKnownEpisode = CollectMissingPlaylistHeadEpisodes(fEpisodes,
		PlaylistEpisodesFromMessage(message), fPendingPodcastHeadEpisodes);
	if (fPaging.ContinueHead(result, reachedKnownEpisode)) {
		_RefreshPodcastHead(result.nextOffset);
		return;
	}
	_FinishPodcastHeadRefresh();
}


void
PlaylistWindow::_TogglePodcastSubscription()
{
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemShow
			|| !fSubscriptionKnown || fSubscriptionPending)
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	std::string id = SpotifyItemIdForUri(fUri);
	bool target = !fIsSubscribed;
	fSubscriptionPending = true;
	if (fSubscribeButton)
		fSubscribeButton->SetEnabled(false);
	BMessenger self(this);
	if (!target) {
		api->Library().UnfollowShow(id, [self, target](bool ok,
				const nlohmann::json&) {
			BMessage msg('subU');
			msg.AddBool("ok", ok);
			msg.AddBool("following", target);
			msg.AddBool("changed", ok);
			msg.AddBool("show_error", !ok);
			self.SendMessage(&msg);
		});
	} else {
		api->Library().FollowShow(id, [self, target](bool ok,
				const nlohmann::json&) {
			BMessage msg('subU');
			msg.AddBool("ok", ok);
			msg.AddBool("following", target);
			msg.AddBool("changed", ok);
			msg.AddBool("show_error", !ok);
			self.SendMessage(&msg);
		});
	}
}


void
PlaylistWindow::_ScheduleEpisodeSearch()
{
	if (!fSearchBox)
		return;
	int32 generation = fPaging.ScheduleSearch();
	_UpdateEpisodeInfo();
	delete fEpisodeSearchRetryRunner;
	fEpisodeSearchRetryRunner = nullptr;
	delete fEpisodeSearchRunner;
	BMessage apply = MakePlaylistSearchMessage(false, generation);
	fEpisodeSearchRunner = new BMessageRunner(BMessenger(this), &apply,
		200000LL, 1);
}


void
PlaylistWindow::_ApplyEpisodeSearch(BMessage* message)
{
	int32 generation = 0;
	if (!ReadPlaylistSearchGeneration(*message, generation) || !fSearchBox
			|| !fPaging.ApplySearch(generation, fSearchBox->Text()))
		return;
	delete fEpisodeSearchRunner;
	fEpisodeSearchRunner = nullptr;
	_RebuildEpisodeList(fPaging.Filter());
	_UpdateEpisodeInfo();
	_CheckLazyLoad();
}


void
PlaylistWindow::_RetryEpisodeSearch(BMessage* message)
{
	int32 generation = 0;
	if (!ReadPlaylistSearchGeneration(*message, generation) || !fPaging.RetrySearch(generation))
		return;
	delete fEpisodeSearchRetryRunner;
	fEpisodeSearchRetryRunner = nullptr;
	_UpdateEpisodeInfo();
	_LoadNextPage();
}


void
PlaylistWindow::_ApplyEpisodeSelection(BMessage* message)
{
	const char* desc;
	if (fDescriptionView
			&& message->FindString("description", &desc) == B_OK) {
		ApplyMediaDescription(fDescriptionView, desc);
		fDescriptionView->SetLinks(MediaDescriptionLinks(desc));
	}
}


void
PlaylistWindow::_InitMenu()
{
	fMenuBar = new BMenuBar("MenuBar");

	SpotifyItemKind kind = SpotifyItemKindForUri(fUri);
	if (kind == kSpotifyItemAlbum) {
		BMenu* albumMenu = new BMenu(B_TRANSLATE("Album"));
		fAlbumSaveItem = new BMenuItem(B_TRANSLATE("Add to Saved Albums"),
			new BMessage(kMsgToggleAlbumSaved));
		albumMenu->AddItem(fAlbumSaveItem);
		albumMenu->AddSeparatorItem();
		albumMenu->AddItem(new BMenuItem(B_TRANSLATE("Reload Artwork"),
			new BMessage(kMsgReloadArtwork)));
		fMenuBar->AddItem(albumMenu);
		_UpdateAlbumMenuItem();
	} else if (kind == kSpotifyItemPlaylist) {
		BMenu* playlistMenu = new BMenu(B_TRANSLATE("Playlist"));
		fPlaylistEditItem = new BMenuItem(
			B_TRANSLATE("Edit Details" B_UTF8_ELLIPSIS),
			new BMessage(kMsgEditPlaylist));
		playlistMenu->AddItem(fPlaylistEditItem);
		fPlaylistCoverItem = new BMenuItem(
			B_TRANSLATE("Change Cover" B_UTF8_ELLIPSIS),
			new BMessage(kMsgChoosePlaylistCover));
		playlistMenu->AddItem(fPlaylistCoverItem);
		playlistMenu->AddSeparatorItem();
		playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Move Selected Up"),
			new BMessage(kMsgMovePlaylistItemUp)));
		playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Move Selected Down"),
			new BMessage(kMsgMovePlaylistItemDown)));
		fPlaylistClearItem = new BMenuItem(B_TRANSLATE("Clear Playlist"),
			new BMessage(kMsgClearPlaylist));
		playlistMenu->AddItem(fPlaylistClearItem);
		playlistMenu->AddSeparatorItem();
		fPlaylistDeleteItem = new BMenuItem(B_TRANSLATE("Delete Playlist"),
			new BMessage(kMsgDeletePlaylist));
		playlistMenu->AddItem(fPlaylistDeleteItem);
		fMenuBar->AddItem(playlistMenu);
		_UpdatePlaylistMenuState();
	} else if (kind != kSpotifyItemShow) {
		BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
		fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Close Window"),
			new BMessage(B_QUIT_REQUESTED), 'W'));
		fMenuBar->AddItem(fileMenu);
	}

	if (kind == kSpotifyItemShow) {
		BMenu* episodeMenu = new BMenu(B_TRANSLATE("Episodes"));
		episodeMenu->AddItem(new BMenuItem(B_TRANSLATE("Refresh Episodes"),
			new BMessage('rfEp')));
		fMenuBar->AddItem(episodeMenu);
	}
}


class DropFilter : public BMessageFilter {
public:
	DropFilter(PlaylistWindow* window)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE), fWindow(window) {}

	filter_result Filter(BMessage* message, BHandler** target) {
		if (message->what == MSG_DRAG_ITEM) {
			DEBUG_PRINT("DropFilter: caught 'drag' message! WasDropped=%d, target=%p\n", message->WasDropped(), *target);
			if (message->WasDropped()) {
				DEBUG_PRINT("DropFilter: forwarding dropped message to window\n");
				BMessage dropMsg(*message);
				// The reposted drop must bypass this common filter.
				dropMsg.what = MSG_PLAYLIST_DROP;
				fWindow->PostMessage(&dropMsg);
				return B_SKIP_MESSAGE;
			}
		}
		return B_DISPATCH_MESSAGE;
	}
private:
	PlaylistWindow* fWindow;
};

class HeaderContextFilter : public BMessageFilter {
public:
	HeaderContextFilter(PlaylistWindow* window, uint32 menuMessage)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
		  fWindow(window),
		  fMenuMessage(menuMessage) {}

	filter_result Filter(BMessage* message, BHandler** target) override {
		if (message->what != B_MOUSE_DOWN)
			return B_DISPATCH_MESSAGE;

		int32 buttons = 0;
		if (message->FindInt32("buttons", &buttons) != B_OK
				|| (buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
			return B_DISPATCH_MESSAGE;
		}

		BView* view = dynamic_cast<BView*>(*target);
		if (!view)
			return B_DISPATCH_MESSAGE;

		BPoint where;
		if (message->FindPoint("where", &where) != B_OK)
			where = BPoint(0, 0);
		view->ConvertToScreen(&where);

		BMessage menu(fMenuMessage);
		menu.AddPoint("screen_where", where);
		fWindow->PostMessage(&menu);
		return B_SKIP_MESSAGE;
	}

private:
	PlaylistWindow* fWindow;
	uint32 fMenuMessage;
};

void
PlaylistWindow::_InitLayout(const char* playlistName)
{
	const auto header = ResolvePlaylistHeader(fUri);
	SpotifyItemKind kind = header.kind;
	bool isPodcast = header.isPodcast;
	bool isAlbum = header.isAlbum;
	bool isLikedSongs = header.isLikedSongs;
	float artworkSize = MediaHeaderStyle::ArtworkSize();

	ArtworkView* coverView = new ArtworkView("CoverView");
	if (isLikedSongs && fCoverUrl.empty())
		coverView->AdoptBitmap(LoadLikedSongsArtwork(artworkSize));
	else
		coverView->ShowLoading();
	MediaHeaderStyle::ApplyArtworkSize(coverView);
	coverView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
	fCoverView = coverView;


	fPlaylistName = new BTextView("PlaylistName");
	fPlaylistName->SetText(playlistName);
	if (isAlbum) {
		BFont titleFont(be_bold_font);
		titleFont.SetSize(be_plain_font->Size()
			* MediaHeaderStyle::kTitleScale);
		fPlaylistName->SetFontAndColor(&titleFont);
	} else {
		fPlaylistName->SetFontAndColor(be_bold_font);
	}
	fPlaylistName->MakeEditable(false);
	fPlaylistName->MakeSelectable(false);
	fPlaylistName->SetWordWrap(true);
	fPlaylistName->SetInsets(0, 0, 0, 0);
	fPlaylistName->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fPlaylistName->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 60));
	fPlaylistName->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	fPlaylistInfo = new BStringView("PlaylistInfo", "");
	fPlaylistInfo->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fPlaylistInfo->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));

	_InitTrackList(kind);

	if (isPodcast) {
		const auto metrics = ResolvePodcastHeaderMetrics(UiScale::LineHeight(),
			MediaHeaderStyle::ActionButtonMinWidth(), MediaHeaderStyle::Scaled(170.0f));
		const float podcastInfoWidth = metrics.infoWidth;
		const float podcastTitleHeight = metrics.titleHeight;
		const float podcastSearchInfoHeight = metrics.searchInfoHeight;
		fPlaylistName->SetExplicitMinSize(BSize(
			podcastInfoWidth, B_SIZE_UNSET));
		fPlaylistName->SetExplicitPreferredSize(BSize(
			podcastInfoWidth, B_SIZE_UNSET));
		fPlaylistName->SetExplicitMaxSize(BSize(podcastInfoWidth,
			podcastTitleHeight));
		fPlaylistInfo->SetExplicitMinSize(BSize(
			podcastInfoWidth, B_SIZE_UNSET));
		fPlaylistInfo->SetExplicitPreferredSize(BSize(
			podcastInfoWidth, B_SIZE_UNSET));
		fPlaylistInfo->SetExplicitMaxSize(BSize(
			podcastInfoWidth, B_SIZE_UNSET));

		fPodcastSearchInfo = new BTextView("PodcastSearchInfo");
		fPodcastSearchInfo->MakeEditable(false);
		fPodcastSearchInfo->MakeSelectable(false);
		fPodcastSearchInfo->SetWordWrap(true);
		fPodcastSearchInfo->SetInsets(0, 0, 0, 0);
		fPodcastSearchInfo->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		fPodcastSearchInfo->SetExplicitMinSize(BSize(
			podcastInfoWidth, podcastSearchInfoHeight));
		fPodcastSearchInfo->SetExplicitPreferredSize(BSize(
			podcastInfoWidth, podcastSearchInfoHeight));
		fPodcastSearchInfo->SetExplicitMaxSize(BSize(
			podcastInfoWidth, podcastSearchInfoHeight));
		fPodcastSearchInfo->SetExplicitAlignment(BAlignment(
			B_ALIGN_LEFT, B_ALIGN_TOP));

		fSearchBox = new BTextControl("search", "", "", nullptr);
		fSearchBox->SetModificationMessage(new BMessage('srch'));
		fSearchBox->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));
		fSearchBox->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
			B_ALIGN_VERTICAL_CENTER));
		fSubscribeButton = new BButton("subBtn", B_TRANSLATE("Subscribe"),
			new BMessage('subS'));
		fSubscribeButton->SetExplicitMinSize(BSize(
			MediaHeaderStyle::ActionButtonMinWidth(), B_SIZE_UNSET));
		fSubscribeButton->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
			B_ALIGN_VERTICAL_CENTER));
		fSubscribeButton->SetEnabled(false);

		fDescriptionView = new MediaDescriptionView("DescriptionView");
		fDescriptionScroll = new BScrollView("DescScroll", fDescriptionView,
			0, false, true, B_FANCY_BORDER);
		fDescriptionScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET,
			MediaHeaderStyle::Scaled(72.0f)));
		fDescriptionScroll->SetExplicitPreferredSize(BSize(B_SIZE_UNSET,
			MediaHeaderStyle::Scaled(94.0f)));
		fDescriptionScroll->SetExplicitMaxSize(
			BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	}
	if (isAlbum) {
		fAlbumSaveButton = new BButton("saveAlbum",
			B_TRANSLATE("Add to Saved Albums"),
			new BMessage(kMsgToggleAlbumSaved));
		fAlbumSaveButton->SetExplicitMinSize(BSize(
			MediaHeaderStyle::ActionButtonMinWidth(), B_SIZE_UNSET));
		fAlbumSaveButton->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
			B_ALIGN_VERTICAL_CENTER));
		fAlbumSaveButton->SetEnabled(false);
		_UpdateAlbumMenuItem();
	}

	AddCommonFilter(new DropFilter(this));
	if (isAlbum) {
		fCoverView->AddFilter(new HeaderContextFilter(this, kMsgShowAlbumMenu));
		fPlaylistName->AddFilter(new HeaderContextFilter(this, kMsgShowAlbumMenu));
		_UpdateAlbumSavedState();
	} else if (kind == kSpotifyItemPlaylist) {
		fCoverView->AddFilter(new HeaderContextFilter(this,
			kMsgShowPlaylistMenu));
		fPlaylistName->AddFilter(new HeaderContextFilter(this,
			kMsgShowPlaylistMenu));
	}

	if (isPodcast) {
		_InitPodcastLayout();
	} else if (isAlbum) {
		_InitAlbumLayout(artworkSize);
	} else {
		_InitDefaultLayout();
	}

	SetSizeLimits(header.minimumWidth, 100000, header.minimumHeight, 100000);
}


void
PlaylistWindow::_InitTrackList(SpotifyItemKind kind)
{
	bool isPodcast = kind == kSpotifyItemShow;

	fTrackList = new TrackListView("TrackList", 0, B_PLAIN_BORDER, true);
	_UpdateTrackDropMarkerMode();
	fTrackList->SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	fTrackList->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	fTrackList->AddColumn(new TrackIntegerColumn("#", 40, 30, 60,
		B_ALIGN_RIGHT),
		0);
	fTrackList->AddColumn(new TrackStringColumn(B_TRANSLATE("Title"), 200,
		80, 400, B_TRUNCATE_END), 1);
	if (isPodcast) {
		fTrackList->AddColumn(new TrackStringColumn(B_TRANSLATE("Description"),
			280, 80, 600, B_TRUNCATE_END), 2);
	} else {
		fTrackList->AddColumn(new TrackStringColumn(B_TRANSLATE("Artist"), 140,
			60, 300, B_TRUNCATE_END), 2);
	}
	fBpmColumn = new TrackStringColumn("BPM", 50, 40, 80, B_TRUNCATE_END,
		B_ALIGN_RIGHT);
	fKeyColumn = new TrackStringColumn("Key", 50, 40, 80, B_TRUNCATE_END,
		B_ALIGN_CENTER);
	fTrackList->AddColumn(fBpmColumn, 3);
	fTrackList->AddColumn(fKeyColumn, 4);
	BStringColumn* dateAlbumColumn;
	if (isPodcast) {
		dateAlbumColumn = new TrackStringColumn(B_TRANSLATE("Date"), 90, 60,
			120, B_TRUNCATE_END);
	} else {
		dateAlbumColumn = new TrackStringColumn(B_TRANSLATE("Album"), 160, 60,
			350, B_TRUNCATE_END);
	}
	fTrackList->AddColumn(dateAlbumColumn, 5);
	fTrackList->AddColumn(new TrackStringColumn(B_TRANSLATE("Duration"), 86,
		70, 120, B_TRUNCATE_END, B_ALIGN_RIGHT), 6);
	fBpmColumn->SetVisible(false);
	fKeyColumn->SetVisible(false);
	if (kind == kSpotifyItemAlbum)
		dateAlbumColumn->SetVisible(false);
}


void
PlaylistWindow::_InitAlbumLayout(float artworkSize)
{
	fPlaylistName->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));
	fPlaylistName->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));
	fPlaylistInfo->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_VERTICAL_CENTER));

	BView* albumInfo = new BView("albumHeaderInfo", 0);
	albumInfo->SetExplicitMinSize(BSize(0, artworkSize));
	albumInfo->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, artworkSize));
	albumInfo->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, artworkSize));
	albumInfo->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));
	BLayoutBuilder::Group<>(albumInfo, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fPlaylistName, 0.0f)
		.AddGroup(B_HORIZONTAL, 0, 0.0f)
			.AddStrut(2.0f)
			.Add(fPlaylistInfo, 1.0f)
		.End()
		.AddGlue()
		.Add(fAlbumSaveButton, 0.0f)
	.End();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.SetInsets(0)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0.0f)
				.SetInsets(B_USE_DEFAULT_SPACING)
				.Add(fCoverView, 0.0f)
				.Add(albumInfo, 1.0f)
			.End()
			.Add(fTrackList, 1)
		.End()
	.End();
}


void
PlaylistWindow::_InitDefaultLayout()
{
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.SetInsets(0)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0.0f)
				.SetInsets(B_USE_DEFAULT_SPACING)
				.Add(fCoverView, 0.0f)
				.AddGroup(B_VERTICAL, 2, 1.0f)
					.Add(fPlaylistName, 0.0f)
					.Add(fPlaylistInfo, 0.0f)
					.AddGlue()
				.End()
			.End()
			.Add(fTrackList, 1)
		.End()
	.End();
}


void
PlaylistWindow::_InitPodcastLayout()
{
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.SetInsets(0)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0.0f)
				.SetInsets(B_USE_DEFAULT_SPACING)
				.Add(fCoverView, 0.0f)
				.AddGroup(B_VERTICAL, 2, 0.0f)
					.Add(fPlaylistName)
					.Add(fPlaylistInfo)
					.Add(fPodcastSearchInfo)
					.AddGlue()
					.Add(fSubscribeButton, 0.0f)
				.End()
				.AddGroup(B_VERTICAL, 4, 1.0f)
					.Add(fDescriptionScroll, 1.0f)
					.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 0.0f)
						.Add(new ResourceIconView("searchIcon",
							kSearchIconResource,
							MediaHeaderStyle::Scaled(16.0f)), 0.0f)
						.Add(fSearchBox, 1.0f)
					.End()
				.End()
			.End()
			.Add(fTrackList, 1)
		.End();
}



void
PlaylistWindow::ShowContextMenu(BView*, BPoint where, BPoint screenWhere)
{
	TrackRow* row = (TrackRow*)fTrackList->RowAt(where);
	if (!row || row->fTrackUri.empty()) return;

	fTrackList->DeselectAll();
	fTrackList->SetFocusRow(row, true);

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	ShowPlayableItemContextMenu(row->fTrackUri, fUri, screenWhere,
		BMessenger(this), api);
}

std::string
PlaylistWindow::_AlbumId() const
{
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemAlbum)
		return "";
	return SpotifyItemIdForUri(fUri);
}

std::string
PlaylistWindow::_PlaylistId() const
{
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemPlaylist)
		return "";
	return SpotifyItemIdForUri(fUri);
}

void
PlaylistWindow::_UpdateAlbumMenuItem()
{
	const char* label = fAlbumSaved
		? B_TRANSLATE("Remove from Saved Albums")
		: B_TRANSLATE("Add to Saved Albums");
	bool enabled = fAlbumSavedKnown && !fAlbumSavePending;
	if (fAlbumSaveItem) {
		fAlbumSaveItem->SetLabel(label);
		fAlbumSaveItem->SetMarked(fAlbumSavedKnown && fAlbumSaved);
		fAlbumSaveItem->SetEnabled(enabled);
	}
	if (fAlbumSaveButton) {
		fAlbumSaveButton->SetLabel(label);
		fAlbumSaveButton->SetEnabled(enabled);
	}
}

void
PlaylistWindow::_UpdateAlbumSavedState()
{
	std::string albumId = _AlbumId();
	if (albumId.empty())
		return;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;

	BMessenger self(this);
	api->Library().CheckSavedAlbums(albumId, [self](bool ok,
			const nlohmann::json& data) {
		BMessage state(kMsgAlbumSavedState);
		bool valid = ok && data.is_array() && !data.empty()
			&& data[0].is_boolean();
		state.AddBool("ok", valid);
		if (valid)
			state.AddBool("saved", data[0].get<bool>());
		self.SendMessage(&state);
	});
}

void
PlaylistWindow::_ShowAlbumContextMenu(BPoint screenWhere)
{
	if (_AlbumId().empty())
		return;

	BPopUpMenu* menu = new BPopUpMenu("album", false, false);
	BMessage* toggle = new BMessage(kMsgToggleAlbumSaved);
	BMenuItem* toggleItem = new BMenuItem(fAlbumSaved
		? B_TRANSLATE("Remove from Saved Albums")
		: B_TRANSLATE("Add to Saved Albums"), toggle);
	toggleItem->SetEnabled(fAlbumSavedKnown && !fAlbumSavePending);
	menu->AddItem(toggleItem);
	menu->AddSeparatorItem();
	BMenuItem* reload = new BMenuItem(B_TRANSLATE("Reload Artwork"),
		new BMessage(kMsgReloadArtwork));
	reload->SetEnabled(!fCoverUrl.empty());
	menu->AddItem(reload);

	BMenuItem* selected = menu->Go(screenWhere, false, true);
	if (selected && selected->Message())
		PostMessage(selected->Message());
	delete menu;
}

void
PlaylistWindow::_ReloadArtwork()
{
	if (fCoverUrl.empty() || !fCoverView)
		return;
	static_cast<ArtworkView*>(fCoverView)->ReloadUrl();
}

void
PlaylistWindow::_ShowPlaylistContextMenu(BPoint screenWhere)
{
	if (_PlaylistId().empty())
		return;

	const auto state = _PlaylistMenuState();
	BPopUpMenu* menu = new BPopUpMenu("playlist", false, false);
	BMenuItem* edit = new BMenuItem(B_TRANSLATE("Edit Details" B_UTF8_ELLIPSIS),
		new BMessage(kMsgEditPlaylist));
	edit->SetEnabled(state.edit);
	menu->AddItem(edit);
	BMenuItem* cover = new BMenuItem(B_TRANSLATE("Change Cover" B_UTF8_ELLIPSIS),
		new BMessage(kMsgChoosePlaylistCover));
	cover->SetEnabled(state.cover);
	menu->AddItem(cover);
	menu->AddSeparatorItem();
	BMenuItem* clear = new BMenuItem(B_TRANSLATE("Clear Playlist"),
		new BMessage(kMsgClearPlaylist));
	clear->SetEnabled(state.clear);
	menu->AddItem(clear);
	BMenuItem* remove = new BMenuItem(state.owned
		? B_TRANSLATE("Delete Playlist") : B_TRANSLATE("Unfollow Playlist"),
		new BMessage(kMsgDeletePlaylist));
	remove->SetEnabled(state.remove);
	menu->AddItem(remove);

	BMenuItem* selected = menu->Go(screenWhere, false, true);
	if (selected && selected->Message())
		PostMessage(selected->Message());
	delete menu;
}

void
PlaylistWindow::_UpdatePlaylistMenuState()
{
	const auto state = _PlaylistMenuState();
	if (fPlaylistEditItem) fPlaylistEditItem->SetEnabled(state.edit);
	if (fPlaylistCoverItem) fPlaylistCoverItem->SetEnabled(state.cover);
	if (fPlaylistClearItem) fPlaylistClearItem->SetEnabled(state.clear);
	if (fPlaylistDeleteItem) {
		fPlaylistDeleteItem->SetLabel(state.owned
			? B_TRANSLATE("Delete Playlist") : B_TRANSLATE("Unfollow Playlist"));
		fPlaylistDeleteItem->SetEnabled(state.remove);
	}
	_UpdateTrackDropMarkerMode();
}

PlaylistMenuState
PlaylistWindow::_PlaylistMenuState() const
{
	return ResolvePlaylistMenuState(ResolvePlaylistContentTarget(fUri), fMetadata.IsOwned(),
		fPaging.Position().total > 0 || (fTrackList && fTrackList->CountRows() > 0),
		_PlaylistMutationPending(), fCover.Pending(), fPlaylistDeletePending);
}


void
PlaylistWindow::_UpdateTrackDropMarkerMode()
{
	if (!fTrackList)
		return;
	bool mutationPending = _PlaylistMutationPending();
	fTrackList->SetDropFeedbackFlags(PlaylistCanAcceptDrop(fUri,
		fMetadata.IsOwned(), mutationPending) ? kDropFeedbackInsertMarker
			: kDropFeedbackNone);
}

void
PlaylistWindow::_ShowPlaylistDetailsDialog()
{
	if (!_PlaylistMenuState().edit) return;
	PlaylistDetailsDialog* dialog = new PlaylistDetailsDialog(
		fPlaylistName ? fPlaylistName->Text() : "", fMetadata.Description(),
		fMetadata.IsPublic(), BMessenger(this));
	dialog->MoveTo(Frame().left + 40, Frame().top + 40);
	dialog->Show();
}

void
PlaylistWindow::_ChoosePlaylistCover()
{
	if (!_PlaylistMenuState().cover) return;
	if (!fPlaylistCoverPanel) {
		fPlaylistCoverPanel = new BFilePanel(B_OPEN_PANEL,
			new BMessenger(this), nullptr, B_FILE_NODE, false,
			new BMessage(kMsgPlaylistCoverSelected));
	}
	fPlaylistCoverPanel->Show();
}

void
PlaylistWindow::_UploadPlaylistCover(const entry_ref& ref)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api || !_PlaylistMenuState().cover)
		return;
	PlaylistCoverCommand command;
	if (!fCover.Begin(_PlaylistId(), fMetadata.IsOwned(), command))
		return;
	_UpdatePlaylistMenuState();
	if (!PlaylistCoverRequests::Send(api->Playlists(), command, ref, BMessenger(this))) {
		PlaylistCoverResult failure;
		failure.command = command;
		BMessage message = MakePlaylistCoverResultMessage(failure);
		_ApplyPlaylistCoverUploadResult(&message);
	}
}


void
PlaylistWindow::_ClearPlaylist()
{
	if (!_PlaylistMenuState().clear || !fTrackList)
		return;
	BAlert* alert = new BAlert("", B_TRANSLATE("Remove every item from this playlist?"),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Clear Playlist"), nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	if (alert->Go() != 1)
		return;
	App* app = dynamic_cast<App*>(be_app);
	if (!app || !app->GetApi())
		return;
	PlaylistWriteCommand command;
	if (!fClear.Begin(_PlaylistWriteContext(), command))
		return;
	for (int32 index = 0; index < fTrackList->CountRows(); index++) {
		BRow* row = fTrackList->RowAt(index);
		fPendingPlaylistClear.rows.push_back(row);
		fPendingPlaylistClear.selected.push_back(_IsRowSelected(row));
	}
	for (auto row = fPendingPlaylistClear.rows.rbegin();
			row != fPendingPlaylistClear.rows.rend(); ++row)
		fTrackList->RemoveRow(*row);
	fPaging.RestorePosition(command.optimisticPage);
	_DeleteCache();
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();
	_SendPlaylistWrite(command);
}

void
PlaylistWindow::_FinishClearPlaylist(bool success)
{
	for (size_t index = 0; index < fPendingPlaylistClear.rows.size(); index++) {
		BRow* row = fPendingPlaylistClear.rows[index];
		if (success)
			delete row;
		else {
			fTrackList->AddRow(row);
			if (fPendingPlaylistClear.selected[index])
				fTrackList->AddToSelection(row);
		}
	}
	fPendingPlaylistClear = {};
}

bool
PlaylistWindow::_PlaylistMutationPending() const
{
	return fRemoval.Pending() || fReorder.Pending() || fClear.Pending() || fAdd.Pending()
		|| fPlaylistDeletePending;
}

PlaylistWriteContext
PlaylistWindow::_PlaylistWriteContext() const
{
	PlaylistWriteContext context;
	context.playlistId = _PlaylistId();
	context.snapshotId = fPlaylistSnapshotId;
	context.page = fPaging.Position();
	context.rowCount = fTrackList ? fTrackList->CountRows() : 0;
	context.owned = fMetadata.IsOwned();
	context.otherMutationPending = _PlaylistMutationPending();
	return context;
}

void
PlaylistWindow::_SendPlaylistWrite(const PlaylistWriteCommand& command)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (api && PlaylistWriteRequests::Send(api->Playlists(), command, BMessenger(this)))
		return;
	PlaylistWriteResult failure;
	failure.kind = command.kind;
	failure.requestId = command.requestId;
	failure.playlistId = command.playlistId;
	BMessage message = MakePlaylistWriteMessage(failure);
	_ApplyPlaylistWriteResult(&message);
}


void
PlaylistWindow::_FinishTrackRemoval(const PlaylistRemovalUpdate& update)
{
	if (update.action == PlaylistRemovalAction::Commit)
		_ApplyFinishedTrackRemoval(update);
	else
		_RollbackFinishedTrackRemoval();

	fPendingTrackRemovals.clear();
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();
	if (update.action == PlaylistRemovalAction::Commit)
		_CheckLazyLoad();
}


void
PlaylistWindow::_ApplyFinishedTrackRemoval(const PlaylistRemovalUpdate& update)
{
	for (const PendingTrackRemoval& pending : fPendingTrackRemovals)
		delete pending.row;
	for (int32 i = 0; fTrackList && i < fTrackList->CountRows(); i++) {
		TrackRow* row = dynamic_cast<TrackRow*>(fTrackList->RowAt(i));
		if (!row || row->fPlaylistPosition < 0)
			continue;
		row->fPlaylistPosition = update.PositionAfterRemoval(row->fPlaylistPosition);
		if (BIntegerField* number = dynamic_cast<BIntegerField*>(row->GetField(0)))
			number->SetValue(row->fPlaylistPosition + 1);
		fTrackList->UpdateRow(row);
	}
	fPaging.RestorePosition(update.page);
	fPlaylistSnapshotId.clear();
	fCachedPlaylistSnapshotId.clear();
	_DeleteCache();
}


void
PlaylistWindow::_RollbackFinishedTrackRemoval()
{
	if (!fTrackList)
		return;

	for (const PendingTrackRemoval& pending : fPendingTrackRemovals)
		fTrackList->AddRow(pending.row, pending.listIndex);
	for (const PendingTrackRemoval& pending : fPendingTrackRemovals) {
		if (pending.selected)
			fTrackList->AddToSelection(pending.row);
	}
}


void
PlaylistWindow::_RefreshPlaylistSnapshot()
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (api)
		PlaylistMetadataRequests::RefreshSnapshot(*api, _PlaylistId(), BMessenger(this));
}


void
PlaylistWindow::_UpdatePlaylistTrackInfo()
{
	if (fPlaylistInfo && fTrackList)
		SetCachedTrackInfo(fPlaylistInfo, true, fPaging.Position().total, fTrackList->CountRows());
}


void
PlaylistWindow::_ApplyPlaylistPositions(const std::vector<int32_t>& positions)
{
	if (!fTrackList || positions.size() != static_cast<size_t>(fTrackList->CountRows()))
		return;
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		TrackRow* row = dynamic_cast<TrackRow*>(fTrackList->RowAt(i));
		if (!row)
			continue;
		row->fPlaylistPosition = positions[i];
		if (BIntegerField* number = dynamic_cast<BIntegerField*>(
				row->GetField(0))) {
			number->SetValue(positions[i] + 1);
		}
		fTrackList->UpdateRow(row);
	}
}


void
PlaylistWindow::_BeginTrackReorder(const std::vector<int32_t>& indices,
	int32 insertBefore)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api || !fTrackList)
		return;
	PlaylistReorderContext context;
	context.playlistId = _PlaylistId();
	context.snapshotId = fPlaylistSnapshotId;
	context.rowCount = fTrackList->CountRows();
	context.owned = fMetadata.IsOwned();
	context.pageLoading = fPaging.Loading();
	context.otherMutationPending = _PlaylistMutationPending();
	context.loadedEnd = fPaging.Position().offset;
	context.total = fPaging.Position().total;
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		auto row = dynamic_cast<TrackRow*>(fTrackList->RowAt(i));
		if (!row)
			return;
		context.visiblePositions.push_back(row->fPlaylistPosition);
	}
	PlaylistReorderCommand command;
	if (!fReorder.Begin(context, indices, insertBefore, command))
		return;

	if (_BuildPendingTrackReorder(command.selectedIndices)) {
		fPaging.RestorePosition(fPaging.Position());
		_DeleteCache();
		std::vector<int32_t> targets;
		for (size_t i = 0; i < command.selectedIndices.size(); i++)
			targets.push_back(command.selectionTarget + static_cast<int32_t>(i));
		_ApplyPendingTrackReorder(targets);
		_ApplyPlaylistPositions(command.visiblePositions);
		_UpdatePlaylistMenuState();
		_SendTrackReorder(command);
		return;
	}
	PlaylistReorderResult failure;
	failure.requestId = command.requestId;
	failure.playlistId = command.playlistId;
	BMessage result = MakePlaylistReorderMessage(failure);
	_ApplyTrackReorderResult(&result);
}

void
PlaylistWindow::_SendTrackReorder(const PlaylistReorderCommand& command)
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (api && PlaylistReorderRequests::Send(api->Playlists(), command, BMessenger(this)))
		return;
	PlaylistReorderResult failure;
	failure.requestId = command.requestId;
	failure.playlistId = command.playlistId;
	BMessage message = MakePlaylistReorderMessage(failure);
	_ApplyTrackReorderResult(&message);
}


bool
PlaylistWindow::_BuildPendingTrackReorder(const std::vector<int32_t>& indices)
{
	fPendingTrackReorder = PendingTrackReorder();
	for (int32_t index : indices) {
		BRow* row = fTrackList->RowAt(index);
		if (!row) {
			fPendingTrackReorder = PendingTrackReorder();
			return false;
		}
		fPendingTrackReorder.rows.push_back(row);
		fPendingTrackReorder.selected.push_back(_IsRowSelected(row));
	}
	return true;
}


bool
PlaylistWindow::_IsRowSelected(BRow* row) const
{
	for (BRow* current = fTrackList->CurrentSelection(); current;
			current = fTrackList->CurrentSelection(current)) {
		if (current == row)
			return true;
	}
	return false;
}


void
PlaylistWindow::_ApplyPendingTrackReorder(const std::vector<int32_t>& indices)
{
	if (fPendingTrackReorder.rows.empty())
		return;
	for (auto row = fPendingTrackReorder.rows.rbegin();
			row != fPendingTrackReorder.rows.rend(); ++row) {
		fTrackList->RemoveRow(*row);
	}
	for (int32 i = 0; i < (int32)fPendingTrackReorder.rows.size(); i++) {
		fTrackList->AddRow(fPendingTrackReorder.rows[i],
			indices[i]);
		if (fPendingTrackReorder.selected[i])
			fTrackList->AddToSelection(fPendingTrackReorder.rows[i]);
	}
}


void
PlaylistWindow::_FinishTrackReorder(const PlaylistReorderUpdate& update)
{
	if (update.action == PlaylistReorderAction::Commit) {
		_ApplyPlaylistPositions(update.visiblePositions);
		fPlaylistSnapshotId = update.snapshotId;
		fCachedPlaylistSnapshotId = update.snapshotId;
		_DeleteCache();
		if (!update.snapshotId.empty())
			_SaveCache();
	} else {
		_ApplyPendingTrackReorder(update.restoreIndices);
		_ApplyPlaylistPositions(update.visiblePositions);
		if (update.action == PlaylistReorderAction::Rollback)
			_SaveCache();
		else {
			fPlaylistSnapshotId.clear();
			fCachedPlaylistSnapshotId.clear();
			_DeleteCache();
		}
	}
	fPendingTrackReorder = PendingTrackReorder();
	_UpdatePlaylistMenuState();
	if (fCloseAfterReorder) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	if (update.action == PlaylistReorderAction::Commit)
		_CheckLazyLoad();
}


void
PlaylistWindow::_MoveSelectedItem(int32 delta)
{
	if (!_CanMoveSelectedItems(delta))
		return;
	int32 first = fTrackList->CountRows();
	int32 last = -1;
	int32 count = 0;
	if (!_SelectedRowSpan(first, last, count))
		return;
	auto step = ResolvePlaylistReorderStep(first, last, count,
		fTrackList->CountRows(), delta);
	if (step.noncontiguous) {
		_ShowContiguousSelectionAlert();
		return;
	}
	if (step.shouldMove)
		_BeginTrackReorder(fTrackList->SelectedRowIndices(), step.insertBefore);
}


bool
PlaylistWindow::_CanMoveSelectedItems(int32 delta) const
{
	return fMetadata.IsOwned() && fTrackList && !_PlaylistMutationPending() && delta != 0;
}


bool
PlaylistWindow::_SelectedRowSpan(int32& source, int32& last,
	int32& selectedCount) const
{
	for (BRow* selected = fTrackList->CurrentSelection(); selected;
			selected = fTrackList->CurrentSelection(selected)) {
		for (int32 i = 0; i < fTrackList->CountRows(); i++) {
			if (fTrackList->RowAt(i) == selected) {
				source = std::min(source, i);
				last = std::max(last, i);
				selectedCount++;
				break;
			}
		}
	}
	return selectedCount > 0;
}


void
PlaylistWindow::_ShowContiguousSelectionAlert() const
{
	BAlert* alert = new BAlert("", B_TRANSLATE(
		"Only a contiguous selection can be moved together."),
		B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
		B_INFO_ALERT);
	alert->Go();
}

void
PlaylistWindow::_DeletePlaylist()
{
	if (!_PlaylistMenuState().remove)
		return;

	std::string playlistId = _PlaylistId();
	if (playlistId.empty())
		return;

	const char* body = fMetadata.IsOwned()
		? B_TRANSLATE("Really delete this playlist? This cannot be undone.")
		: B_TRANSLATE("Unfollow this playlist?");
	const char* action = fMetadata.IsOwned()
		? B_TRANSLATE("Delete Playlist") : B_TRANSLATE("Unfollow Playlist");
	BAlert* alert = new BAlert("", body,
		B_TRANSLATE("Cancel"), action, nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	if (alert->Go() != 1)
		return;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;

	fPlaylistDeletePending = true;
	_UpdatePlaylistMenuState();

	BMessenger self(this);
	api->Playlists().UnfollowPlaylist(playlistId, [self](bool ok,
			const nlohmann::json&) {
		self.SendMessage(ok ? kMsgPlaylistDeleted : kMsgPlaylistDeleteFailed);
	});
}

void
PlaylistWindow::_ToggleAlbumSaved()
{
	std::string albumId = _AlbumId();
	if (albumId.empty() || !fAlbumSavedKnown || fAlbumSavePending)
		return;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;

	bool remove = fAlbumSaved;
	fAlbumSavePending = true;
	_UpdateAlbumMenuItem();
	BMessenger self(this);
	auto callback = [self, remove](bool ok, const nlohmann::json&) {
		BMessage state(kMsgAlbumSavedState);
		state.AddBool("ok", ok);
		state.AddBool("saved", !remove);
		state.AddBool("changed", ok);
		state.AddBool("show_error", !ok);
		self.SendMessage(&state);
	};

	if (remove)
		api->Library().RemoveSavedAlbum(albumId, callback);
	else
		api->Library().SaveAlbum(albumId, callback);
}

void
PlaylistWindow::SetPlayingTrack(const char* trackUri)
{
	std::string nextUri = trackUri ? trackUri : "";
	if (nextUri == fCurrentPlayingTrackUri)
		return;
	fCurrentPlayingTrackUri = nextUri;
	if (!fTrackList)
		return;
	for (int i = 0; i < fTrackList->CountRows(); i++) {
		TrackRow* row = (TrackRow*)fTrackList->RowAt(i);
		if (row) {
			bool isPlaying = !fCurrentPlayingTrackUri.empty()
				&& row->fTrackUri == fCurrentPlayingTrackUri;
			if (row->SetPlaying(isPlaying))
				fTrackList->InvalidateRow(row);
		}
	}
}

void
PlaylistWindow::_AddFollowingTrackQueue(BMessage& play,
	const std::string& trackUri) const
{
	if (!fTrackList || trackUri.empty())
		return;

	bool found = false;
	for (int32 index = 0; index < fTrackList->CountRows(); index++) {
		TrackRow* row = (TrackRow*)fTrackList->RowAt(index);
		if (!row || row->fTrackUri.empty())
			continue;
		if (!found) {
			found = row->fTrackUri == trackUri;
			continue;
		}
		play.AddString(MessageFields::NextQueueUri, row->fTrackUri.c_str());
	}
}


void
PlaylistWindow::_AddPodcastNowPlayingContext(BMessage& play) const
{
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemShow)
		return;

	const char* showName = fPlaylistName ? fPlaylistName->Text() : "";
	if (showName && showName[0])
		play.AddString("artist", showName);
	play.AddString(kNowPlayingItemKindField, "episode");
	play.AddString(kNowPlayingPrimaryOpenUriField, fUri.c_str());
	play.AddString(kNowPlayingParentUriField, fUri.c_str());
	play.AddString(kNowPlayingParentKindField, "show");
	play.AddString(kNowPlayingShowIdField, SpotifyItemIdForUri(fUri).c_str());
}

void
PlaylistWindow::_LoadData(bool ignoreEpisodeCache)
{
	if (_PlaylistMutationPending())
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || fUri.empty()) return;

	_ResetLoadState();

	bool loadFirstPage = true;

	PlaylistContentTarget target = ResolvePlaylistContentTarget(fUri);
	if (target.isCollection) {
		loadFirstPage = _PrepareCollectionLoad(*api);
	} else if (target.kind == kSpotifyItemPlaylist) {
		loadFirstPage = _PreparePlaylistLoad(*api, target.id);
	} else if (target.kind == kSpotifyItemAlbum) {
		loadFirstPage = _PrepareAlbumLoad(*api, target.id);
	} else if (target.kind == kSpotifyItemShow) {
		loadFirstPage = _PrepareShowLoad(*api, target.id,
			ignoreEpisodeCache);
	}

	if (loadFirstPage)
		_LoadNextPage();
}


void
PlaylistWindow::_ResetLoadState()
{
	fPaging.Reset(fSearchBox ? fSearchBox->Text() : "");
	fPlaylistSnapshotId.clear();
	fCachedPlaylistSnapshotId.clear();
	delete fEpisodeSearchRetryRunner;
	fEpisodeSearchRetryRunner = nullptr;
	delete fEpisodeSearchRunner;
	fEpisodeSearchRunner = nullptr;
	fEpisodes.clear();
	fPendingPodcastHeadEpisodes.clear();
	if (fTrackList)
		fTrackList->Clear();
}


bool
PlaylistWindow::_PrepareCollectionLoad(SpotifyApi& api)
{
	if (_LoadCache())
		return false;

	api.Library().InvalidateSavedTracks();
	return true;
}


bool
PlaylistWindow::_PreparePlaylistLoad(SpotifyApi& api,
	const std::string& playlistId)
{
	bool loadFirstPage = !_LoadCache();
	BMessenger messenger(this);

	api.Playlists().InvalidatePlaylist(playlistId);
	PlaylistMetadataRequests::Load(api,
		{PlaylistMetadataKind::Playlist, playlistId}, messenger);
	PlaylistMetadataRequests::Load(api,
		{PlaylistMetadataKind::CurrentUser, ""}, messenger);

	return loadFirstPage;
}


bool
PlaylistWindow::_PrepareAlbumLoad(SpotifyApi& api, const std::string& albumId)
{
	BMessenger messenger(this);
	PlaylistMetadataRequests::Load(api,
		{PlaylistMetadataKind::Album, albumId}, messenger);
	return true;
}


bool
PlaylistWindow::_PrepareShowLoad(SpotifyApi& api, const std::string& showId,
	bool ignoreEpisodeCache)
{
	BMessenger messenger(this);

	if (ignoreEpisodeCache)
		api.Content().InvalidateShowEpisodes(showId);

	api.Library().CheckFollowingShow(showId, [messenger](bool ok,
			const nlohmann::json& data) {
		SendShowSubscriptionMessage(messenger, ok, data);
	});

	PlaylistMetadataRequests::Load(api,
		{PlaylistMetadataKind::Podcast, showId}, messenger);

	if (ignoreEpisodeCache || !_LoadCache())
		return true;

	_RebuildEpisodeList(fPaging.Filter());
	_UpdateEpisodeInfo();
	if (!fCurrentPlayingTrackUri.empty())
		SetPlayingTrack(fCurrentPlayingTrackUri.c_str());
	_RefreshPodcastHead(0);
	return false;
}


void
PlaylistWindow::_LoadNextPage()
{
	if (_PlaylistMutationPending())
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || fUri.empty())
		return;
	PlaylistPageRequest request = MakePlaylistPageRequest(
		ResolvePlaylistContentTarget(fUri), fPaging.Position().offset, fPageBatchSize);
	request.albumName = fPlaylistName ? fPlaylistName->Text() : "Album";
	if (!fPaging.BeginPage(request))
		return;
	if (!PlaylistPageRequests::Load(*api, request, BMessenger(this)))
		fPaging.DispatchFailed(request);
}


void
PlaylistWindow::_CheckLazyLoad()
{
	if (_CanLazyLoadPage() && fPaging.ShouldLoad(_ShouldLoadNextPageForScroll()))
		_LoadNextPage();
}


bool
PlaylistWindow::_CanLazyLoadPage() const
{
	return fTrackList && fPaging.CanLoad() && !_PlaylistMutationPending();
}


bool
PlaylistWindow::_ShouldLoadNextPageForScroll() const
{
	BScrollBar* scrollBar = TrackVerticalScrollBar(fTrackList);
	if (!scrollBar)
		return fTrackList->CountRows() == 0;

	float min = 0.0f;
	float max = 0.0f;
	scrollBar->GetRange(&min, &max);
	float value = scrollBar->Value();
	return (max <= 0.0f && fTrackList->CountRows() == 0)
		|| value >= max - 220.0f;
}

void
PlaylistWindow::_RebuildEpisodeList(const std::string& filter)
{
	fTrackList->Clear();

	std::string normalizedFilter = NormalizePlaylistEpisodeFilter(filter);

	for (const auto& ep : fEpisodes) {
		if (!PlaylistEpisodeMatchesFilter(ep, normalizedFilter))
			continue;
		fTrackList->AddRow(PlaylistEpisodeRowFromEpisode(ep,
			fCurrentPlayingTrackUri));
	}
}

void
PlaylistWindow::_AppendEpisodeRows(size_t firstEpisode,
	const std::string& filter)
{
	std::string normalizedFilter = NormalizePlaylistEpisodeFilter(filter);

	for (size_t index = firstEpisode; index < fEpisodes.size(); index++) {
		const PlaylistEpisode& episode = fEpisodes[index];
		if (!PlaylistEpisodeMatchesFilter(episode, normalizedFilter))
			continue;

		fTrackList->AddRow(PlaylistEpisodeRowFromEpisode(episode,
			fCurrentPlayingTrackUri));
	}
}

void
PlaylistWindow::_UpdateEpisodeInfo()
{
	if (!fPlaylistInfo)
		return;

	char info[64];
	if (fPaging.Position().total > (int32)fEpisodes.size())
		snprintf(info, sizeof(info),
			B_TRANSLATE("Podcast \xC2\xB7 %d/%d Episodes"),
			(int)fEpisodes.size(), (int)fPaging.Position().total);
	else
		snprintf(info, sizeof(info),
			B_TRANSLATE("Podcast \xC2\xB7 %d Episodes"),
			(int)fEpisodes.size());
	fPlaylistInfo->SetText(info);

	if (!fPodcastSearchInfo)
		return;
	if (fPaging.Filter().empty()) {
		fPodcastSearchInfo->SetText("");
		return;
	}

	char searchInfo[96];
	int32 matches = fTrackList ? fTrackList->CountRows() : 0;
	if (fPaging.WaitingRetry()) {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - retrying search..."), (int)matches);
	} else if (fPaging.Searching() && fPaging.Position().total > 0) {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - searching: %d/%d"), (int)matches,
			(int)fPaging.Position().offset, (int)fPaging.Position().total);
	} else if (fPaging.Searching()) {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - searching: %d"), (int)matches,
			(int)fPaging.Position().offset);
	} else if (fPaging.SearchFailed() && fPaging.Position().total > 0) {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - stopped: %d/%d"), (int)matches,
			(int)fPaging.Position().offset, (int)fPaging.Position().total);
	} else {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - searched: %d"), (int)matches,
			(int)fPaging.Position().offset);
	}
	fPodcastSearchInfo->SetText(searchInfo);
}

void
PlaylistWindow::_RenumberEpisodes()
{
	for (size_t i = 0; i < fEpisodes.size(); i++)
		fEpisodes[i].number = (int32)i + 1;
}

void
PlaylistWindow::_RefreshPodcastHead(int32 offset)
{
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemShow || fEpisodes.empty())
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	PlaylistPageRequest request = MakePlaylistPageRequest(
		ResolvePlaylistContentTarget(fUri), offset, fPageBatchSize);
	request.headRefresh = true;
	if (!fPaging.BeginHead(request))
		return;
	if (offset == 0) {
		fPendingPodcastHeadEpisodes.clear();
		api->Content().InvalidateShowEpisodes(SpotifyItemIdForUri(fUri));
	}
	if (!PlaylistPageRequests::Load(*api, request, BMessenger(this))) {
		fPaging.DispatchFailed(request);
		fPendingPodcastHeadEpisodes.clear();
	}
}


void
PlaylistWindow::_FinishPodcastHeadRefresh()
{
	if (!fPendingPodcastHeadEpisodes.empty()) {
		fEpisodes.insert(fEpisodes.begin(), fPendingPodcastHeadEpisodes.begin(),
			fPendingPodcastHeadEpisodes.end());
		fPendingPodcastHeadEpisodes.clear();
		_RenumberEpisodes();
		_RebuildEpisodeList(fPaging.Filter());
		_SaveCache();
	} else {
		fPendingPodcastHeadEpisodes.clear();
	}

	fPaging.FinishHeadRefresh((int32)fEpisodes.size());
	_UpdateEpisodeInfo();
	_CheckLazyLoad();
}

void
PlaylistWindow::_LoadMoreEpisodes()
{
	_LoadNextPage();
}

bool
PlaylistWindow::_LoadCache()
{
	if (PlaylistCacheFiles::UriUsesTrackCache(fUri))
		return _LoadTrackCache();
	if (PlaylistCacheFiles::UriUsesShowCache(fUri))
		return _LoadShowCache();
	return false;
}


bool
PlaylistWindow::_LoadTrackCache()
{
	try {
		PlaylistCacheFiles::TrackDocument document;
		bool isPlaylist = false;
		if (!PlaylistCacheFiles::ReadTrackDocumentForUri(fUri, document,
				&isPlaylist)) {
			return false;
		}
		if (isPlaylist) {
			fCachedPlaylistSnapshotId = document.snapshotId;
			fPlaylistSnapshotId = fCachedPlaylistSnapshotId;
		}

		if (fTrackList)
			fTrackList->Clear();
		AddCachedTrackRows(document.tracks, fTrackList, fCurrentPlayingTrackUri);
		CachedPageState pageState = ResolveCachedTrackPageState(document.nextOffset,
			document.total, fTrackList->CountRows());
		fPaging.RestorePosition({pageState.offset, pageState.total, pageState.hasMore});
		SetCachedTrackInfo(fPlaylistInfo, isPlaylist, fPaging.Position().total,
			fTrackList->CountRows());

		return fTrackList->CountRows() > 0;
	} catch (...) {
		if (fTrackList)
			fTrackList->Clear();
		return false;
	}
}


bool
PlaylistWindow::_LoadShowCache()
{
	try {
		PlaylistCacheFiles::ShowDocument document;
		if (!PlaylistCacheFiles::ReadShowDocumentForUri(fUri, document)) {
			return false;
		}
		fEpisodes.clear();
		for (const PlaylistCacheDocument::Episode& cached
				: document.episodes) {
			fEpisodes.push_back(PlaylistEpisodeFromCache(cached));
		}
		_RenumberEpisodes();
		CachedPageState pageState = ResolveCachedEpisodePageState(
			document.nextOffset, document.total, (int32)fEpisodes.size(),
			document.hasNextOffset);
		if (!pageState.valid) {
			fEpisodes.clear();
			return false;
		}
		fPaging.RestorePosition({pageState.offset, pageState.total, pageState.hasMore});
		return !fEpisodes.empty();
	} catch (...) {
		fEpisodes.clear();
		return false;
	}
}


void
PlaylistWindow::_SaveCache()
{
	delete fCacheSaveRunner;
	BMessage save(kMsgSaveCache);
	fCacheSaveRunner = new BMessageRunner(BMessenger(this), &save,
		750000LL, 1);
}

void
PlaylistWindow::_WriteCacheNow()
{
	if (PlaylistCacheFiles::UriUsesTrackCache(fUri)) {
		_WriteTrackCache();
		return;
	}

	if (PlaylistCacheFiles::UriUsesShowCache(fUri))
		_WriteShowCache();
}


bool
PlaylistWindow::_WriteTrackCache()
{
	if (!fTrackList || _PlaylistMutationPending())
		return false;
	bool isPlaylist = PlaylistCacheFiles::UriUsesPlaylistTrackCache(fUri);
	if (isPlaylist && fPlaylistSnapshotId.empty())
		return false;

	std::vector<PlaylistCacheDocument::Track> tracks;
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		TrackRow* row = (TrackRow*)fTrackList->RowAt(i);
		if (row)
			tracks.push_back(CachedTrackFromRow(row, i));
	}

	return PlaylistCacheFiles::WriteTrackDocumentForUri(fUri, fPaging.Position().total,
		fPaging.Position().offset, fPlaylistSnapshotId, tracks);
}


void
PlaylistWindow::_WriteShowCache()
{
	std::vector<PlaylistCacheDocument::Episode> episodes;
	for (const PlaylistEpisode& ep : fEpisodes)
		episodes.push_back(CacheEpisodeFromPlaylistEpisode(ep));

	PlaylistCacheFiles::WriteShowDocumentForUri(fUri, fPaging.Position().total,
		fPaging.Position().offset,
		fPaging.Position().total <= 0 || fPaging.Position().offset >= fPaging.Position().total, episodes);
}

void
PlaylistWindow::_DeleteCache()
{
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	PlaylistCacheFiles::RemoveForUri(fUri);
}


PlaylistWindow::~PlaylistWindow()
{
	delete fPlaylistCoverPanel;
	delete fEpisodeSearchRunner;
	delete fEpisodeSearchRetryRunner;
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	if (!_PlaylistMutationPending())
		_WriteCacheNow();
	delete fLazyLoadRunner;
	for (const PendingTrackRemoval& pending : fPendingTrackRemovals)
		delete pending.row;
	fPendingTrackRemovals.clear();
	for (BRow* row : fPendingPlaylistClear.rows)
		delete row;
	fPendingPlaylistClear.rows.clear();
	BRect frame = Frame();
	SettingsController::Update([&](HaifySettings& s) {
		s.playlistWindowX = frame.left;
		s.playlistWindowY = frame.top;
		s.playlistWindowW = frame.Width();
		s.playlistWindowH = frame.Height();
	});
}
