#include "PlaylistWindow.h"
#include "ArtworkView.h"
#include "DescriptionTextFormatter.h"
#include "MediaDescriptionView.h"
#include "TrackContextMenu.h"
#include "TextInputDialog.h"
#include "MediaHeaderStyle.h"
#include "Messages.h"
#include "NowPlayingFields.h"
#include "SettingsController.h"
#include "HaifyDebug.h"
#include "App.h"
#include "UiLogic.h"
#include "playlist/PlaylistCacheDocument.h"
#include "playlist/PlaylistCacheFiles.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"
#include "spotify/api/SpotifyResponse.h"

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
#include <File.h>
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
#include <DateFormat.h>
#include <DateTime.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
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
static const uint32 kMsgApplyEpisodeSearch = 'aEps';
static const uint32 kMsgRetryEpisodeSearch = 'rEps';
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


static std::string
Base64Encode(const std::vector<uint8>& bytes)
{
	static const char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string result;
	result.reserve(((bytes.size() + 2) / 3) * 4);
	for (size_t i = 0; i < bytes.size(); i += 3) {
		uint32 value = (uint32)bytes[i] << 16;
		if (i + 1 < bytes.size()) value |= (uint32)bytes[i + 1] << 8;
		if (i + 2 < bytes.size()) value |= bytes[i + 2];
		result += alphabet[(value >> 18) & 0x3f];
		result += alphabet[(value >> 12) & 0x3f];
		result += i + 1 < bytes.size() ? alphabet[(value >> 6) & 0x3f] : '=';
		result += i + 2 < bytes.size() ? alphabet[value & 0x3f] : '=';
	}
	return result;
}


class TrackStringField : public BStringField {
public:
	bool fIsPlaying;
	TrackStringField(const char* string) : BStringField(string), fIsPlaying(false) {}
};

class TrackStringColumn : public BStringColumn {
public:
	TrackStringColumn(const char* title, float width, float minWidth, float maxWidth,
		uint32 truncate, alignment align = B_ALIGN_LEFT)
		: BStringColumn(title, width, minWidth, maxWidth, truncate, align) {}

	virtual void DrawField(BField* field, BRect rect, BView* parent) {
		TrackStringField* f = dynamic_cast<TrackStringField*>(field);
		if (!f || !f->fIsPlaying) {
			BStringColumn::DrawField(field, rect, parent);
			return;
		}
		BFont font;
		parent->GetFont(&font);
		BFont boldFont(be_bold_font);
		boldFont.SetSize(font.Size());
		parent->SetFont(&boldFont);
		BStringColumn::DrawField(field, rect, parent);
		parent->SetFont(&font);
	}
};

class TrackRow : public BRow {
public:
	std::string fTrackUri;
	std::string fArtistUri;
	std::string fAlbumUri;
	std::string fDescription;
	int32 fPlaylistPosition;
	TrackRow(const std::string& uri, int32 playlistPosition = -1)
		: BRow(), fTrackUri(uri), fPlaylistPosition(playlistPosition) {}

	bool SetPlaying(bool playing) {
		bool changed = false;
		for (int32 column = 1; column <= 6; column++) {
			TrackStringField* field =
				dynamic_cast<TrackStringField*>(GetField(column));
			if (field && field->fIsPlaying != playing) {
				field->fIsPlaying = playing;
				changed = true;
			}
		}
		return changed;
	}
};

static std::string
TrackRowStringAt(TrackRow* row, int32 column)
{
	BStringField* field = dynamic_cast<BStringField*>(row->GetField(column));
	return field ? field->String() : "";
}

static int32
TrackRowNumber(TrackRow* row, int32 fallback)
{
	BIntegerField* field = dynamic_cast<BIntegerField*>(row->GetField(0));
	return field ? field->Value() : fallback;
}

static PlaylistCacheDocument::Track
CachedTrackFromRow(TrackRow* row, int32 rowIndex)
{
	PlaylistCacheDocument::Track track;
	track.number = TrackRowNumber(row, rowIndex + 1);
	track.title = TrackRowStringAt(row, 1);
	track.artist = TrackRowStringAt(row, 2);
	track.bpm = TrackRowStringAt(row, 3);
	track.key = TrackRowStringAt(row, 4);
	track.album = TrackRowStringAt(row, 5);
	track.duration = TrackRowStringAt(row, 6);
	track.uri = row->fTrackUri;
	track.artistUri = row->fArtistUri;
	track.albumUri = row->fAlbumUri;
	return track;
}

static std::string
JsonString(const nlohmann::json& object, const char* key,
	const std::string& fallback = "")
{
	if (!object.is_object())
		return fallback;
	auto value = object.find(key);
	if (value == object.end() || !value->is_string())
		return fallback;
	return value->get<std::string>();
}

static std::string
JsonDescription(const nlohmann::json& object)
{
	return JsonString(object, "html_description",
		JsonString(object, "description"));
}

static int
JsonInt(const nlohmann::json& object, const char* key, int fallback = 0)
{
	if (!object.is_object())
		return fallback;
	auto value = object.find(key);
	if (value == object.end()
			|| (!value->is_number_integer() && !value->is_number_unsigned())) {
		return fallback;
	}
	return value->get<int>();
}

static TrackRow*
CachedTrackRowFromCache(const PlaylistCacheDocument::Track& track,
	const std::string& currentPlayingTrackUri)
{
	TrackRow* row = new TrackRow(track.uri, track.number - 1);
	row->fArtistUri = track.artistUri;
	row->fAlbumUri = track.albumUri;
	row->SetField(new BIntegerField(track.number), 0);
	row->SetField(new TrackStringField(track.title.c_str()), 1);
	row->SetField(new TrackStringField(track.artist.c_str()), 2);
	row->SetField(new TrackStringField(track.bpm.c_str()), 3);
	row->SetField(new TrackStringField(track.key.c_str()), 4);
	row->SetField(new TrackStringField(track.album.c_str()), 5);
	row->SetField(new TrackStringField(track.duration.c_str()), 6);
	row->SetPlaying(!currentPlayingTrackUri.empty()
		&& row->fTrackUri == currentPlayingTrackUri);
	return row;
}

static void
AddCachedTrackRows(const std::vector<PlaylistCacheDocument::Track>& tracks,
	BColumnListView* trackList,
	const std::string& currentPlayingTrackUri)
{
	for (const PlaylistCacheDocument::Track& track : tracks)
		trackList->AddRow(CachedTrackRowFromCache(track,
			currentPlayingTrackUri));
}


static std::string sFormatDate(const std::string& isoDate);


static TrackRow*
EpisodeRowFromEpisode(const PlaylistEpisode& episode,
	const std::string& currentPlayingTrackUri)
{
	TrackRow* row = new TrackRow(episode.trackUri);
	row->fDescription = episode.description;
	row->SetField(new BIntegerField(episode.number), 0);
	row->SetField(new TrackStringField(episode.title.c_str()), 1);
	std::string displayDescription = FormatMediaDescription(
		episode.description);
	row->SetField(new TrackStringField(displayDescription.c_str()), 2);
	row->SetField(new TrackStringField(""), 3);
	row->SetField(new TrackStringField(""), 4);
	row->SetField(new TrackStringField(sFormatDate(episode.date).c_str()), 5);
	row->SetField(new TrackStringField(episode.duration.c_str()), 6);
	row->SetPlaying(!currentPlayingTrackUri.empty()
		&& row->fTrackUri == currentPlayingTrackUri);
	return row;
}


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

static bool
JsonBool(const nlohmann::json& object, const char* key, bool fallback = false)
{
	if (!object.is_object())
		return fallback;
	auto value = object.find(key);
	if (value == object.end() || !value->is_boolean())
		return fallback;
	return value->get<bool>();
}

static nlohmann::json
MutationBody(const nlohmann::json& response)
{
	if (!response.is_object() || !response.contains("body")
			|| !response["body"].is_string())
		return response;
	try {
		std::string body = response["body"].get<std::string>();
		return body.empty() ? nlohmann::json::object()
			: nlohmann::json::parse(body);
	} catch (...) {
		return nlohmann::json::object();
	}
}

static std::string
FormatTrackDuration(int ms)
{
	int seconds = ms / 1000;
	int mins = seconds / 60;
	seconds = seconds % 60;
	char buf[16];
	snprintf(buf, sizeof(buf), "%d:%02d", mins, seconds);
	return std::string(buf);
}

static void
AddTrackToMessage(BMessage* msg, const nlohmann::json& track, int32 number,
	const std::string& fallbackAlbum, const std::string& fallbackAlbumUri)
{
	msg->AddInt32("number", number);
	msg->AddString("title", JsonString(track, "name", "Unknown").c_str());

	std::string artist = "Unknown";
	std::string artistUri;
	if (track.contains("artists") && track["artists"].is_array()
			&& !track["artists"].empty()) {
		artist = JsonString(track["artists"][0], "name", "Unknown");
		artistUri = JsonString(track["artists"][0], "uri");
	} else if (track.contains("show") && track["show"].is_object()) {
		artist = JsonString(track["show"], "name", "Unknown");
		artistUri = JsonString(track["show"], "uri");
	}
	msg->AddString("artist", artist.c_str());
	msg->AddString("artistUri", artistUri.c_str());
	msg->AddString("bpm", "");
	msg->AddString("key", "");

	std::string album = fallbackAlbum;
	std::string albumUri = fallbackAlbumUri;
	if (track.contains("album") && track["album"].is_object()) {
		std::string fallback = fallbackAlbum.empty() ? "Unknown" : fallbackAlbum;
		album = JsonString(track["album"], "name", fallback);
		albumUri = JsonString(track["album"], "uri", fallbackAlbumUri);
	} else if (track.contains("show") && track["show"].is_object()) {
		album = JsonString(track["show"], "name", fallbackAlbum);
		albumUri = JsonString(track["show"], "uri", fallbackAlbumUri);
	}
	msg->AddString("album", album.c_str());
	msg->AddString("albumUri", albumUri.c_str());
	msg->AddString("duration", FormatTrackDuration(
		JsonInt(track, "duration_ms")).c_str());
	msg->AddString("trackUri", JsonString(track, "uri").c_str());
}

static void
AddEpisodeToMessage(BMessage* msg, const nlohmann::json& episode, int32 number)
{
	msg->AddInt32("number",       number);
	msg->AddString("title", JsonString(episode, "name", "Unknown").c_str());
	msg->AddString("description", JsonDescription(episode).c_str());
	msg->AddString("date", JsonString(episode, "release_date").c_str());
	msg->AddString("duration",
		FormatTrackDuration(JsonInt(episode, "duration_ms")).c_str());
	msg->AddString("trackUri", JsonString(episode, "uri").c_str());
}

static void
AddUnavailableEpisodeToMessage(BMessage* msg, int32 number)
{
	msg->AddInt32("number",       number);
	msg->AddString("title",       B_TRANSLATE("Unavailable episode"));
	msg->AddString("description", "");
	msg->AddString("date",        "");
	msg->AddString("duration",    "");
	msg->AddString("trackUri",    "");
}


static void
SendPageLoadFailure(const BMessenger& messenger, int32 searchGeneration,
	const nlohmann::json& data)
{
	BMessage failed('pLdF');
	failed.AddInt32("search_generation", searchGeneration);
	failed.AddInt32("status", SpotifyResponseStatus(data));
	failed.AddInt32("retry_after", SpotifyResponseRetryAfter(data));
	messenger.SendMessage(&failed);
}


