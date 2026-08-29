#include "ArtistWindow.h"
#include "ArtworkView.h"
#include "TrackContextMenu.h"
#include "App.h"
#include "Messages.h"
#include "HaifyDebug.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"
#include <nlohmann/json.hpp>

#include <Application.h>
#include <Bitmap.h>
#include <Alignment.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <SplitView.h>
#include <StringView.h>
#include <Catalog.h>
#include <Button.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ArtistWindow"

static const uint32 kMsgArtistMetadata = 'amta';
static const uint32 kMsgArtistTopTracks = 'atrk';
static const uint32 kMsgArtistAlbums = 'aalb';
static const uint32 kMsgRetryTopTracks = 'rtTr';
static const int32 kMaxTopTracksRetries = 2;
static const int32 kArtistTopTracksOfficial = 0;
static const int32 kArtistTopTracksSearchFallback = 1;

static const char*
ArtistTopTracksLabel(int32 source)
{
	if (source == kArtistTopTracksSearchFallback)
		return B_TRANSLATE("Tracks by Artist");
	return B_TRANSLATE("Top Tracks");
}

static bool
IsListContextClick(BMessage* message)
{
	if (!message || message->what != B_MOUSE_DOWN)
		return false;

	int32 buttons = 0;
	return message->FindInt32("buttons", &buttons) == B_OK
		&& (buttons & (B_SECONDARY_MOUSE_BUTTON
			| B_TERTIARY_MOUSE_BUTTON)) != 0;
}


static bool
IsContextMenuTarget(BView* view, BColumnListView* owner)
{
	if (!owner || !view || dynamic_cast<BScrollBar*>(view))
		return false;
	if (view->Name() && strcmp(view->Name(), "header") == 0)
		return false;
	for (BView* parent = view; parent; parent = parent->Parent()) {
		if (parent == owner || parent == owner->ScrollView())
			return true;
	}
	return false;
}


static bool
FindContextMenuScreenPoint(BMessage* message, BView* view, BPoint& screen)
{
	if (message->FindPoint("screen_where", &screen) == B_OK)
		return true;

	BPoint where;
	if (message->FindPoint("where", &where) != B_OK)
		return false;
	screen = view->ConvertToScreen(where);
	return true;
}


static filter_result
PostContextMenuRequest(BMessage* message, BHandler** target,
	BColumnListView* owner)
{
	if (!IsListContextClick(message))
		return B_DISPATCH_MESSAGE;

	BView* view = dynamic_cast<BView*>(*target);
	if (!IsContextMenuTarget(view, owner))
		return B_DISPATCH_MESSAGE;

	BPoint screen;
	if (!FindContextMenuScreenPoint(message, view, screen))
		return B_DISPATCH_MESSAGE;

	BMessage show('rCf!');
	show.AddPoint("screenPt", screen);
	if (owner->Looper())
		owner->Looper()->PostMessage(&show, owner);
	return B_SKIP_MESSAGE;
}


static bool
BuildArtistMetadataMessage(const nlohmann::json& data, BMessage& message)
{
	if (!data.is_object())
		return false;

	message.AddString("name", data.value("name", "Unknown").c_str());
	int followers = 0;
	if (data.contains("followers") && data["followers"].is_object())
		followers = data["followers"].value("total", 0);
	message.AddInt32("followers", (int32)followers);
	if (data.contains("images") && data["images"].is_array()
			&& !data["images"].empty()) {
		message.AddString("imageUrl",
			data["images"][0].value("url", "").c_str());
	}
	return true;
}


static bool
BuildArtistAlbumsMessage(const nlohmann::json& data, BMessage& message)
{
	if (!data.contains("items") || !data["items"].is_array())
		return false;

	for (const auto& album : data["items"]) {
		if (!album.is_object())
			continue;
		message.AddString("id", album.value("id", "").c_str());
		message.AddString("name", album.value("name", "").c_str());
		std::string releaseDate = album.value("release_date", "");
		message.AddString("year", releaseDate.size() >= 4
			? releaseDate.substr(0, 4).c_str() : releaseDate.c_str());
		message.AddString("type", album.value("album_type", "").c_str());
		std::string cover;
		if (album.contains("images") && album["images"].is_array()
				&& !album["images"].empty())
			cover = album["images"][0].value("url", "");
		message.AddString("cover", cover.c_str());
	}
	return true;
}