static BMessage*
CreateTrackPageMessage(int32 offset, const nlohmann::json& data)
{
	BMessage* msg = new BMessage('pLdt');
	msg->AddBool("append", offset > 0);
	msg->AddInt32("total", JsonInt(data, "total", -1));
	int32 pageCount = (int32)data["items"].size();
	msg->AddInt32("page_count", pageCount);
	msg->AddInt32("next_offset", offset + pageCount);
	return msg;
}


static BMessage*
CreateEpisodePageMessage(int32 offset, const nlohmann::json& data)
{
	BMessage* msg = new BMessage('pEpL');
	msg->AddInt32("append", offset > 0 ? 1 : 0);
	msg->AddInt32("total", (int32)JsonInt(data, "total"));
	int32 pageCount = (int32)data["items"].size();
	msg->AddInt32("page_count", pageCount);
	msg->AddInt32("next_offset", offset + pageCount);
	return msg;
}


static bool
IsSecondaryTrackMouseClick(BMessage* message)
{
	int32 buttons = 0;
	return message && message->FindInt32("buttons", &buttons) == B_OK
		&& (buttons & (B_SECONDARY_MOUSE_BUTTON
			| B_TERTIARY_MOUSE_BUTTON)) != 0;
}


static bool
IsTrackListContentView(BView* view)
{
	if (!view || dynamic_cast<BScrollBar*>(view))
		return false;
	return !view->Name() || strcmp(view->Name(), "header") != 0;
}


static bool
FindTrackScreenPoint(BMessage* message, BView* view, BPoint& screen)
{
	if (message->FindPoint("screen_where", &screen) == B_OK)
		return true;

	BPoint where;
	if (message->FindPoint("where", &where) != B_OK)
		return false;
	screen = view->ConvertToScreen(where);
	return true;
}


class TrackListView : public BColumnListView {
public:
	TrackListView(const char* name, uint32 flags, border_style border, bool showHorizontalScrollbar)
		: BColumnListView(name, flags, border, showHorizontalScrollbar) {}

	class RightClickFilter : public BMessageFilter {
	public:
		RightClickFilter(TrackListView* owner)
			: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
			  fOwner(owner) {}

		filter_result Filter(BMessage* msg, BHandler** target) override {
			if (!fOwner || !msg || msg->what != B_MOUSE_DOWN)
				return B_DISPATCH_MESSAGE;
			if (!IsSecondaryTrackMouseClick(msg))
				return B_DISPATCH_MESSAGE;
			BView* view = dynamic_cast<BView*>(*target);
			if (!IsTrackListContentView(view))
				return B_DISPATCH_MESSAGE;
			if (!_IsInsideOwner(view))
				return B_DISPATCH_MESSAGE;
			BPoint screen;
			if (!FindTrackScreenPoint(msg, view, screen))
				return B_DISPATCH_MESSAGE;
			BMessage show('rCf!');
			show.AddPoint("screenPt", screen);
			if (fOwner->Looper())
				fOwner->Looper()->PostMessage(&show, fOwner);
			return B_SKIP_MESSAGE;
		}
	private:
		bool _IsInsideOwner(BView* view) const {
			for (BView* p = view; p; p = p->Parent()) {
				if (p == fOwner || p == fOwner->ScrollView())
					return true;
			}
			return false;
		}

		TrackListView* fOwner;
	};

	virtual void AttachedToWindow() {
		BColumnListView::AttachedToWindow();
		if (BView* outline = ScrollView())
			outline->AddFilter(new RightClickFilter(this));
		else
			AddFilter(new RightClickFilter(this));
	}

	virtual void MouseDown(BPoint point) {
		BMessage* msg = Window()->CurrentMessage();
		int32 buttons = 0;
		if (msg) msg->FindInt32("buttons", &buttons);
		BPoint livePoint;
		uint32 liveButtons = 0;
		GetMouse(&livePoint, &liveButtons, false);
		buttons |= liveButtons;
		bool contextClick = (buttons & (B_SECONDARY_MOUSE_BUTTON
			| B_TERTIARY_MOUSE_BUTTON)) != 0
			|| (buttons != 0 && (buttons & B_PRIMARY_MOUSE_BUTTON) == 0);
		if (contextClick) {
			BPoint screen = point;
			ConvertToScreen(&screen);
			((PlaylistWindow*)Window())->ShowContextMenu(this, point, screen);
			return;
		}
		BColumnListView::MouseDown(point);
	}

	virtual void MessageReceived(BMessage* msg) {
		if (msg->what == 'rCf!') {
			BPoint screen;
			if (msg->FindPoint("screenPt", &screen) == B_OK) {
				BPoint where = screen;
				if (BView* outline = ScrollView())
					outline->ConvertFromScreen(&where);
				else
					ConvertFromScreen(&where);
				((PlaylistWindow*)Window())->ShowContextMenu(this, where, screen);
			}
			return;
		}
		if (msg->WasDropped()) {
			DEBUG_PRINT("TrackListView received dropped message (what=%.4s)\n", (char*)&msg->what);
		}
		if (msg->WasDropped() && msg->what == 'drag') {
			DEBUG_PRINT("TrackListView: Posting 'drag' drop to Window\n");
			Window()->PostMessage(msg);
			return;
		}
		BColumnListView::MessageReceived(msg);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes) {
		if (numBytes == 1 && bytes[0] == B_DELETE) {
			BRow* baseRow = CurrentSelection();
			DEBUG_PRINT("TrackListView: DEL pressed. baseRow=%p\n", baseRow);
			if (baseRow) {
				TrackRow* row = (TrackRow*)baseRow;
				DEBUG_PRINT("TrackListView: deleting track %s\n", row->fTrackUri.c_str());
				if (!row->fTrackUri.empty()) {
					BMessage msg('remT');
					msg.AddString("trackUri", row->fTrackUri.c_str());
					Window()->PostMessage(&msg);
				}
			}
			return;
		}
		BColumnListView::KeyDown(bytes, numBytes);
	}

	virtual bool InitiateDrag(BPoint pt, bool wasSelected) {
		BRow* baseRow = CurrentSelection();
		if (!baseRow) {
			DEBUG_PRINT("InitiateDrag: No selection, using RowAt(pt)\n");
			baseRow = RowAt(pt);
		}

		if (baseRow) {
			TrackRow* row = (TrackRow*)baseRow;
			DEBUG_PRINT("InitiateDrag: Initiating drag for track %s\n", row->fTrackUri.c_str());
			if (!row->fTrackUri.empty()) {
				BMessage dragMsg('drag');
				dragMsg.AddString("uri", row->fTrackUri.c_str());
				dragMsg.AddString("itemType",
					SpotifyItemTypeName(SpotifyItemKindForUri(row->fTrackUri)));
				dragMsg.AddString("trackUri", row->fTrackUri.c_str());
				if (PlaylistWindow* window =
						dynamic_cast<PlaylistWindow*>(Window())) {
					dragMsg.AddString("sourcePlaylist", window->GetUri().c_str());
					for (int32 i = 0; i < CountRows(); i++) {
						if (RowAt(i) == row) {
							dragMsg.AddInt32("sourceIndex", i);
							break;
						}
					}
				}
				auto getStr = [&](int32 col) -> const char* {
					BStringField* f = dynamic_cast<BStringField*>(row->GetField(col));
					return f ? f->String() : "";
				};
				dragMsg.AddString("title",    getStr(1));
				dragMsg.AddString("artist",   getStr(2));
				dragMsg.AddString("album",    getStr(5));
				dragMsg.AddString("duration", getStr(6));

				BRect dragRect(pt.x - 100, pt.y - 10, pt.x + 100, pt.y + 10);
				DragMessage(&dragMsg, dragRect, this);
				return true;
			}
		} else {
			DEBUG_PRINT("InitiateDrag: No row found for drag\n");
		}
		return false;
	}

	virtual void MouseMoved(BPoint pt, uint32 transit, const BMessage* dragMessage) {
		if (dragMessage && dragMessage->what == 'drag') {
			bool canDrop = false;
			PlaylistWindow* win = (PlaylistWindow*)Window();
			if (win && SpotifyItemKindForUri(win->GetUri())
					== kSpotifyItemPlaylist) {
				std::string id = SpotifyItemIdForUri(win->GetUri());
				App* app = (App*)be_app;
				SpotifyApi* api = app->GetApi();
				if (api) {
					auto playlists = api->Playlists().GetCachedPlaylists();
					for (const auto& pl : playlists) {
						if (pl.first == id) {
							canDrop = true;
							break;
						}
					}
				}
			}




		}
		BColumnListView::MouseMoved(pt, transit, dragMessage);
		if (Window())
			Window()->PostMessage(kMsgCheckLazyLoad);
	}

	virtual void SelectionChanged() {
		BColumnListView::SelectionChanged();
		if (Window())
			Window()->PostMessage(kMsgCheckLazyLoad);
		TrackRow* row = (TrackRow*)CurrentSelection();
		if (!row || !Window()) return;
		if (!row->fDescription.empty()) {
			BMessage msg('epSl');
			msg.AddString("description", row->fDescription.c_str());
			Window()->PostMessage(&msg);
		}
	}

	virtual void ItemInvoked() {
		TrackRow* row = (TrackRow*)CurrentSelection();
		if (!row || !Window()) return;

		BPoint where;
		uint32 buttons;
		GetMouse(&where, &buttons, false);
		int32 col = _ColumnAt(where.x);

		if (col == 1 && !row->fTrackUri.empty()) {
			Window()->PostMessage(new BMessage(MSG_TRACK_INVOKED));
		} else if (col == 2 && !row->fArtistUri.empty()) {
			std::string id = SpotifyItemKindForUri(row->fArtistUri)
				== kSpotifyItemArtist
				? SpotifyItemIdForUri(row->fArtistUri) : row->fArtistUri;
			if (id.empty())
				return;
			BMessage msg(MSG_SHOW_ARTIST);
			msg.AddString("id", id.c_str());
			be_app->PostMessage(&msg);
		} else if (col == 5 && !row->fAlbumUri.empty()) {
			std::string id = SpotifyItemKindForUri(row->fAlbumUri)
				== kSpotifyItemAlbum
				? SpotifyItemIdForUri(row->fAlbumUri) : row->fAlbumUri;
			if (id.empty())
				return;
			BMessage msg(MSG_SHOW_ALBUM);
			msg.AddString("id", id.c_str());
			be_app->PostMessage(&msg);
		}
	}

private:
	int32 _ColumnAt(float x) const {
		float left = 0;
		for (int32 i = 0; i < CountColumns(); i++) {
			BColumn* col = ColumnAt(i);
			if (!col) break;
			left += col->Width();
			if (x < left) return i;
		}
		return CountColumns() - 1;
	}
};

static BScrollBar*
TrackVerticalScrollBar(TrackListView* list)
{
	if (!list)
		return nullptr;
	if (BView* scrollTarget = list->ScrollView()) {
		if (BScrollBar* scrollBar = scrollTarget->ScrollBar(B_VERTICAL))
			return scrollBar;
	}
	return list->ScrollBar(B_VERTICAL);
}


static const char*
PlaylistWindowTitlePrefix(const std::string& uri)
{
	switch (SpotifyItemKindForUri(uri)) {
		case kSpotifyItemPlaylist: return "Playlist: ";
		case kSpotifyItemAlbum: return "Album: ";
		case kSpotifyItemShow: return "Podcast: ";
		case kSpotifyItemArtist: return "Artist: ";
		default: return "";
	}
}


static const char*
PlaylistWindowContentType(const std::string& uri)
{
	if (uri == "spotify:collection")
		return B_TRANSLATE("Liked Songs");
	return SpotifyItemKindForUri(uri) == kSpotifyItemAlbum
		? "Album" : SpotifyItemKindForUri(uri) == kSpotifyItemShow
		? "Podcast" : "Playlist";
}


static const char*
PlaylistWindowCountLabel(const std::string& uri)
{
	return SpotifyItemKindForUri(uri) == kSpotifyItemShow
		? "Episodes" : "Songs";
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

	SetTitle((std::string(PlaylistWindowTitlePrefix(fUri))
		+ playlistName).c_str());

	_InitMenu();
	_InitLayout(playlistName);
	if (fUri != "spotify:collection" && !fCoverUrl.empty() && fCoverView)
		((ArtworkView*)fCoverView)->LoadUrl(fCoverUrl);
	_LoadData();
	BMessage lazyMessage(kMsgCheckLazyLoad);
	fLazyLoadRunner = new BMessageRunner(BMessenger(this), &lazyMessage,
		500000LL);

}


bool
PlaylistWindow::_HandleTrackActionMessage(BMessage* message)
{
	switch (message->what) {
		case 'tply':
			_PlayTrackFromMessage(message);
			return true;
		case 'remL':
			_RemoveTrackFromLibrary(message);
			return true;
		case 'iCmR':
			_ShowPlayableContextMenu(message);
			return true;
		case MSG_PLAY_PAUSE:
			be_app->PostMessage(message);
			return true;
		case MSG_TRACK_INVOKED:
			_PlayCurrentTrack();
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
		case 'rTrR':
			_ApplyTrackRemovalResult(message);
			return true;
		case 'pMvR':
			_ApplyTrackReorderResult(message);
			return true;
		case 'pClR':
			_ApplyClearPlaylistResult(message);
			return true;
		case 'pAdR':
			_ApplyPlaylistAddResult(message);
			return true;
		case 'pRmM':
			_ApplyPlaylistRemoveMarked(message);
			return true;
		case 'drpT':
			_HandleTrackDrop(message);
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
		case 'pLdF':
			_ApplyPageLoadFailure(message);
			return true;
		case 'pLdt':
			_ApplyTrackPage(message);
			return true;
		case 'uTtl':
			_ApplyTitleUpdate(message);
			return true;
		case 'pStU':
			_ApplyPlayingTrackUpdate(message);
			return true;
		case 'uCov':
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
		case 'plMt':
			_ApplyPlaylistMetadata(message);
			return true;
		case 'plUs':
			_ApplyPlaylistUserState(message);
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
		case 'pCvR':
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
		case 'pEpL':
			_ApplyEpisodePage(message);
			return true;
		case 'pEpR':
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
		case kMsgApplyEpisodeSearch:
			_ApplyEpisodeSearch(message);
			return true;
		case kMsgRetryEpisodeSearch:
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
PlaylistWindow::_ApplyPlaylistRemoveMarked(BMessage* message)
{
	if (!message->GetBool("ok", false))
		return;

	fPlaylistSnapshotId = message->GetString("snapshot_id", "");
	int32 total = message->GetInt32("total", -1);
	if (total >= 0)
		fPageTotal = total;
	fPageHasMore = fPageOffset < fPageTotal;
	_UpdatePlaylistTrackInfo();
}


void
PlaylistWindow::_ReloadDataIfIdle()
{
	if (!fTrackReorderPending && !fPlaylistClearPending)
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
		SetTitle((std::string(PlaylistWindowTitlePrefix(fUri)) + title).c_str());
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
	const char* trackUri;
	if (message->FindString("trackUri", &trackUri) == B_OK)
		SetPlayingTrack(trackUri);
}


void
PlaylistWindow::_ApplyCoverUpdate(BMessage* message)
{
	const char* url;
	if (message->FindString("url", &url) != B_OK)
		return;
	fCoverUrl = url;
	if (fCoverView)
		((ArtworkView*)fCoverView)->LoadUrl(fCoverUrl);
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
	if (fPlaylistDeleteItem)
		fPlaylistDeleteItem->SetEnabled(true);
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
		BMessage msg('uTtl');
		msg.AddString("title", sname.c_str());
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
	const char* trackUri = message->GetString("trackUri", "");
	if (!*trackUri)
		return;
	BMessage play('play');
	play.AddString("uri", trackUri);
	play.AddString("context_uri", fUri.c_str());
	bool podcast = SpotifyItemKindForUri(fUri) == kSpotifyItemShow;
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		TrackRow* row = (TrackRow*)fTrackList->RowAt(i);
		if (!row || row->fTrackUri != trackUri)
			continue;
		BStringField* title = dynamic_cast<BStringField*>(row->GetField(1));
		BStringField* artist = dynamic_cast<BStringField*>(row->GetField(2));
		if (title)
			play.AddString("title", title->String());
		if (!podcast && artist)
			play.AddString("artist", artist->String());
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

	BMessage play('play');
	play.AddString("uri", row->fTrackUri.c_str());
	play.AddString("context_uri", fUri.c_str());
	bool podcast = SpotifyItemKindForUri(fUri) == kSpotifyItemShow;
	BStringField* title = dynamic_cast<BStringField*>(row->GetField(1));
	BStringField* artist = dynamic_cast<BStringField*>(row->GetField(2));
	if (title)
		play.AddString("title", title->String());
	if (!podcast && artist)
		play.AddString("artist", artist->String());
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
	if (message->FindString("trackUri", &trackUri) != B_OK) {
		DEBUG_PRINT("PlaylistWindow: 'remT' received but no trackUri found\n");
		return;
	}
	DEBUG_PRINT("PlaylistWindow: received 'remT' for %s\n", trackUri);
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemPlaylist || !fPlaylistOwned) {
		DEBUG_PRINT("PlaylistWindow: 'remT' failed because fUri is not a playlist: %s\n",
			fUri.c_str());
		return;
	}
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || fTrackRemovalPending || fTrackReorderPending
			|| fPlaylistClearPending) {
		return;
	}

	std::vector<std::pair<std::string, int>> items;
	if (!_CollectPendingTrackRemovals(items))
		return;

	std::vector<std::string> knownPlaylistUris = _KnownPlaylistUrisForRemoval();
	_RemovePendingTrackRows();
	fTrackRemovalPending = true;
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();

	BMessenger self(this);
	auto completion = [self](bool ok, const nlohmann::json& data) {
		BMessage result('rTrR');
		result.AddBool("ok", ok);
		result.AddInt32("status", SpotifyResponseStatus(data));
		result.AddBool("partial_update", JsonBool(data, "partial_update"));
		self.SendMessage(&result);
	};
	std::string playlistId = SpotifyItemIdForUri(fUri);
	if (!knownPlaylistUris.empty()) {
		api->Playlists().RemovePlaylistItemsFromKnownSnapshot(
			playlistId, items, fPlaylistSnapshotId, knownPlaylistUris,
			completion);
	} else {
		api->Playlists().RemovePlaylistItemsAtPositions(
			playlistId, items, fPlaylistSnapshotId, completion);
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
		fPendingTrackRemovals.push_back({row, listIndex,
			row->fPlaylistPosition, true});
	}
	return !items.empty() && !fPendingTrackRemovals.empty();
}


std::vector<std::string>
PlaylistWindow::_KnownPlaylistUrisForRemoval() const
{
	std::vector<std::string> uris;
	if (!PlaylistHasCompleteSnapshot(fPlaylistSnapshotId, fPageTotal,
			fTrackList->CountRows(), fPageOffset)) {
		return uris;
	}
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
	const char* trackUri = message->GetString("trackUri", "");
	if (!trackUri || !trackUri[0])
		trackUri = message->GetString("uri", "");
	const char* sourcePlaylist = message->GetString("sourcePlaylist", "");
	int32 sourceIndex = message->GetInt32("sourceIndex", -1);
	std::string type = message->GetString("itemType", "");
	bool mutationPending = fTrackRemovalPending || fTrackReorderPending
		|| fPlaylistClearPending;
	PlaylistDropAction action = ResolvePlaylistDropAction(fUri,
		sourcePlaylist, sourceIndex, type, trackUri, fPlaylistOwned,
		mutationPending);
	if (action == kPlaylistDropReorder) {
		_HandleTrackReorderDrop(message, sourceIndex);
		return;
	}
	if (action == kPlaylistDropAddPlayableItem) {
		_AddDroppedPlayableItem(message, trackUri);
		return;
	}
	DEBUG_PRINT("PlaylistWindow: Drop received without playable track uri\n");
}


void
PlaylistWindow::_HandleTrackReorderDrop(BMessage* message, int32 sourceIndex)
{
	BPoint point = message->DropPoint();
	fTrackList->ConvertFromScreen(&point);
	BRow* targetRow = fTrackList->RowAt(point);
	int32 targetIndex = fTrackList->CountRows();
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		if (fTrackList->RowAt(i) == targetRow) {
			targetIndex = i;
			break;
		}
	}
	if (targetIndex == sourceIndex)
		return;
	int32 insertBefore = ResolvePlaylistDropInsertBefore(
		sourceIndex, targetIndex, fTrackList->CountRows());
	_BeginTrackReorder(sourceIndex, 1, insertBefore);
}


void
PlaylistWindow::_AddDroppedPlayableItem(BMessage* message, const char* trackUri)
{
	DEBUG_PRINT("PlaylistWindow: Drop received for track %s on %s\n",
		trackUri, fUri.c_str());
	const char* title = message->GetString("title", "");
	const char* artist = message->GetString("artist", "");
	const char* album = message->GetString("album", "");
	const char* duration = message->GetString("duration", "");
	bool fullyLoaded = fPageOffset >= fPageTotal;
	int32 playlistPosition = std::max(fPageTotal,
		(int32)fTrackList->CountRows());
	int32 nextNum = playlistPosition + 1;
	TrackRow* row = new TrackRow(trackUri, playlistPosition);
	row->SetField(new BIntegerField(nextNum), 0);
	row->SetField(new TrackStringField(title), 1);
	row->SetField(new TrackStringField(artist), 2);
	row->SetField(new TrackStringField(""), 3);
	row->SetField(new TrackStringField(""), 4);
	row->SetField(new TrackStringField(album), 5);
	row->SetField(new TrackStringField(duration), 6);
	row->SetPlaying(!fCurrentPlayingTrackUri.empty()
		&& row->fTrackUri == fCurrentPlayingTrackUri);
	fTrackList->AddRow(row);
	fPageTotal = playlistPosition + 1;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;

	DEBUG_PRINT("PlaylistWindow: Adding track to playlist\n");
	BMessenger self(this);
	api->Playlists().AddTrackToPlaylist(_PlaylistId(), trackUri,
		[self, row, fullyLoaded](bool ok, const nlohmann::json& data) {
		BMessage result('pAdR');
		result.AddBool("ok", ok);
		result.AddBool("fully_loaded", fullyLoaded);
		result.AddPointer("row", row);
		nlohmann::json body = MutationBody(data);
		result.AddString("snapshot_id",
			JsonString(body, "snapshot_id").c_str());
		self.SendMessage(&result);
	});
	_DeleteCache();
}