class TrackArtistStringField : public BStringField {
public:
	bool fIsPlaying = false;
	TrackArtistStringField(const char* s) : BStringField(s) {}
};

class TrackArtistStringColumn : public BStringColumn {
public:
	TrackArtistStringColumn(const char* title, float w, float mn, float mx, uint32 tr)
		: BStringColumn(title, w, mn, mx, tr) {}

	virtual void DrawField(BField* field, BRect rect, BView* parent) {
		TrackArtistStringField* f = dynamic_cast<TrackArtistStringField*>(field);
		BFont font;
		parent->GetFont(&font);
		if (f && f->fIsPlaying) {
			BFont bold(be_bold_font);
			bold.SetSize(font.Size());
			parent->SetFont(&bold);
		}
		BStringColumn::DrawField(field, rect, parent);
		parent->SetFont(&font);
	}
};


class TrackArtistRow : public BRow {
public:
	std::string fTrackUri;
	std::string fAlbumUri;
	TrackArtistRow(const std::string& uri) : BRow(), fTrackUri(uri) {}
};

class AlbumArtistRow : public BRow {
public:
	std::string fAlbumId, fAlbumName, fCoverUrl;
	AlbumArtistRow(const std::string& id, const std::string& name,
	               const std::string& cover)
		: BRow(), fAlbumId(id), fAlbumName(name), fCoverUrl(cover) {}
};



class ArtistWindow;

class TrackArtistListView : public BColumnListView {
public:
	TrackArtistListView(const char* name)
		: BColumnListView(name, B_NAVIGABLE, B_FANCY_BORDER, true) {}

	class RightClickFilter : public BMessageFilter {
	public:
		RightClickFilter(TrackArtistListView* owner)
			: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
			  fOwner(owner) {}
		filter_result Filter(BMessage* msg, BHandler** target) override {
			return PostContextMenuRequest(msg, target, fOwner);
		}
	private:
		TrackArtistListView* fOwner;
	};

	virtual void AttachedToWindow() {
		BColumnListView::AttachedToWindow();
		if (BView* outline = ScrollView())
			outline->AddFilter(new RightClickFilter(this));
		else
			AddFilter(new RightClickFilter(this));
	}