void
PlaylistWindow::_ApplyPageLoadFailure(BMessage* message)
{
	fPageLoading = false;
	int32 searchGeneration = message->GetInt32("search_generation", -1);
	if (fEpisodeSearchPaging && searchGeneration >= 0
			&& searchGeneration == fEpisodeSearchGeneration) {
		int32 status = message->GetInt32("status", -1);
		int32 retryAfter = message->GetInt32("retry_after", -1);
		bool temporary = status < 0 || status == 408 || status == 425
			|| status == 429 || status >= 500;
		if (temporary && fEpisodeSearchRetryCount < 3) {
			fEpisodeSearchRetryCount++;
			fEpisodeSearchWaitingRetry = true;
			bigtime_t delay = status == 429 && retryAfter > 0
				? (bigtime_t)retryAfter * 1000000LL : 2000000LL;
			delete fEpisodeSearchRetryRunner;
			BMessage retry(kMsgRetryEpisodeSearch);
			retry.AddInt32("search_generation", fEpisodeSearchGeneration);
			fEpisodeSearchRetryRunner = new BMessageRunner(BMessenger(this),
				&retry, delay, 1);
		} else {
			fEpisodeSearchPaging = false;
			fEpisodeSearchWaitingRetry = false;
			fEpisodeSearchFailed = true;
		}
		_UpdateEpisodeInfo();
	} else if (fEpisodeSearchPaging && !fEpisodeSearchFilter.empty()) {
		_CheckLazyLoad();
	}
}


void
PlaylistWindow::_ApplyPlaylistMetadata(BMessage* message)
{
	const char* title = message->GetString("title", "");
	if (title && title[0]) {
		SetTitle((std::string("Playlist: ") + title).c_str());
		fPlaylistName->SetText(title);
	}

	const char* coverUrl = message->GetString("cover_url", "");
	if (coverUrl && coverUrl[0]) {
		BMessage cover('uCov');
		cover.AddString("url", coverUrl);
		PostMessage(&cover);
	}

	std::string snapshot = message->GetString("snapshot_id", "");
	fPlaylistDescription = message->GetString("description", "");
	fPlaylistOwnerId = message->GetString("owner_id", "");
	fPlaylistPublic = message->GetBool("public", false);
	fPlaylistOwned = !fCurrentUserId.empty()
		&& (fCurrentUserId == fPlaylistOwnerId
			|| fCurrentUserLegacyId == fPlaylistOwnerId);
	_UpdatePlaylistMenuState();
	int32 total = message->GetInt32("total", -1);
	fPlaylistSnapshotId = snapshot;

	if (ShouldReloadPlaylistRowsForSnapshot(fCachedPlaylistSnapshotId,
			snapshot)) {
		_DeleteCache();
		fCachedPlaylistSnapshotId.clear();
		fPageLoading = false;
		fPageOffset = 0;
		fPageTotal = total >= 0 ? total : 0;
		fPageHasMore = total != 0;
		_LoadNextPage();
		return;
	}

	PlaylistMetadataPageState pageState = ResolvePlaylistMetadataPageState(
		total, fPageTotal, fTrackList->CountRows(), fPageOffset);
	fPageTotal = pageState.total;
	fPageHasMore = pageState.hasMore;
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
	fPlaylistDescription = message->GetString("description", "");
	fPlaylistPublic = message->GetBool("public", false);
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
	fPageLoading = false;
	bool append = message->GetBool("append", false);
	BScrollBar* scrollBar = TrackVerticalScrollBar(fTrackList);
	float scrollValue = scrollBar ? scrollBar->Value() : 0.0f;
	if (!append)
		fTrackList->Clear();

	_AddTrackPageRows(message);

	int32 total = message->GetInt32("total", -1);
	if (total >= 0)
		fPageTotal = total;
	fPageOffset = message->GetInt32("next_offset",
		(int32)fTrackList->CountRows());
	int32 pageCount = message->GetInt32("page_count", 0);
	fPageHasMore = pageCount > 0
		&& (fPageTotal <= 0 || fPageOffset < fPageTotal);
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
		row->SetField(new BIntegerField(number), 0);
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
	BMessage play('play');
	play.AddString("uri", fUri.c_str());
	be_app->PostMessage(&play);
}


void
PlaylistWindow::_ApplyTrackRemovalResult(BMessage* message)
{
	bool ok = message->GetBool("ok", false);
	bool needsReload = !ok && (message->GetInt32("status", -1) == 409
		|| message->GetBool("partial_update", false));
	_FinishTrackRemoval(ok);
	if (ok) {
		_RefreshPlaylistSnapshot();
	} else if (needsReload) {
		PostMessage('pSnC');
	} else {
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
	bool ok = message->GetBool("ok", false);
	bool conflict = !ok && message->GetInt32("status", -1) == 409;
	_FinishTrackReorder(ok, message->GetString("snapshot_id", ""));
	if (ok) {
		if (fPlaylistSnapshotId.empty())
			_RefreshPlaylistSnapshot();
	} else if (conflict) {
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
PlaylistWindow::_ApplyClearPlaylistResult(BMessage* message)
{
	bool ok = message->GetBool("ok", false);
	_FinishClearPlaylist(ok, message->GetString("snapshot_id", ""));
	if (!ok) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not clear the playlist."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
}


void
PlaylistWindow::_ApplyPlaylistAddResult(BMessage* message)
{
	BRow* pendingRow = nullptr;
	if (message->FindPointer("row", (void**)&pendingRow) != B_OK
			|| !pendingRow)
		return;
	bool fullyLoaded = message->GetBool("fully_loaded", false);
	if (message->GetBool("ok", false)) {
		fPlaylistSnapshotId = message->GetString("snapshot_id", "");
		fCachedPlaylistSnapshotId = fPlaylistSnapshotId;
		if (fullyLoaded)
			fPageOffset++;
		_DeleteCache();
		if (!fPlaylistSnapshotId.empty())
			_SaveCache();
		else
			_RefreshPlaylistSnapshot();
	} else {
		bool stillVisible = fTrackList->IndexOf(pendingRow) >= 0;
		if (stillVisible) {
			fTrackList->RemoveRow(pendingRow);
			delete pendingRow;
			fPageTotal = std::max((int32)0, fPageTotal - 1);
		}
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not add the song to the playlist."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
	fPageHasMore = fPageOffset < fPageTotal;
	_UpdatePlaylistTrackInfo();
}


void
PlaylistWindow::_ApplyPlaylistUserState(BMessage* message)
{
	fCurrentUserId = message->GetString("user_id", "");
	fCurrentUserLegacyId = message->GetString("legacy_user_id", "");
	fPlaylistOwned = !fCurrentUserId.empty()
		&& (fCurrentUserId == fPlaylistOwnerId
			|| fCurrentUserLegacyId == fPlaylistOwnerId);
	_UpdatePlaylistMenuState();
}


void
PlaylistWindow::_UpdatePlaylistDetails(BMessage* message)
{
	if (!fPlaylistOwned)
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
	if (message->GetBool("ok", false)) {
		App* app = dynamic_cast<App*>(be_app);
		SpotifyApi* api = app ? app->GetApi() : nullptr;
		if (api) {
			BMessenger self(this);
			api->Playlists().GetPlaylistImages(_PlaylistId(),
				[self](bool ok, const nlohmann::json& images) {
				if (!ok || !images.is_array() || images.empty())
					return;
				BMessage cover('uCov');
				cover.AddString("url", JsonString(images[0], "url").c_str());
				self.SendMessage(&cover);
			});
		}
	} else {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Spotify could not upload the playlist cover."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
	}
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
	fPageLoading = false;
	fEpisodeSearchRetryCount = 0;
	fEpisodeSearchWaitingRetry = false;
	fEpisodeSearchFailed = false;
	int32 append = message->GetInt32("append", 0);
	BScrollBar* scrollBar = TrackVerticalScrollBar(fTrackList);
	float scrollValue = scrollBar ? scrollBar->Value() : 0.0f;
	fEpisodeTotal = message->GetInt32("total", 0);
	fEpisodeOffset = message->GetInt32("next_offset", 0);
	fPageTotal = fEpisodeTotal;
	fPageOffset = fEpisodeOffset;
	int32 pageCount = message->GetInt32("page_count", 0);
	fPageHasMore = pageCount > 0
		&& (fEpisodeTotal <= 0 || fEpisodeOffset < fEpisodeTotal);

	if (!append)
		fEpisodes.clear();
	size_t firstNewEpisode = _AppendEpisodePageItems(message);
	_RenumberEpisodes();

	if (append)
		_AppendEpisodeRows(firstNewEpisode, fEpisodeSearchFilter);
	else
		_RebuildEpisodeList(fEpisodeSearchFilter);
	if (append && scrollBar)
		scrollBar->SetValue(scrollValue);
	if (!fPageHasMore)
		fEpisodeSearchPaging = false;

	_UpdateEpisodeInfo();

	_SaveCache();
	_CheckLazyLoad();
}


static bool
MessageEpisodeAt(BMessage* message, int32 index, PlaylistEpisode& episode)
{
	int32 number;
	if (message->FindInt32("number", index, &number) != B_OK)
		return false;

	const char* title = message->FindString("title", index);
	const char* description = message->FindString("description", index);
	const char* date = message->FindString("date", index);
	const char* duration = message->FindString("duration", index);
	const char* trackUri = message->FindString("trackUri", index);
	episode = MakePlaylistEpisode(number, title ? title : "",
		description ? description : "", date ? date : "",
		duration ? duration : "", trackUri ? trackUri : "");
	return true;
}


size_t
PlaylistWindow::_AppendEpisodePageItems(BMessage* message)
{
	size_t firstNewEpisode = fEpisodes.size();
	PlaylistEpisode episode;
	for (int32 i = 0; MessageEpisodeAt(message, i, episode); i++) {
		if (_HasEpisode(episode.trackUri, episode.title, episode.date,
				episode.duration)) {
			continue;
		}
		fEpisodes.push_back(episode);
	}
	return firstNewEpisode;
}


void
PlaylistWindow::_ApplyPodcastHeadPage(BMessage* message)
{
	bool ok = message->GetBool("ok", false);
	if (!ok) {
		fPodcastHeadRefreshing = false;
		fPendingPodcastHeadEpisodes.clear();
		return;
	}

	fEpisodeTotal = message->GetInt32("total", fEpisodeTotal);
	int32 offset = message->GetInt32("offset", 0);
	int32 pageCount = message->GetInt32("page_count", 0);
	int32 nextOffset = message->GetInt32("next_offset", offset + pageCount);

	bool reachedKnownEpisode = false;
	PlaylistEpisode episode;
	for (int32 i = 0; MessageEpisodeAt(message, i, episode); i++) {
		if (_HasEpisode(episode.trackUri, episode.title, episode.date,
				episode.duration)) {
			reachedKnownEpisode = true;
			break;
		}
		fPendingPodcastHeadEpisodes.push_back(episode);
	}

	if (!reachedKnownEpisode && pageCount > 0
			&& (fEpisodeTotal <= 0 || nextOffset < fEpisodeTotal)) {
		_RefreshPodcastHead(nextOffset);
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
	fEpisodeSearchGeneration++;
	fEpisodeSearchPaging = false;
	fEpisodeSearchWaitingRetry = false;
	fEpisodeSearchFailed = false;
	fEpisodeSearchRetryCount = 0;
	_UpdateEpisodeInfo();
	delete fEpisodeSearchRetryRunner;
	fEpisodeSearchRetryRunner = nullptr;
	delete fEpisodeSearchRunner;
	BMessage apply(kMsgApplyEpisodeSearch);
	apply.AddInt32("search_generation", fEpisodeSearchGeneration);
	fEpisodeSearchRunner = new BMessageRunner(BMessenger(this), &apply,
		200000LL, 1);
}


void
PlaylistWindow::_ApplyEpisodeSearch(BMessage* message)
{
	int32 searchGeneration = message->GetInt32("search_generation", -1);
	if (searchGeneration != fEpisodeSearchGeneration)
		return;
	delete fEpisodeSearchRunner;
	fEpisodeSearchRunner = nullptr;
	if (fSearchBox) {
		fEpisodeSearchFilter = fSearchBox->Text();
		fEpisodeSearchPaging = !fEpisodeSearchFilter.empty() && fPageHasMore;
		fEpisodeSearchFailed = false;
		_RebuildEpisodeList(fEpisodeSearchFilter);
		_UpdateEpisodeInfo();
		_CheckLazyLoad();
	}
}


void
PlaylistWindow::_RetryEpisodeSearch(BMessage* message)
{
	int32 searchGeneration = message->GetInt32("search_generation", -1);
	if (searchGeneration != fEpisodeSearchGeneration)
		return;
	delete fEpisodeSearchRetryRunner;
	fEpisodeSearchRetryRunner = nullptr;
	if (fEpisodeSearchPaging && !fEpisodeSearchFilter.empty()) {
		fEpisodeSearchWaitingRetry = false;
		_UpdateEpisodeInfo();
		_LoadNextPage();
	}
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
		if (message->what == 'drag') {
			DEBUG_PRINT("DropFilter: caught 'drag' message! WasDropped=%d, target=%p\n", message->WasDropped(), *target);
			if (message->WasDropped()) {
				DEBUG_PRINT("DropFilter: forwarding dropped message to window\n");
				BMessage dropMsg(*message);
				dropMsg.what = 'drpT';
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
	SpotifyItemKind kind = SpotifyItemKindForUri(fUri);
	bool isPodcast = kind == kSpotifyItemShow;
	bool isAlbum = kind == kSpotifyItemAlbum;
	bool isLikedSongs = (fUri == "spotify:collection");
	float artworkSize = MediaHeaderStyle::kArtworkSize;

	ArtworkView* coverView = new ArtworkView("CoverView");
	if (isLikedSongs)
		coverView->AdoptBitmap(LoadLikedSongsArtwork(artworkSize));
	else
		coverView->ShowLoading();
	coverView->SetExplicitMinSize(BSize(artworkSize, artworkSize));
	coverView->SetExplicitMaxSize(BSize(artworkSize, artworkSize));
	coverView->SetExplicitPreferredSize(BSize(artworkSize, artworkSize));
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

	fTrackList = new TrackListView("TrackList", 0, B_PLAIN_BORDER, true);
	fTrackList->SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	fTrackList->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	fTrackList->AddColumn(new BIntegerColumn("#", 40, 30, 60, B_ALIGN_RIGHT), 0);
	fTrackList->AddColumn(new TrackStringColumn(B_TRANSLATE("Title"), 200, 80, 400, B_TRUNCATE_END), 1);
	if (isPodcast) {
		fTrackList->AddColumn(new TrackStringColumn(B_TRANSLATE("Description"), 280, 80, 600, B_TRUNCATE_END), 2);
	} else {
		fTrackList->AddColumn(new TrackStringColumn(B_TRANSLATE("Artist"), 140, 60, 300, B_TRUNCATE_END), 2);
	}
	fBpmColumn = new TrackStringColumn("BPM", 50, 40, 80, B_TRUNCATE_END, B_ALIGN_RIGHT);
	fKeyColumn = new TrackStringColumn("Key", 50, 40, 80, B_TRUNCATE_END, B_ALIGN_CENTER);
	fTrackList->AddColumn(fBpmColumn, 3);
	fTrackList->AddColumn(fKeyColumn, 4);
	BStringColumn* dateAlbumColumn;
	if (isPodcast) {
		dateAlbumColumn = new TrackStringColumn(B_TRANSLATE("Date"), 90, 60, 120, B_TRUNCATE_END);
	} else {
		dateAlbumColumn = new TrackStringColumn(B_TRANSLATE("Album"), 160, 60, 350, B_TRUNCATE_END);
	}
	fTrackList->AddColumn(dateAlbumColumn, 5);
	fTrackList->AddColumn(new TrackStringColumn(B_TRANSLATE("Duration"), 86, 70, 120, B_TRUNCATE_END, B_ALIGN_RIGHT), 6);
	fBpmColumn->SetVisible(false);
	fKeyColumn->SetVisible(false);
	if (isAlbum)
		dateAlbumColumn->SetVisible(false);

	if (isPodcast) {
		const float podcastInfoWidth = 230.0f;
		const float podcastSearchInfoHeight = be_plain_font->Size() * 1.35f;
		fPlaylistName->SetExplicitMinSize(BSize(
			podcastInfoWidth, B_SIZE_UNSET));
		fPlaylistName->SetExplicitPreferredSize(BSize(
			podcastInfoWidth, B_SIZE_UNSET));
		fPlaylistName->SetExplicitMaxSize(BSize(podcastInfoWidth, 42));
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
			MediaHeaderStyle::kActionButtonMinWidth, B_SIZE_UNSET));
		fSubscribeButton->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
			B_ALIGN_VERTICAL_CENTER));
		fSubscribeButton->SetEnabled(false);

		fDescriptionView = new MediaDescriptionView("DescriptionView");
		fDescriptionScroll = new BScrollView("DescScroll", fDescriptionView,
			0, false, true, B_FANCY_BORDER);
		fDescriptionScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 72));
		fDescriptionScroll->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 94));
		fDescriptionScroll->SetExplicitMaxSize(
			BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	}
	if (isAlbum) {
		fAlbumSaveButton = new BButton("saveAlbum",
			B_TRANSLATE("Add to Saved Albums"),
			new BMessage(kMsgToggleAlbumSaved));
		fAlbumSaveButton->SetExplicitMinSize(BSize(
			MediaHeaderStyle::kActionButtonMinWidth, B_SIZE_UNSET));
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
								kSearchIconResource, 16.0f), 0.0f)
							.Add(fSearchBox, 1.0f)
						.End()
					.End()
				.End()
				.Add(fTrackList, 1)
			.End();
	} else if (isAlbum) {
		fPlaylistName->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));
		fPlaylistName->SetExplicitAlignment(BAlignment(
			B_ALIGN_USE_FULL_WIDTH, B_ALIGN_TOP));
		fPlaylistInfo->SetExplicitAlignment(BAlignment(
			B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER));

		BView* albumInfo = new BView("albumHeaderInfo", 0);
		albumInfo->SetExplicitMinSize(BSize(0, artworkSize));
		albumInfo->SetExplicitPreferredSize(BSize(B_SIZE_UNSET,
			artworkSize));
		albumInfo->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
			artworkSize));
		albumInfo->SetExplicitAlignment(BAlignment(
			B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT));
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
	} else {
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

	if (isPodcast)
		SetSizeLimits(560, 100000, 300, 100000);
	else if (isAlbum)
		SetSizeLimits(420, 100000, 260, 100000);
	else
		SetSizeLimits(420, 100000, 260, 100000);
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
	((ArtworkView*)fCoverView)->ReloadUrl();
}

void
PlaylistWindow::_ShowPlaylistContextMenu(BPoint screenWhere)
{
	if (_PlaylistId().empty())
		return;

	BPopUpMenu* menu = new BPopUpMenu("playlist", false, false);
	BMenuItem* edit = new BMenuItem(B_TRANSLATE("Edit Details" B_UTF8_ELLIPSIS),
		new BMessage(kMsgEditPlaylist));
	edit->SetEnabled(fPlaylistOwned);
	menu->AddItem(edit);
	BMenuItem* cover = new BMenuItem(B_TRANSLATE("Change Cover" B_UTF8_ELLIPSIS),
		new BMessage(kMsgChoosePlaylistCover));
	cover->SetEnabled(fPlaylistOwned);
	menu->AddItem(cover);
	menu->AddSeparatorItem();
	BMenuItem* clear = new BMenuItem(B_TRANSLATE("Clear Playlist"),
		new BMessage(kMsgClearPlaylist));
	clear->SetEnabled(fPlaylistOwned && fTrackList->CountRows() > 0
		&& !fTrackRemovalPending && !fTrackReorderPending
		&& !fPlaylistClearPending);
	menu->AddItem(clear);
	BMenuItem* remove = new BMenuItem(fPlaylistOwned
		? B_TRANSLATE("Delete Playlist") : B_TRANSLATE("Unfollow Playlist"),
		new BMessage(kMsgDeletePlaylist));
	menu->AddItem(remove);

	BMenuItem* selected = menu->Go(screenWhere, false, true);
	if (selected && selected->Message())
		PostMessage(selected->Message());
	delete menu;
}

void
PlaylistWindow::_UpdatePlaylistMenuState()
{
	if (fPlaylistEditItem) fPlaylistEditItem->SetEnabled(fPlaylistOwned);
	if (fPlaylistCoverItem) fPlaylistCoverItem->SetEnabled(fPlaylistOwned);
	if (fPlaylistClearItem)
		fPlaylistClearItem->SetEnabled(fPlaylistOwned && fTrackList
			&& fTrackList->CountRows() > 0 && !fTrackRemovalPending
			&& !fTrackReorderPending && !fPlaylistClearPending);
	if (fPlaylistDeleteItem)
		fPlaylistDeleteItem->SetLabel(fPlaylistOwned
			? B_TRANSLATE("Delete Playlist") : B_TRANSLATE("Unfollow Playlist"));
}

void
PlaylistWindow::_ShowPlaylistDetailsDialog()
{
	if (!fPlaylistOwned || _PlaylistId().empty()) return;
	PlaylistDetailsDialog* dialog = new PlaylistDetailsDialog(
		fPlaylistName ? fPlaylistName->Text() : "", fPlaylistDescription,
		fPlaylistPublic, BMessenger(this));
	dialog->MoveTo(Frame().left + 40, Frame().top + 40);
	dialog->Show();
}

void
PlaylistWindow::_ChoosePlaylistCover()
{
	if (!fPlaylistOwned || _PlaylistId().empty()) return;
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
	if (!fPlaylistOwned) return;
	BFile file(&ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK) return;
	off_t size = 0;
	if (file.GetSize(&size) != B_OK || size < 4 || size > 256 * 1024) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Choose a JPEG whose encoded payload stays below 256 KB."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return;
	}
	std::vector<uint8> bytes((size_t)size);
	if (file.Read(bytes.data(), size) != size || bytes[0] != 0xff
			|| bytes[1] != 0xd8) {
		BAlert* alert = new BAlert("", B_TRANSLATE("The selected file is not a JPEG."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return;
	}
	std::string encoded = Base64Encode(bytes);
	if (encoded.size() > 256 * 1024) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"The Base64-encoded JPEG exceeds Spotify's 256 KB limit."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return;
	}
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api) return;
	BMessenger self(this);
	api->Playlists().UploadPlaylistImage(_PlaylistId(), encoded,
		[self](bool ok, const nlohmann::json&) {
			BMessage result('pCvR');
			result.AddBool("ok", ok);
			self.SendMessage(&result);
		});
}