	virtual void MouseDown(BPoint pt) {
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
			BPoint screen = pt;
			ConvertToScreen(&screen);
			((ArtistWindow*)Window())->ShowTrackContextMenu(pt, screen);
			return;
		}
		BColumnListView::MouseDown(pt);
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
				((ArtistWindow*)Window())->ShowTrackContextMenu(where, screen);
			}
			return;
		}
		BColumnListView::MessageReceived(msg);
	}

	virtual void ItemInvoked() {
		TrackArtistRow* row = dynamic_cast<TrackArtistRow*>(CurrentSelection());
		if (!row || !Window()) return;
		BPoint where; uint32 buttons;
		GetMouse(&where, &buttons, false);
		int32 col = _ColumnAt(where.x);
		if (col == 1 && !row->fTrackUri.empty()) {
			Window()->PostMessage(new BMessage('tply'));
		} else if (col == 2 && !row->fAlbumUri.empty()) {
			std::string id = SpotifyItemIdForUri(row->fAlbumUri);
			if (id.empty())
				return;
			BMessage msg(MSG_SHOW_ALBUM);
			msg.AddString("id", id.c_str());
			be_app->PostMessage(&msg);
		}
	}

	virtual bool InitiateDrag(BPoint point, bool) {
		TrackArtistRow* row = dynamic_cast<TrackArtistRow*>(CurrentSelection());
		if (!row)
			row = dynamic_cast<TrackArtistRow*>(RowAt(point));
		if (!row || row->fTrackUri.empty())
			return false;

		BMessage drag('drag');
		drag.AddString("uri", row->fTrackUri.c_str());
		drag.AddString("itemType", "track");
		drag.AddString("trackUri", row->fTrackUri.c_str());
		BStringField* title = dynamic_cast<BStringField*>(row->GetField(1));
		if (title)
			drag.AddString("title", title->String());
		BRect dragRect(point.x - 100, point.y - 10, point.x + 100, point.y + 10);
		DragMessage(&drag, dragRect, this);
		return true;
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


class AlbumArtistListView : public BColumnListView {
public:
	AlbumArtistListView()
		: BColumnListView("albums", B_NAVIGABLE, B_FANCY_BORDER, true) {}

	class RightClickFilter : public BMessageFilter {
	public:
		RightClickFilter(AlbumArtistListView* owner)
			: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
			  fOwner(owner) {}
		filter_result Filter(BMessage* msg, BHandler** target) override {
			return PostContextMenuRequest(msg, target, fOwner);
		}
	private:
		AlbumArtistListView* fOwner;
	};

	virtual void AttachedToWindow() {
		BColumnListView::AttachedToWindow();
		if (BView* outline = ScrollView())
			outline->AddFilter(new RightClickFilter(this));
		else
			AddFilter(new RightClickFilter(this));
	}

	virtual void MouseDown(BPoint point) {
		BMessage* msg = Window() ? Window()->CurrentMessage() : nullptr;
		int32 buttons = 0;
		if (msg)
			msg->FindInt32("buttons", &buttons);
		BPoint livePoint;
		uint32 liveButtons = 0;
		GetMouse(&livePoint, &liveButtons, false);
		buttons |= liveButtons;
		bool contextClick = (buttons & (B_SECONDARY_MOUSE_BUTTON
			| B_TERTIARY_MOUSE_BUTTON)) != 0
			|| (buttons != 0 && (buttons & B_PRIMARY_MOUSE_BUTTON) == 0);
		if (!contextClick) {
			BColumnListView::MouseDown(point);
			return;
		}

		BPoint screen = point;
		ConvertToScreen(&screen);
		_ShowContextMenuAt(screen);
	}

	virtual void MessageReceived(BMessage* msg) {
		if (msg->what == 'rCf!') {
			BPoint screen;
			if (msg->FindPoint("screenPt", &screen) == B_OK)
				_ShowContextMenuAt(screen);
			return;
		}
		BColumnListView::MessageReceived(msg);
	}

private:
	void _ShowContextMenuAt(BPoint screen) {
		BPoint point = screen;
		if (BView* outline = ScrollView())
			outline->ConvertFromScreen(&point);
		else
			ConvertFromScreen(&point);

		AlbumArtistRow* row = dynamic_cast<AlbumArtistRow*>(RowAt(point));
		if (!row)
			row = dynamic_cast<AlbumArtistRow*>(CurrentSelection());
		if (!row || row->fAlbumId.empty())
			return;
		AddToSelection(row);

		std::string uri = SpotifyUriForItemKind(kSpotifyItemAlbum,
			row->fAlbumId);
		if (uri.empty())
			return;
		BPopUpMenu* menu = new BPopUpMenu("album", false, false);
		BMessage* openMsg = new BMessage('aopn');
		menu->AddItem(new BMenuItem(B_TRANSLATE("Open"), openMsg));
		BMessage* saveMsg = new BMessage('savA');
		saveMsg->AddString("uri", uri.c_str());
		menu->AddItem(new BMenuItem(B_TRANSLATE("Save Album"), saveMsg));
		BMenuItem* sel = menu->Go(screen, false, true);
		if (sel && sel->Message() && Window())
			Window()->PostMessage(sel->Message());
		delete menu;
	}

public:
	virtual bool InitiateDrag(BPoint point, bool) {
		AlbumArtistRow* row = dynamic_cast<AlbumArtistRow*>(CurrentSelection());
		if (!row)
			row = dynamic_cast<AlbumArtistRow*>(RowAt(point));
		if (!row || row->fAlbumId.empty())
			return false;

		std::string uri = SpotifyUriForItemKind(kSpotifyItemAlbum,
			row->fAlbumId);
		if (uri.empty())
			return false;
		BMessage drag('drag');
		drag.AddString("uri", uri.c_str());
		drag.AddString("itemType", "album");
		drag.AddString("albumUri", uri.c_str());
		drag.AddString("title", row->fAlbumName.c_str());
		BRect dragRect(point.x - 100, point.y - 10, point.x + 100, point.y + 10);
		DragMessage(&drag, dragRect, this);
		return true;
	}
};


static std::string FormatMs(int ms) {
	int s = ms / 1000;
	char buf[16];
	snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
	return buf;
}

static std::string FormatFollowers(int n) {
	char buf[32];
	if (n >= 1000000)
		snprintf(buf, sizeof(buf), "%.1fM Followers", n / 1000000.0f);
	else if (n >= 1000)
		snprintf(buf, sizeof(buf), "%.1fK Followers", n / 1000.0f);
	else
		snprintf(buf, sizeof(buf), "%d Followers", n);
	return buf;
}


ArtistWindow::ArtistWindow(const std::string& artistId)
	: BWindow(BRect(100, 100, 680, 600), "Artist",
	          B_DOCUMENT_WINDOW,
	          B_ASYNCHRONOUS_CONTROLS),
	  fArtistId(artistId)
{
	fArtworkView = new ArtworkView("artistCover");
	fArtworkView->ShowLoading();
	fArtworkView->SetExplicitMinSize(BSize(110, 110));
	fArtworkView->SetExplicitMaxSize(BSize(110, 110));
	fArtworkView->SetExplicitPreferredSize(BSize(110, 110));
	fArtworkView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	fNameView = new BStringView("artistName", B_UTF8_ELLIPSIS);
	BFont bigFont(be_bold_font);
	bigFont.SetSize(be_plain_font->Size() * 2.0f);
	fNameView->SetFont(&bigFont);
	fNameView->SetAlignment(B_ALIGN_LEFT);
	fNameView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
	fNameView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fFollowersView = new BStringView("artistFollowers", "");
	fFollowersView->SetAlignment(B_ALIGN_LEFT);
	fFollowersView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
	fFollowersView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fFollowButton = new BButton("followArtist", B_TRANSLATE("Follow"),
		new BMessage('afol'));
	fFollowButton->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_BOTTOM));


	fTrackList = new TrackArtistListView("tracks");
	fTrackList->AddColumn(new BStringColumn(
	    "#",        28,  20,  40,  B_TRUNCATE_END), 0);
	fTrackList->AddColumn(new TrackArtistStringColumn(
	    "Title",   200,  60, 400,  B_TRUNCATE_END), 1);
	fTrackList->AddColumn(new TrackArtistStringColumn(
	    "Album",   160,  50, 350,  B_TRUNCATE_END), 2);
	fTrackList->AddColumn(new TrackArtistStringColumn(
	    "Duration", 58,  40,  80,  B_TRUNCATE_END), 3);
	fTrackList->SetExplicitMinSize(BSize(B_SIZE_UNSET, 80));
	fTrackList->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 220));
	fTrackList->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 220));

	fTracksLabel = new BStringView("tracksLabel", B_TRANSLATE("Top Tracks"));

	fAlbumList = new AlbumArtistListView();
	fAlbumList->AddColumn(new BStringColumn(
	    "Title",  250,  60, 500, B_TRUNCATE_END), 0);
	fAlbumList->AddColumn(new BStringColumn(
	    "Year",    55,  40,  70, B_TRUNCATE_END), 1);
	fAlbumList->AddColumn(new BStringColumn(
	    "Type",    80,  50, 120, B_TRUNCATE_END), 2);
	fAlbumList->SetExplicitMinSize(BSize(B_SIZE_UNSET, 140));
	fAlbumList->SetInvocationMessage(new BMessage('aopn'));

	float followerIndent = std::ceil(be_plain_font->StringWidth(" "));
	BSplitView* contentSplit = new BSplitView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Split<>(contentSplit)
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.AddGroup(B_VERTICAL, 0, 0.0f)
				.SetInsets(12, 6, 12, 0)
				.Add(fTracksLabel)
			.End()
			.Add(fTrackList, 1.0f)
		.End()
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.AddGroup(B_VERTICAL, 0, 0.0f)
				.SetInsets(12, 6, 12, 0)
				.Add(new BStringView("albumsLabel", "Albums"))
			.End()
			.Add(fAlbumList, 1.0f)
		.End()
		.SetCollapsible(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
	    .SetInsets(0)
	    .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 0.0f)
	        .SetInsets(12, 12, 12, 10)
	        .Add(fArtworkView, 0.0f)
	        .AddGroup(B_VERTICAL, 4, 0.0f)
	            .Add(fNameView, 0.0f)
	            .AddGroup(B_HORIZONTAL, 0, 0.0f)
	                .AddStrut(followerIndent)
	                .Add(fFollowersView, 0.0f)
	                .AddGlue()
	            .End()
	            .AddGlue()
	            .Add(fFollowButton, 0.0f)
	        .End()
	        .AddGlue()
	    .End()
	    .Add(contentSplit, 1.0f)
	.End();

	SetSizeLimits(495, 100000, 430, 100000);
	_LoadData();
}