void
PlaylistWindow::_ClearPlaylist()
{
	if (!fPlaylistOwned || fTrackRemovalPending || fTrackReorderPending
			|| fPlaylistClearPending || _PlaylistId().empty()
			|| !fTrackList || fTrackList->CountRows() == 0) return;
	BAlert* alert = new BAlert("", B_TRANSLATE(
		"Remove every item from this playlist?"), B_TRANSLATE("Cancel"),
		B_TRANSLATE("Clear Playlist"), nullptr, B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	if (alert->Go() != 1) return;
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api) return;

	fPendingPlaylistClear = PendingPlaylistClear();
	fPendingPlaylistClear.pageOffset = fPageOffset;
	fPendingPlaylistClear.pageTotal = fPageTotal;
	fPendingPlaylistClear.pageHasMore = fPageHasMore;
	fPendingPlaylistClear.snapshotId = fPlaylistSnapshotId;
	for (int32 index = 0; index < fTrackList->CountRows(); index++) {
		BRow* row = fTrackList->RowAt(index);
		bool selected = false;
		for (BRow* current = fTrackList->CurrentSelection(); current;
				current = fTrackList->CurrentSelection(current)) {
			if (current == row) {
				selected = true;
				break;
			}
		}
		fPendingPlaylistClear.rows.push_back(row);
		fPendingPlaylistClear.selected.push_back(selected);
	}
	for (auto row = fPendingPlaylistClear.rows.rbegin();
			row != fPendingPlaylistClear.rows.rend(); ++row)
		fTrackList->RemoveRow(*row);
	fPlaylistClearPending = true;
	fPageOffset = 0;
	fPageTotal = 0;
	fPageHasMore = false;
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();
	BMessenger self(this);
	api->Playlists().ReplacePlaylistItems(_PlaylistId(), {},
		[self](bool ok, const nlohmann::json& data) {
			BMessage result('pClR');
			result.AddBool("ok", ok);
			nlohmann::json body = MutationBody(data);
			result.AddString("snapshot_id",
				JsonString(body, "snapshot_id").c_str());
			self.SendMessage(&result);
		});
}

void
PlaylistWindow::_FinishClearPlaylist(bool success,
	const std::string& snapshotId)
{
	if (!fPlaylistClearPending)
		return;
	if (success) {
		for (BRow* row : fPendingPlaylistClear.rows)
			delete row;
		fPlaylistSnapshotId = snapshotId;
		fCachedPlaylistSnapshotId = snapshotId;
		_DeleteCache();
		if (!snapshotId.empty())
			_SaveCache();
		else
			_RefreshPlaylistSnapshot();
	} else {
		fPageOffset = fPendingPlaylistClear.pageOffset;
		fPageTotal = fPendingPlaylistClear.pageTotal;
		fPageHasMore = fPendingPlaylistClear.pageHasMore;
		fPlaylistSnapshotId = fPendingPlaylistClear.snapshotId;
		for (int32 index = 0;
				index < (int32)fPendingPlaylistClear.rows.size(); index++) {
			BRow* row = fPendingPlaylistClear.rows[index];
			fTrackList->AddRow(row);
			if (fPendingPlaylistClear.selected[index])
				fTrackList->AddToSelection(row);
		}
	}
	fPendingPlaylistClear = PendingPlaylistClear();
	fPlaylistClearPending = false;
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();
}

void
PlaylistWindow::_FinishTrackRemoval(bool success)
{
	if (!fTrackRemovalPending)
		return;

	if (success)
		_ApplyFinishedTrackRemoval();
	else
		_RollbackFinishedTrackRemoval();

	fPendingTrackRemovals.clear();
	fTrackRemovalPending = false;
	_UpdatePlaylistTrackInfo();
	_UpdatePlaylistMenuState();
	if (success)
		_CheckLazyLoad();
}


void
PlaylistWindow::_ApplyFinishedTrackRemoval()
{
	std::vector<int32> removedPositions = _RemovedPlaylistPositions();

	for (const PendingTrackRemoval& pending : fPendingTrackRemovals)
		delete pending.row;

	for (int32 i = 0; fTrackList && i < fTrackList->CountRows(); i++) {
		TrackRow* row = dynamic_cast<TrackRow*>(fTrackList->RowAt(i));
		if (!row || row->fPlaylistPosition < 0)
			continue;
		int32 shift = (int32)std::count_if(removedPositions.begin(),
			removedPositions.end(), [row](int32 removedPosition) {
				return removedPosition < row->fPlaylistPosition;
			});
		row->fPlaylistPosition -= shift;
		if (BIntegerField* number = dynamic_cast<BIntegerField*>(
				row->GetField(0)))
			number->SetValue(row->fPlaylistPosition + 1);
		fTrackList->UpdateRow(row);
	}

	int32 removedBeforeOffset = (int32)std::count_if(
		removedPositions.begin(), removedPositions.end(),
		[this](int32 removedPosition) {
			return removedPosition < fPageOffset;
		});
	fPageOffset = std::max((int32)0, fPageOffset - removedBeforeOffset);
	if (fPageTotal > 0)
		fPageTotal = std::max((int32)0,
			fPageTotal - (int32)removedPositions.size());
	else if (fTrackList)
		fPageTotal = fTrackList->CountRows();
	fPageHasMore = fPageOffset < fPageTotal;
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


std::vector<int32>
PlaylistWindow::_RemovedPlaylistPositions() const
{
	std::vector<int32> removedPositions;
	for (const PendingTrackRemoval& pending : fPendingTrackRemovals)
		removedPositions.push_back(pending.playlistPosition);
	std::sort(removedPositions.begin(), removedPositions.end());
	return removedPositions;
}


void
PlaylistWindow::_RefreshPlaylistSnapshot()
{
	std::string playlistId = _PlaylistId();
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (playlistId.empty() || !api)
		return;

	api->Playlists().InvalidatePlaylist(playlistId);
	BMessenger self(this);
	api->Playlists().GetPlaylist(playlistId, [self](bool ok,
			const nlohmann::json& data) {
		BMessage metadata('pRmM');
		metadata.AddBool("ok", ok);
		if (ok) {
			metadata.AddString("snapshot_id",
				JsonString(data, "snapshot_id").c_str());
			if (data.contains("tracks") && data["tracks"].is_object())
				metadata.AddInt32("total", JsonInt(data["tracks"], "total", -1));
			else if (data.contains("items") && data["items"].is_object())
				metadata.AddInt32("total", JsonInt(data["items"], "total", -1));
		}
		self.SendMessage(&metadata);
	});
}


void
PlaylistWindow::_UpdatePlaylistTrackInfo()
{
	if (!fPlaylistInfo || !fTrackList)
		return;
	char info[64];
	if (fPageTotal > fTrackList->CountRows()) {
		snprintf(info, sizeof(info), "Playlist \xC2\xB7 %ld/%ld Songs",
			(long)fTrackList->CountRows(), (long)fPageTotal);
	} else {
		snprintf(info, sizeof(info), "Playlist \xC2\xB7 %ld Songs",
			(long)fTrackList->CountRows());
	}
	fPlaylistInfo->SetText(info);
}


void
PlaylistWindow::_RenumberPlaylistRows()
{
	if (!fTrackList)
		return;
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		TrackRow* row = dynamic_cast<TrackRow*>(fTrackList->RowAt(i));
		if (!row)
			continue;
		row->fPlaylistPosition = i;
		if (BIntegerField* number = dynamic_cast<BIntegerField*>(
				row->GetField(0))) {
			number->SetValue(i + 1);
		}
		fTrackList->UpdateRow(row);
	}
}


void
PlaylistWindow::_BeginTrackReorder(int32 sourceIndex, int32 rangeLength,
	int32 insertBefore)
{
	std::string playlistId = _PlaylistId();
	if (!_CanBeginTrackReorder(playlistId, sourceIndex, rangeLength))
		return;

	PlaylistReorderPlan plan = ResolvePlaylistReorderPlan(sourceIndex,
		rangeLength, insertBefore, fTrackList->CountRows());
	if (!plan.shouldMove)
		return;
	insertBefore = plan.insertBefore;
	int32 targetIndex = plan.targetIndex;

	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;

	if (!_BuildPendingTrackReorder(sourceIndex, rangeLength, targetIndex))
		return;
	_ApplyPendingTrackReorder();
	fTrackReorderPending = true;
	_UpdatePlaylistMenuState();

	BMessenger self(this);
	api->Playlists().ReorderPlaylistItems(playlistId, sourceIndex, insertBefore,
		rangeLength, fPlaylistSnapshotId,
		[self](bool ok, const nlohmann::json& data) {
			BMessage result('pMvR');
			result.AddBool("ok", ok);
			result.AddInt32("status", SpotifyResponseStatus(data));
			nlohmann::json body = MutationBody(data);
			result.AddString("snapshot_id",
				JsonString(body, "snapshot_id").c_str());
			self.SendMessage(&result);
		});
}


bool
PlaylistWindow::_CanBeginTrackReorder(const std::string& playlistId,
	int32 sourceIndex, int32 rangeLength) const
{
	return fPlaylistOwned && fTrackList && !fPageLoading
		&& !fTrackRemovalPending && !fTrackReorderPending
		&& !fPlaylistClearPending && !playlistId.empty()
		&& sourceIndex >= 0 && rangeLength >= 1
		&& sourceIndex + rangeLength <= fTrackList->CountRows();
}


bool
PlaylistWindow::_BuildPendingTrackReorder(int32 sourceIndex,
	int32 rangeLength, int32 targetIndex)
{
	fPendingTrackReorder = PendingTrackReorder();
	fPendingTrackReorder.sourceIndex = sourceIndex;
	fPendingTrackReorder.targetIndex = targetIndex;
	for (int32 offset = 0; offset < rangeLength; offset++) {
		BRow* row = fTrackList->RowAt(sourceIndex + offset);
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
PlaylistWindow::_ApplyPendingTrackReorder()
{
	for (auto row = fPendingTrackReorder.rows.rbegin();
			row != fPendingTrackReorder.rows.rend(); ++row) {
		fTrackList->RemoveRow(*row);
	}
	for (int32 i = 0; i < (int32)fPendingTrackReorder.rows.size(); i++) {
		fTrackList->AddRow(fPendingTrackReorder.rows[i],
			fPendingTrackReorder.targetIndex + i);
		if (fPendingTrackReorder.selected[i])
			fTrackList->AddToSelection(fPendingTrackReorder.rows[i]);
	}
	_RenumberPlaylistRows();
}


void
PlaylistWindow::_FinishTrackReorder(bool success,
	const std::string& snapshotId)
{
	if (!fTrackReorderPending || !fTrackList)
		return;
	if (!success) {
		for (auto row = fPendingTrackReorder.rows.rbegin();
				row != fPendingTrackReorder.rows.rend(); ++row) {
			fTrackList->RemoveRow(*row);
		}
		for (int32 i = 0; i < (int32)fPendingTrackReorder.rows.size(); i++) {
			fTrackList->AddRow(fPendingTrackReorder.rows[i],
				fPendingTrackReorder.sourceIndex + i);
			if (fPendingTrackReorder.selected[i])
				fTrackList->AddToSelection(fPendingTrackReorder.rows[i]);
		}
	}
	_RenumberPlaylistRows();
	if (success) {
		fPlaylistSnapshotId = snapshotId;
		fCachedPlaylistSnapshotId = snapshotId;
		_DeleteCache();
		if (!snapshotId.empty())
			_SaveCache();
	}
	fPendingTrackReorder = PendingTrackReorder();
	fTrackReorderPending = false;
	_UpdatePlaylistMenuState();
	if (success)
		_CheckLazyLoad();
}


void
PlaylistWindow::_MoveSelectedItem(int32 delta)
{
	if (!_CanMoveSelectedItems(delta))
		return;
	int32 source = fTrackList->CountRows();
	int32 last = -1;
	int32 selectedCount = 0;
	if (!_SelectedRowSpan(source, last, selectedCount))
		return;
	if (last - source + 1 != selectedCount) {
		_ShowContiguousSelectionAlert();
		return;
	}
	if ((delta < 0 && source == 0)
			|| (delta > 0 && last + 1 >= fTrackList->CountRows()))
		return;
	int32 insertBefore = delta > 0 ? last + 2 : source - 1;
	_BeginTrackReorder(source, selectedCount, insertBefore);
}


bool
PlaylistWindow::_CanMoveSelectedItems(int32 delta) const
{
	return fPlaylistOwned && fTrackList && !fTrackRemovalPending
		&& !fTrackReorderPending && !fPlaylistClearPending && delta != 0;
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
	if (fPlaylistDeletePending)
		return;

	std::string playlistId = _PlaylistId();
	if (playlistId.empty())
		return;

	const char* body = fPlaylistOwned
		? B_TRANSLATE("Really delete this playlist? This cannot be undone.")
		: B_TRANSLATE("Unfollow this playlist?");
	const char* action = fPlaylistOwned
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
	if (fPlaylistDeleteItem)
		fPlaylistDeleteItem->SetEnabled(false);

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
		play.AddString("next_queue_uri", row->fTrackUri.c_str());
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
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || fUri.empty()) return;

	fPageLoading = false;
	fPageHasMore = true;
	fPageOffset = 0;
	fPageTotal = 0;
	fPlaylistSnapshotId.clear();
	fCachedPlaylistSnapshotId.clear();
	fPodcastHeadRefreshing = false;
	fEpisodeOffset = 0;
	fEpisodeTotal = 0;
	fEpisodeSearchFilter = fSearchBox ? fSearchBox->Text() : "";
	fEpisodeSearchGeneration++;
	fEpisodeSearchRetryCount = 0;
	fEpisodeSearchPaging = !fEpisodeSearchFilter.empty();
	fEpisodeSearchWaitingRetry = false;
	fEpisodeSearchFailed = false;
	delete fEpisodeSearchRetryRunner;
	fEpisodeSearchRetryRunner = nullptr;
	fEpisodes.clear();
	fPendingPodcastHeadEpisodes.clear();
	if (fTrackList)
		fTrackList->Clear();

	bool loadFirstPage = true;

	SpotifyItemKind kind = SpotifyItemKindForUri(fUri);
	if (fUri == "spotify:collection") {
		loadFirstPage = _PrepareCollectionLoad(*api);
	} else if (kind == kSpotifyItemPlaylist) {
		loadFirstPage = _PreparePlaylistLoad(*api, SpotifyItemIdForUri(fUri));
	} else if (kind == kSpotifyItemAlbum) {
		loadFirstPage = _PrepareAlbumLoad(*api, SpotifyItemIdForUri(fUri));
	} else if (kind == kSpotifyItemShow) {
		loadFirstPage = _PrepareShowLoad(*api, SpotifyItemIdForUri(fUri),
			ignoreEpisodeCache);
	}

	if (loadFirstPage)
		_LoadNextPage();
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
	api.Playlists().GetPlaylist(playlistId, [messenger](bool ok,
			const nlohmann::json& data) {
		if (!ok) return;
		BMessage* metaMsg = new BMessage('plMt');
		metaMsg->AddString("title",
			JsonString(data, "name", "Playlist").c_str());
		metaMsg->AddString("snapshot_id", JsonString(data,
			"snapshot_id").c_str());
		metaMsg->AddString("description", JsonDescription(data).c_str());
		metaMsg->AddBool("public", JsonBool(data, "public"));
		if (data.contains("owner") && data["owner"].is_object()) {
			std::string ownerId = JsonString(data["owner"], "account_id",
				JsonString(data["owner"], "id"));
			metaMsg->AddString("owner_id", ownerId.c_str());
		}
		if (data.contains("tracks") && data["tracks"].is_object())
			metaMsg->AddInt32("total",
				(int32)JsonInt(data["tracks"], "total"));
		else if (data.contains("items") && data["items"].is_object())
			metaMsg->AddInt32("total",
				(int32)JsonInt(data["items"], "total"));
		if (data.contains("images") && data["images"].is_array()
		        && !data["images"].empty()) {
			std::string url = JsonString(data["images"][0], "url");
			if (!url.empty())
				metaMsg->AddString("cover_url", url.c_str());
		}
		messenger.SendMessage(metaMsg);
		delete metaMsg;
	});
	api.Profile().GetCurrentUserProfile([messenger](bool ok,
		const nlohmann::json& profile) {
		if (!ok || !profile.is_object()) return;
		BMessage user('plUs');
		std::string legacyId = JsonString(profile, "id");
		user.AddString("user_id", JsonString(profile, "account_id",
			legacyId).c_str());
		user.AddString("legacy_user_id", legacyId.c_str());
		messenger.SendMessage(&user);
	});

	return loadFirstPage;
}


bool
PlaylistWindow::_PrepareAlbumLoad(SpotifyApi& api, const std::string& albumId)
{
	BMessenger messenger(this);
	api.Content().GetAlbum(albumId, [messenger](bool ok,
			const nlohmann::json& albumData) {
		std::string albumName = "Album";
		if (ok) {
			albumName = JsonString(albumData, "name", "Album");
			BMessage* titleMsg = new BMessage('uTtl');
			titleMsg->AddString("title", albumName.c_str());
			messenger.SendMessage(titleMsg);
			delete titleMsg;

			std::string coverUrl;
			if (albumData.contains("images")
			        && albumData["images"].is_array()
			        && !albumData["images"].empty()) {
				coverUrl = JsonString(albumData["images"][0], "url");
			}
			BMessage* covMsg = new BMessage('uCov');
			covMsg->AddString("url", coverUrl.c_str());
			messenger.SendMessage(covMsg);
			delete covMsg;
		}
	});
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
		bool valid = ok && data.is_array() && !data.empty()
			&& data[0].is_boolean();
		BMessage* subMsg = new BMessage('subU');
		subMsg->AddBool("ok", valid);
		if (valid)
			subMsg->AddBool("following", data[0].get<bool>());
		messenger.SendMessage(subMsg);
		delete subMsg;
	});

	api.Content().GetShow(showId, [messenger](bool ok,
			const nlohmann::json& data) {
		if (!ok) return;
		BMessage* titleMsg = new BMessage('uTtl');
		titleMsg->AddString("title",
			JsonString(data, "name", "Podcast").c_str());
		messenger.SendMessage(titleMsg);
		delete titleMsg;
		std::string url;
		if (data.contains("images") && data["images"].is_array()
		        && !data["images"].empty()) {
			url = JsonString(data["images"][0], "url");
		}
		BMessage* covMsg = new BMessage('uCov');
		covMsg->AddString("url", url.c_str());
		messenger.SendMessage(covMsg);
		delete covMsg;
	});

	if (ignoreEpisodeCache || !_LoadCache())
		return true;

	_RebuildEpisodeList(fEpisodeSearchFilter);
	_UpdateEpisodeInfo();
	if (!fCurrentPlayingTrackUri.empty())
		SetPlayingTrack(fCurrentPlayingTrackUri.c_str());
	_RefreshPodcastHead(0);
	return false;
}


void
PlaylistWindow::_LoadNextPage()
{
	if (fPageLoading || !fPageHasMore)
		return;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || fUri.empty())
		return;

	fPageLoading = true;
	BMessenger messenger(this);
	int32 offset = fPageOffset;
	int32 limit = fPageBatchSize;
	SpotifyItemKind kind = SpotifyItemKindForUri(fUri);
	int32 episodeSearchGeneration = kind == kSpotifyItemShow
		&& fEpisodeSearchPaging ? fEpisodeSearchGeneration : -1;

	if (fUri == "spotify:collection") {
		_LoadCollectionPage(*api, messenger, offset, limit,
			episodeSearchGeneration);
		return;
	}

	if (kind == kSpotifyItemPlaylist) {
		_LoadPlaylistPage(*api, messenger, SpotifyItemIdForUri(fUri), offset,
			limit, episodeSearchGeneration);
		return;
	}

	if (kind == kSpotifyItemAlbum) {
		_LoadAlbumPage(*api, messenger, SpotifyItemIdForUri(fUri), offset,
			limit, episodeSearchGeneration);
		return;
	}

	if (kind == kSpotifyItemShow) {
		_LoadShowPage(*api, messenger, SpotifyItemIdForUri(fUri), offset,
			limit, episodeSearchGeneration);
		return;
	}

	fPageLoading = false;
	fPageHasMore = false;
}


void
PlaylistWindow::_LoadCollectionPage(SpotifyApi& api,
	const BMessenger& messenger, int32 offset, int32 limit,
	int32 searchGeneration)
{
	api.Library().GetSavedTracks(offset, limit,
		[messenger, offset, searchGeneration](bool ok,
				const nlohmann::json& data) {
		if (!ok || !data.contains("items")) {
			SendPageLoadFailure(messenger, searchGeneration, data);
			return;
		}
		BMessage* msg = CreateTrackPageMessage(offset, data);
		int32 itemIndex = 0;
		for (const auto& item : data["items"]) {
			int32 number = offset + itemIndex + 1;
			itemIndex++;
			if (!item.contains("track") || !item["track"].is_object())
				continue;
			AddTrackToMessage(msg, item["track"], number, "", "");
		}
		messenger.SendMessage(msg);
		delete msg;
	});
}


void
PlaylistWindow::_LoadPlaylistPage(SpotifyApi& api,
	const BMessenger& messenger, const std::string& playlistId, int32 offset,
	int32 limit, int32 searchGeneration)
{
	api.Playlists().GetPlaylistTracks(playlistId, offset, limit,
		[messenger, offset, searchGeneration](bool ok,
				const nlohmann::json& data) {
		if (!ok || !data.contains("items")) {
			SendPageLoadFailure(messenger, searchGeneration, data);
			return;
		}
		BMessage* msg = CreateTrackPageMessage(offset, data);
		int32 itemIndex = 0;
		for (const auto& item : data["items"]) {
			int32 number = offset + itemIndex + 1;
			itemIndex++;
			if (item.contains("item") && item["item"].is_object())
				AddTrackToMessage(msg, item["item"], number, "", "");
			else if (item.contains("track") && item["track"].is_object())
				AddTrackToMessage(msg, item["track"], number, "", "");
		}
		messenger.SendMessage(msg);
		delete msg;
	});
}


void
PlaylistWindow::_LoadAlbumPage(SpotifyApi& api,
	const BMessenger& messenger, const std::string& albumId, int32 offset,
	int32 limit, int32 searchGeneration)
{
	std::string albumName = fPlaylistName ? fPlaylistName->Text() : "Album";
	api.Content().GetAlbumTracks(albumId, offset, limit,
		[messenger, offset, albumId, albumName, searchGeneration](
				bool ok, const nlohmann::json& data) {
		if (!ok || !data.contains("items")) {
			SendPageLoadFailure(messenger, searchGeneration, data);
			return;
		}
		BMessage* msg = CreateTrackPageMessage(offset, data);
		int32 itemIndex = 0;
		for (const auto& track : data["items"]) {
			int32 number = offset + itemIndex + 1;
			itemIndex++;
			if (track.is_object()) {
				AddTrackToMessage(msg, track, number, albumName,
					SpotifyUriForItemKind(kSpotifyItemAlbum, albumId));
			}
		}
		messenger.SendMessage(msg);
		delete msg;
	});
}