ArtistWindow::~ArtistWindow()
{
	delete fTopTracksRetryRunner;
}


void ArtistWindow::_LoadData()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api) return;

	BMessenger self(this);
	api->Library().CheckFollowingArtist(fArtistId,
		[self](bool ok, const nlohmann::json& data) {
			if (!ok || !data.is_array() || data.empty()) return;
			BMessage state('afst');
			state.AddBool("following", data[0].get<bool>());
			self.SendMessage(&state);
		});

	api->Artists().GetArtist(fArtistId, [self](bool ok,
			const nlohmann::json& data) {
		if (!ok) return;
		BMessage msg(kMsgArtistMetadata);
		if (BuildArtistMetadataMessage(data, msg))
			self.SendMessage(&msg);
	});

	_LoadTopTracks(true);

	api->Artists().GetArtistAlbums(fArtistId, 20,
	    [self](bool ok, const nlohmann::json& data) {
		if (!ok) return;
		BMessage msg(kMsgArtistAlbums);
		if (BuildArtistAlbumsMessage(data, msg))
			self.SendMessage(&msg);
	});
}

void ArtistWindow::_LoadTopTracks(bool resetRetryCount)
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api) return;

	if (resetRetryCount)
		fTopTracksRetryCount = 0;
	delete fTopTracksRetryRunner;
	fTopTracksRetryRunner = nullptr;

	int32 generation = ++fTopTracksGeneration;
	BMessenger self(this);
	api->Artists().GetArtistTopTracks(fArtistId,
	    [self, generation](bool ok, const ArtistTopTracksResult& result,
			const nlohmann::json&) {
		BMessage msg(kMsgArtistTopTracks);
		msg.AddBool("ok", ok);
		msg.AddInt32("generation", generation);
		msg.AddInt32("source", result.source
			== ArtistTopTracksSource::kSearchFallback
			? kArtistTopTracksSearchFallback : kArtistTopTracksOfficial);
		if (!ok || !result.tracks.is_array()) {
			self.SendMessage(&msg);
			return;
		}
		int i = 1;
		for (const auto& t : result.tracks) {
			if (!t.is_object()) continue;
			if (i > 10) break;
			msg.AddString("uri",   t.value("uri", "").c_str());
			msg.AddString("title", t.value("name", "").c_str());
			msg.AddInt32("num",    (int32)i++);
			msg.AddInt32("ms",     (int32)t.value("duration_ms", 0));
			std::string album, albumUri;
			if (t.contains("album") && t["album"].is_object()) {
				album    = t["album"].value("name", "");
				albumUri = t["album"].value("uri",  "");
			}
			msg.AddString("album",    album.c_str());
			msg.AddString("albumUri", albumUri.c_str());
		}
		self.SendMessage(&msg);
	});
}


void ArtistWindow::_ScheduleTopTracksRetry(int32 generation)
{
	if (generation != fTopTracksGeneration
			|| fTopTracksRetryCount >= kMaxTopTracksRetries)
		return;

	fTopTracksRetryCount++;
	delete fTopTracksRetryRunner;
	BMessage retry(kMsgRetryTopTracks);
	retry.AddInt32("generation", generation);
	fTopTracksRetryRunner = new BMessageRunner(BMessenger(this), &retry,
		(bigtime_t)750000 * fTopTracksRetryCount, 1);
}

void ArtistWindow::_LoadArtwork(const std::string& url)
{
	if (fArtworkView)
		fArtworkView->LoadUrl(url);
}

void ArtistWindow::_SetPlayingTrack(const char* uri)
{
	fCurrentPlayingTrackUri = uri ? uri : "";
	if (!fTrackList) return;
	bool anyChanged = false;
	for (int32 i = 0; i < fTrackList->CountRows(); i++) {
		TrackArtistRow* row = (TrackArtistRow*)fTrackList->RowAt(i);
		if (!row) continue;
		bool changed = false;
		bool isPlaying = (row->fTrackUri == fCurrentPlayingTrackUri);
		for (int32 column = 1; column <= 3; column++) {
			TrackArtistStringField* f =
			    dynamic_cast<TrackArtistStringField*>(row->GetField(column));
			if (f && f->fIsPlaying != isPlaying) {
				f->fIsPlaying = isPlaying;
				changed = true;
			}
		}
		if (changed) {
			fTrackList->InvalidateRow(row);
			anyChanged = true;
		}
	}
	if (anyChanged) {
		UpdateIfNeeded();
		Flush();
	}
}