void
PlaylistWindow::_LoadShowPage(SpotifyApi& api,
	const BMessenger& messenger, const std::string& showId, int32 offset,
	int32 limit, int32 searchGeneration)
{
	api.Content().GetShowEpisodes(showId, offset, limit,
		[messenger, offset, searchGeneration](bool ok,
				const nlohmann::json& data) {
		if (!ok || !data.contains("items")) {
			SendPageLoadFailure(messenger, searchGeneration, data);
			return;
		}
		BMessage* msg = CreateEpisodePageMessage(offset, data);
		int32 itemIndex = 0;
		for (const auto& ep : data["items"]) {
			int32 number = offset + itemIndex + 1;
			itemIndex++;
			if (ep.is_object())
				AddEpisodeToMessage(msg, ep, number);
			else
				AddUnavailableEpisodeToMessage(msg, number);
		}
		messenger.SendMessage(msg);
		delete msg;
	});
}


void
PlaylistWindow::_CheckLazyLoad()
{
	if (!_CanLazyLoadPage())
		return;
	if (!fEpisodeSearchFilter.empty()) {
		if (fEpisodeSearchPaging && !fEpisodeSearchWaitingRetry)
			_LoadNextPage();
		return;
	}

	if (_ShouldLoadNextPageForScroll())
		_LoadNextPage();
}


bool
PlaylistWindow::_CanLazyLoadPage() const
{
	return fTrackList && !fPageLoading && !fPodcastHeadRefreshing
		&& !fTrackRemovalPending && !fTrackReorderPending
		&& !fPlaylistClearPending && fPageHasMore;
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

static std::string
sFormatDate(const std::string& isoDate)
{
	int year = 0, month = 0, day = 0;
	if (sscanf(isoDate.c_str(), "%d-%d-%d", &year, &month, &day) < 3
			|| year <= 0 || month <= 0 || day <= 0)
		return isoDate;

	BDate bdate(year, month, day);
	BDateFormat formatter;
	BString result;
	if (formatter.Format(result, bdate, B_SHORT_DATE_FORMAT) == B_OK)
		return std::string(result.String());
	return isoDate;
}


void
PlaylistWindow::_RebuildEpisodeList(const std::string& filter)
{
	fTrackList->Clear();

	std::string normalizedFilter = NormalizePlaylistEpisodeFilter(filter);

	for (const auto& ep : fEpisodes) {
		if (!PlaylistEpisodeMatchesFilter(ep, normalizedFilter))
			continue;
		fTrackList->AddRow(EpisodeRowFromEpisode(ep,
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

		fTrackList->AddRow(EpisodeRowFromEpisode(episode,
			fCurrentPlayingTrackUri));
	}
}

void
PlaylistWindow::_UpdateEpisodeInfo()
{
	if (!fPlaylistInfo)
		return;

	char info[64];
	if (fEpisodeTotal > (int32)fEpisodes.size())
		snprintf(info, sizeof(info),
			B_TRANSLATE("Podcast \xC2\xB7 %d/%d Episodes"),
			(int)fEpisodes.size(), (int)fEpisodeTotal);
	else
		snprintf(info, sizeof(info),
			B_TRANSLATE("Podcast \xC2\xB7 %d Episodes"),
			(int)fEpisodes.size());
	fPlaylistInfo->SetText(info);

	if (!fPodcastSearchInfo)
		return;
	if (fEpisodeSearchFilter.empty()) {
		fPodcastSearchInfo->SetText("");
		return;
	}

	char searchInfo[96];
	int32 matches = fTrackList ? fTrackList->CountRows() : 0;
	if (fEpisodeSearchWaitingRetry) {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - retrying search..."), (int)matches);
	} else if (fEpisodeSearchPaging && fEpisodeTotal > 0) {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - searching: %d/%d"), (int)matches,
			(int)fEpisodeOffset, (int)fEpisodeTotal);
	} else if (fEpisodeSearchPaging) {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - searching: %d"), (int)matches,
			(int)fEpisodeOffset);
	} else if (fEpisodeSearchFailed && fEpisodeTotal > 0) {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - stopped: %d/%d"), (int)matches,
			(int)fEpisodeOffset, (int)fEpisodeTotal);
	} else {
		snprintf(searchInfo, sizeof(searchInfo), B_TRANSLATE(
			"%d matches - searched: %d"), (int)matches,
			(int)fEpisodeOffset);
	}
	fPodcastSearchInfo->SetText(searchInfo);
}

void
PlaylistWindow::_RenumberEpisodes()
{
	for (size_t i = 0; i < fEpisodes.size(); i++)
		fEpisodes[i].number = (int32)i + 1;
}

bool
PlaylistWindow::_HasEpisode(const std::string& uri, const std::string& title,
	const std::string& date, const std::string& duration) const
{
	for (const auto& episode : fEpisodes) {
		if (!uri.empty()) {
			if (episode.trackUri == uri)
				return true;
			continue;
		}
		if (episode.trackUri.empty() && episode.title == title
				&& episode.date == date && episode.duration == duration)
			return true;
	}
	return false;
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

	if (offset == 0) {
		if (fPodcastHeadRefreshing)
			return;
		fPendingPodcastHeadEpisodes.clear();
		api->Content().InvalidateShowEpisodes(SpotifyItemIdForUri(fUri));
	}

	fPodcastHeadRefreshing = true;
	std::string id = SpotifyItemIdForUri(fUri);
	BMessenger messenger(this);
	int32 limit = fPageBatchSize;

	api->Content().GetShowEpisodes(id, offset, limit,
		[messenger, offset](bool ok, const nlohmann::json& data) {
		BMessage* msg = new BMessage('pEpR');
		msg->AddBool("ok", ok);
		msg->AddInt32("offset", offset);
		if (ok && data.contains("items")) {
			msg->AddInt32("total", (int32)JsonInt(data, "total"));
			int32 pageCount = (int32)data["items"].size();
			msg->AddInt32("page_count", pageCount);
			msg->AddInt32("next_offset", offset + pageCount);
			int32 itemIndex = 0;
			for (const auto& ep : data["items"]) {
				int32 number = offset + itemIndex + 1;
				itemIndex++;
				if (ep.is_object())
					AddEpisodeToMessage(msg, ep, number);
				else
					AddUnavailableEpisodeToMessage(msg, number);
			}
		}
		messenger.SendMessage(msg);
		delete msg;
	});
}

void
PlaylistWindow::_FinishPodcastHeadRefresh()
{
	fPodcastHeadRefreshing = false;

	if (!fPendingPodcastHeadEpisodes.empty()) {
		fEpisodes.insert(fEpisodes.begin(), fPendingPodcastHeadEpisodes.begin(),
			fPendingPodcastHeadEpisodes.end());
		fPendingPodcastHeadEpisodes.clear();
		_RenumberEpisodes();
		_RebuildEpisodeList(fEpisodeSearchFilter);
		_SaveCache();
	} else {
		fPendingPodcastHeadEpisodes.clear();
	}

	if (fEpisodeOffset < (int32)fEpisodes.size())
		fEpisodeOffset = (int32)fEpisodes.size();
	fPageOffset = fEpisodeOffset;
	fPageTotal = fEpisodeTotal;
	fPageHasMore = fEpisodeTotal <= 0 || fEpisodeOffset < fEpisodeTotal;
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
	SpotifyItemKind kind = SpotifyItemKindForUri(fUri);
	if (fUri == "spotify:collection" || kind == kSpotifyItemPlaylist)
		return _LoadTrackCache(kind == kSpotifyItemPlaylist);
	if (kind == kSpotifyItemShow)
		return _LoadShowCache();
	return false;
}


bool
PlaylistWindow::_LoadTrackCache(bool isPlaylist)
{
	try {
		PlaylistCacheFiles::TrackDocument document;
		if (!PlaylistCacheFiles::ReadTrackDocument(isPlaylist,
				SpotifyItemIdForUri(fUri), document)) {
			return false;
		}
		if (isPlaylist) {
			fCachedPlaylistSnapshotId = document.snapshotId;
			fPlaylistSnapshotId = fCachedPlaylistSnapshotId;
		}

		if (fTrackList)
			fTrackList->Clear();
		fPageTotal = document.total;
		fPageOffset = document.nextOffset;

		AddCachedTrackRows(document.tracks, fTrackList, fCurrentPlayingTrackUri);
		CachedPageState pageState = ResolveCachedTrackPageState(fPageOffset,
			fPageTotal, fTrackList->CountRows());
		fPageOffset = pageState.offset;
		fPageTotal = pageState.total;
		fPageHasMore = pageState.hasMore;
		SetCachedTrackInfo(fPlaylistInfo, isPlaylist, fPageTotal,
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
		if (!PlaylistCacheFiles::ReadShowDocument(SpotifyItemIdForUri(fUri),
				document)) {
			return false;
		}
		fEpisodeTotal = document.total;
		fEpisodes.clear();
		for (const PlaylistCacheDocument::Episode& cached
				: document.episodes) {
			fEpisodes.push_back(PlaylistEpisodeFromCache(cached));
		}
		_RenumberEpisodes();
		CachedPageState pageState = ResolveCachedEpisodePageState(
			document.nextOffset, fEpisodeTotal, (int32)fEpisodes.size(),
			document.hasNextOffset);
		if (!pageState.valid) {
			fEpisodes.clear();
			return false;
		}
		fEpisodeOffset = pageState.offset;
		fPageOffset = fEpisodeOffset;
		fEpisodeTotal = pageState.total;
		fPageTotal = pageState.total;
		fPageHasMore = pageState.hasMore;
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
	SpotifyItemKind kind = SpotifyItemKindForUri(fUri);
	if (fUri == "spotify:collection" || kind == kSpotifyItemPlaylist) {
		_WriteTrackCache(kind == kSpotifyItemPlaylist);
		return;
	}

	if (kind == kSpotifyItemShow)
		_WriteShowCache();
}


bool
PlaylistWindow::_WriteTrackCache(bool isPlaylist)
{
	if (!fTrackList)
		return false;
	if (isPlaylist && fPlaylistSnapshotId.empty())
		return false;

	std::vector<PlaylistCacheDocument::Track> tracks;
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		TrackRow* row = (TrackRow*)fTrackList->RowAt(i);
		if (row)
			tracks.push_back(CachedTrackFromRow(row, i));
	}

	return PlaylistCacheFiles::WriteTrackDocument(isPlaylist,
		SpotifyItemIdForUri(fUri), fPageTotal, fPageOffset,
		fPlaylistSnapshotId, tracks);
}


void
PlaylistWindow::_WriteShowCache()
{
	std::vector<PlaylistCacheDocument::Episode> episodes;
	for (const PlaylistEpisode& ep : fEpisodes)
		episodes.push_back(CacheEpisodeFromPlaylistEpisode(ep));

	PlaylistCacheFiles::WriteShowDocument(SpotifyItemIdForUri(fUri),
		fEpisodeTotal, fEpisodeOffset,
		fEpisodeTotal <= 0 || fEpisodeOffset >= fEpisodeTotal, episodes);
}

void
PlaylistWindow::_DeleteCache()
{
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	if (fUri == "spotify:collection") {
		PlaylistCacheFiles::RemoveLikedSongs();
		return;
	}
	if (SpotifyItemKindForUri(fUri) == kSpotifyItemPlaylist) {
		PlaylistCacheFiles::RemovePlaylist(SpotifyItemIdForUri(fUri));
		return;
	}
	if (SpotifyItemKindForUri(fUri) != kSpotifyItemShow) return;
	PlaylistCacheFiles::RemoveShow(SpotifyItemIdForUri(fUri));
}


PlaylistWindow::~PlaylistWindow()
{
	delete fPlaylistCoverPanel;
	delete fEpisodeSearchRunner;
	delete fEpisodeSearchRetryRunner;
	delete fCacheSaveRunner;
	fCacheSaveRunner = nullptr;
	if (!fTrackRemovalPending && !fTrackReorderPending
			&& !fPlaylistClearPending)
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