void ArtistWindow::ShowTrackContextMenu(BPoint local, BPoint screen)
{
	TrackArtistRow* row =
	    dynamic_cast<TrackArtistRow*>(fTrackList->RowAt(local));
	if (!row)
		row = dynamic_cast<TrackArtistRow*>(fTrackList->CurrentSelection());
	if (!row || row->fTrackUri.empty()) return;

	fTrackList->AddToSelection(row);

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	std::string ctx = SpotifyUriForItemKind(kSpotifyItemArtist, fArtistId);
	std::string trackUri = row->fTrackUri;
	BMessenger self(this);
	auto showMenu = [trackUri, ctx, screen, self, api]() {
		::ShowTrackContextMenu(trackUri, ctx, screen, self, api);
	};
	if (api && api->Playlists().GetCachedPlaylists().empty()) {
		api->Playlists().GetPlaylists([showMenu](bool, const nlohmann::json&) {
			showMenu();
		});
	} else {
		showMenu();
	}
}


void ArtistWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case MSG_LIBRARY_CHANGED:
	{
		std::string uri = message->GetString("uri", "");
		std::string operation = message->GetString("operation", "");
		if (uri != SpotifyUriForItemKind(kSpotifyItemArtist, fArtistId)
				|| (operation != "add" && operation != "remove")) {
			break;
		}
		fFollowing = operation == "add";
		if (fFollowButton) {
			fFollowButton->SetLabel(fFollowing
				? B_TRANSLATE("Unfollow") : B_TRANSLATE("Follow"));
		}
		break;
	}

	case 'lddt':
		_LoadData();
		break;

	case 'afst':
		fFollowing = message->GetBool("following", false);
		if (fFollowButton)
			fFollowButton->SetLabel(fFollowing
				? B_TRANSLATE("Unfollow") : B_TRANSLATE("Follow"));
		break;

	case 'afol':
	{
		App* app = dynamic_cast<App*>(be_app);
		SpotifyApi* api = app ? app->GetApi() : nullptr;
		if (!api) break;
		bool target = !fFollowing;
		BMessenger self(this);
		std::string artistUri = SpotifyUriForItemKind(kSpotifyItemArtist,
			fArtistId);
		auto done = [self, target, artistUri](bool ok, const nlohmann::json&) {
			if (!ok) return;
			BMessage state('afst');
			state.AddBool("following", target);
			self.SendMessage(&state);
			BMessage changed(MSG_LIBRARY_CHANGED);
			changed.AddString("operation", target ? "add" : "remove");
			changed.AddString("uri", artistUri.c_str());
			be_app->PostMessage(&changed);
		};
		if (target)
			api->Library().FollowArtist(fArtistId, done);
		else
			api->Library().UnfollowArtist(fArtistId, done);
		break;
	}

	case kMsgArtistMetadata:
	{
		const char* name   = message->GetString("name", "");
		int32 followers    = message->GetInt32("followers", 0);
		const char* imgUrl = message->GetString("imageUrl", "");
		if (fNameView) { fNameView->SetText(name); SetTitle(name); }
		if (fFollowersView)
			fFollowersView->SetText(FormatFollowers(followers).c_str());
		_LoadArtwork(imgUrl);
		break;
	}

	case kMsgArtistTopTracks:
	{
		if (!fTrackList) break;
		int32 generation = message->GetInt32("generation", 0);
		if (generation != fTopTracksGeneration)
			break;
		if (!message->GetBool("ok", false)) {
			_ScheduleTopTracksRetry(generation);
			break;
		}
		if (fTracksLabel) {
			fTracksLabel->SetText(ArtistTopTracksLabel(
				message->GetInt32("source", kArtistTopTracksOfficial)));
		}
		fTrackList->Clear();
		const char *uri, *title, *album, *albumUri;
		int32 num, ms;
		int32 rowsAdded = 0;
		for (int i = 0; i < 10 &&
		     message->FindString("uri",      i, &uri)      == B_OK &&
		     message->FindString("title",    i, &title)    == B_OK &&
		     message->FindInt32 ("num",      i, &num)      == B_OK &&
		     message->FindInt32 ("ms",       i, &ms)       == B_OK &&
		     message->FindString("album",    i, &album)    == B_OK &&
		     message->FindString("albumUri", i, &albumUri) == B_OK;
		     i++)
		{
			char numBuf[8];
			snprintf(numBuf, sizeof(numBuf), "%d", (int)num);
			TrackArtistRow* row = new TrackArtistRow(uri);
			row->fAlbumUri = albumUri;
			row->SetField(new BStringField(numBuf),               0);
			row->SetField(new TrackArtistStringField(title),       1);
			row->SetField(new TrackArtistStringField(album),       2);
			row->SetField(new TrackArtistStringField(FormatMs(ms).c_str()), 3);
			fTrackList->AddRow(row);
			rowsAdded++;
		}
		if (!fCurrentPlayingTrackUri.empty())
			_SetPlayingTrack(fCurrentPlayingTrackUri.c_str());
		if (rowsAdded == 0)
			_ScheduleTopTracksRetry(generation);
		break;
	}

	case kMsgArtistAlbums:
	{
		if (!fAlbumList) break;
		fAlbumList->Clear();
		const char *id, *name, *year, *type, *cover;
		for (int i = 0;
		     message->FindString("id",    i, &id)    == B_OK &&
		     message->FindString("name",  i, &name)  == B_OK &&
		     message->FindString("year",  i, &year)  == B_OK &&
		     message->FindString("type",  i, &type)  == B_OK &&
		     message->FindString("cover", i, &cover) == B_OK;
		     i++)
		{
			AlbumArtistRow* row = new AlbumArtistRow(id, name, cover);
			row->SetField(new BStringField(name), 0);
			row->SetField(new BStringField(year), 1);
			const char* typeLabel =
			    (strcmp(type, "album")  == 0) ? "Album"  :
			    (strcmp(type, "single") == 0) ? "Single" : type;
			row->SetField(new BStringField(typeLabel), 2);
			fAlbumList->AddRow(row);
		}
		break;
	}

	case kMsgRetryTopTracks:
	{
		if (message->GetInt32("generation", 0) == fTopTracksGeneration)
			_LoadTopTracks(false);
		break;
	}

	case 'tply':
	{
		const char* msgUri = message->GetString("trackUri", "");
		std::string uri = *msgUri ? msgUri : "";
		TrackArtistRow* row = nullptr;
		if (uri.empty() && fTrackList) {
			row = dynamic_cast<TrackArtistRow*>(fTrackList->CurrentSelection());
			if (row) uri = row->fTrackUri;
		}
		if (!row && fTrackList) {
			for (int32 i = 0; i < fTrackList->CountRows(); i++) {
				TrackArtistRow* candidate =
					dynamic_cast<TrackArtistRow*>(fTrackList->RowAt(i));
				if (candidate && candidate->fTrackUri == uri) {
					row = candidate;
					break;
				}
			}
		}
		if (!uri.empty()) {
			BMessage play('play');
			play.AddString("uri", uri.c_str());
			std::string contextUri = SpotifyUriForItemKind(
				kSpotifyItemArtist, fArtistId);
			play.AddString("context_uri", contextUri.c_str());
			if (row) {
				BStringField* title = dynamic_cast<BStringField*>(row->GetField(1));
				BStringField* album = dynamic_cast<BStringField*>(row->GetField(2));
				if (title)
					play.AddString("title", title->String());
				if (fNameView)
					play.AddString("artist", fNameView->Text());
				if (album)
					play.AddString("album", album->String());
			}
			be_app->PostMessage(&play);
			_SetPlayingTrack(uri.c_str());
		}
		break;
	}

	case 'aopn':
	{
		if (!fAlbumList) break;
		AlbumArtistRow* row =
		    dynamic_cast<AlbumArtistRow*>(fAlbumList->CurrentSelection());
		if (!row || row->fAlbumId.empty()) break;
		std::string albumUri = SpotifyUriForItemKind(kSpotifyItemAlbum,
			row->fAlbumId);
		if (albumUri.empty())
			break;
		BMessage msg(MSG_OPEN_PLAYLIST);
		msg.AddString("uri",      albumUri.c_str());
		msg.AddString("name",     row->fAlbumName.c_str());
		msg.AddString("coverUrl", row->fCoverUrl.c_str());
		be_app->PostMessage(&msg);
		break;
	}

	case 'likT':
	{
		const char* trackUri;
		if (message->FindString("trackUri", &trackUri) == B_OK) {
			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();
			if (api) {
				std::string u = trackUri;
				std::string trackId = SpotifyItemIdForUri(u);
				if (SpotifyItemKindForUri(u) == kSpotifyItemTrack
						&& !trackId.empty())
					api->Library().SaveTrack(trackId, nullptr);
			}
		}
		break;
	}

	case 'addP':
	{
		const char* trackUri;
		const char* playlistId;
		if (message->FindString("trackUri",   &trackUri)   == B_OK &&
		    message->FindString("playlistId", &playlistId) == B_OK) {
			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();
			if (api)
				api->Playlists().AddTrackToPlaylist(playlistId, trackUri,
					nullptr);
		}
		break;
	}

	case 'remL':
	{
		const char* trackUri;
		if (message->FindString("trackUri", &trackUri) == B_OK) {
			App* app = (App*)be_app;
			SpotifyApi* api = app->GetApi();
			if (api) {
				std::string uri = trackUri;
				if (SpotifyItemKindForUri(uri) == kSpotifyItemTrack) {
					api->Library().RemoveLibraryItems({uri}, [uri](bool ok,
							const nlohmann::json&) {
						if (!ok)
							return;
						BMessage changed(MSG_LIBRARY_CHANGED);
						changed.AddString("operation", "remove");
						changed.AddString("uri", uri.c_str());
						be_app->PostMessage(&changed);
					});
				}
			}
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

	case 'savA':
	{
		const char* uri = message->GetString("uri", "");
		std::string albumUri = uri ? uri : "";
		std::string albumId = SpotifyItemIdForUri(albumUri);
		if (SpotifyItemKindForUri(albumUri) != kSpotifyItemAlbum
				|| albumId.empty())
			break;
		App* app = (App*)be_app;
		SpotifyApi* api = app->GetApi();
		if (api) {
			api->Library().SaveAlbum(albumId, [albumUri](bool ok,
					const nlohmann::json&) {
				if (!ok) return;
				BMessage changed(MSG_LIBRARY_CHANGED);
				changed.AddString("operation", "add");
				changed.AddString("uri", albumUri.c_str());
				be_app->PostMessage(&changed);
			});
		}
		break;
	}

	case 'pStU':
	{
		_SetPlayingTrack(message->GetString("trackUri", ""));
		break;
	}

	default:
		BWindow::MessageReceived(message);
		break;
	}
}
