#include "QueueWindow.h"
#include "TrackContextMenu.h"
#include "App.h"
#include "Messages.h"
#include "SettingsController.h"
#include "DiscoverListView.h"
#include "spotify/api/SpotifyApi.h"
#include <nlohmann/json.hpp>

#include <Application.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <ScrollBar.h>
#include <TabView.h>
#include <Catalog.h>
#include <cstring>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "QueueWindow"

static const uint32 kMsgTabSelected = 'tabS';



class QueueRow : public BRow {
public:
	std::string fUri;
	bool        fIsPlaying = false;

	QueueRow(const std::string& uri) : BRow(), fUri(uri) {}
};



class QueueTitleField : public BStringField {
public:
	bool fIsPlaying;
	QueueTitleField(const char* s) : BStringField(s), fIsPlaying(false) {}
};

class QueueTitleColumn : public BStringColumn {
public:
	QueueTitleColumn(const char* title, float w, float min, float max)
		: BStringColumn(title, w, min, max, B_TRUNCATE_END) {}

	void DrawField(BField* field, BRect rect, BView* parent) override {
		QueueTitleField* f = (QueueTitleField*)field;
		BFont font;
		parent->GetFont(&font);
		if (f->fIsPlaying) {
			BFont bold(be_bold_font);
			bold.SetSize(font.Size());
			parent->SetFont(&bold);
		}
		BStringColumn::DrawField(field, rect, parent);
		parent->SetFont(&font);
	}
};



class QueueListView : public BColumnListView {
public:
	QueueListView()
		: BColumnListView("QueueList", 0, B_NO_BORDER, false) {}

	class RightClickFilter : public BMessageFilter {
	public:
		RightClickFilter(QueueListView* owner)
			: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
			  fOwner(owner) {}
		filter_result Filter(BMessage* msg, BHandler** target) override {
			if (!fOwner || !msg || msg->what != B_MOUSE_DOWN)
				return B_DISPATCH_MESSAGE;
			int32 buttons = 0;
			if (msg->FindInt32("buttons", &buttons) != B_OK
					|| (buttons & (B_SECONDARY_MOUSE_BUTTON
						| B_TERTIARY_MOUSE_BUTTON)) == 0)
				return B_DISPATCH_MESSAGE;
			BView* view = dynamic_cast<BView*>(*target);
			if (!view || dynamic_cast<BScrollBar*>(view))
				return B_DISPATCH_MESSAGE;
			if (view->Name() && strcmp(view->Name(), "header") == 0)
				return B_DISPATCH_MESSAGE;
			bool inside = false;
			for (BView* p = view; p; p = p->Parent()) {
				if (p == fOwner || p == fOwner->ScrollView()) {
					inside = true;
					break;
				}
			}
			if (!inside)
				return B_DISPATCH_MESSAGE;
			BPoint screen;
			if (msg->FindPoint("screen_where", &screen) != B_OK) {
				BPoint where;
				if (msg->FindPoint("where", &where) != B_OK)
					return B_DISPATCH_MESSAGE;
				screen = view->ConvertToScreen(where);
			}
			BMessage show('rCf!');
			show.AddPoint("screenPt", screen);
			if (fOwner->Looper())
				fOwner->Looper()->PostMessage(&show, fOwner);
			return B_SKIP_MESSAGE;
		}
	private:
		QueueListView* fOwner;
	};

	virtual void AttachedToWindow() {
		BColumnListView::AttachedToWindow();
		if (BView* outline = ScrollView())
			outline->AddFilter(new RightClickFilter(this));
		else
			AddFilter(new RightClickFilter(this));
	}

	virtual void MouseDown(BPoint where) {
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
		if (!contextClick) {
			BColumnListView::MouseDown(where);
			return;
		}

		QueueRow* row = dynamic_cast<QueueRow*>(RowAt(where));
		if (!row) row = dynamic_cast<QueueRow*>(CurrentSelection());
		if (!row || row->fUri.empty()) return;
		AddToSelection(row);

		BPoint screen = where;
		ConvertToScreen(&screen);

		App* app = (App*)be_app;
		SpotifyApi* api = app->GetApi();
		ShowTrackContextMenu(row->fUri, "", screen, BMessenger(Window()), api);
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
				QueueRow* row = dynamic_cast<QueueRow*>(RowAt(where));
				if (!row) row = dynamic_cast<QueueRow*>(CurrentSelection());
				if (row && !row->fUri.empty()) {
					AddToSelection(row);
					App* app = (App*)be_app;
					SpotifyApi* api = app->GetApi();
					ShowTrackContextMenu(row->fUri, "", screen,
						BMessenger(Window()), api);
				}
			}
			return;
		}
		BColumnListView::MessageReceived(msg);
	}

	virtual void ItemInvoked() {
		QueueRow* row = dynamic_cast<QueueRow*>(CurrentSelection());
		if (!row || row->fUri.empty()) return;
		BMessage msg('tply');
		msg.AddString("trackUri", row->fUri.c_str());
		Window()->PostMessage(&msg);
	}

	virtual bool InitiateDrag(BPoint point, bool) {
		QueueRow* row = dynamic_cast<QueueRow*>(CurrentSelection());
		if (!row)
			row = dynamic_cast<QueueRow*>(RowAt(point));
		if (!row || row->fUri.empty())
			return false;

		BMessage drag('drag');
		drag.AddString("uri", row->fUri.c_str());
		drag.AddString("itemType",
			row->fUri.find("spotify:episode:") == 0 ? "episode" : "track");
		drag.AddString("trackUri", row->fUri.c_str());
		BStringField* title = dynamic_cast<BStringField*>(row->GetField(0));
		BStringField* artist = dynamic_cast<BStringField*>(row->GetField(1));
		BStringField* duration = dynamic_cast<BStringField*>(row->GetField(2));
		if (title)
			drag.AddString("title", title->String());
		if (artist)
			drag.AddString("artist", artist->String());
		if (duration)
			drag.AddString("duration", duration->String());
		BRect dragRect(point.x - 100, point.y - 10, point.x + 100, point.y + 10);
		DragMessage(&drag, dragRect, this);
		return true;
	}
};



class NotifyTabView : public BTabView {
public:
	NotifyTabView() : BTabView("tabs", B_WIDTH_FROM_LABEL) {}

	void Select(int32 tab) override {
		BTabView::Select(tab);
		if (Window()) {
			BMessage msg(kMsgTabSelected);
			msg.AddInt32("tab", tab);
			Window()->PostMessage(&msg);
		}
	}
};



QueueWindow::QueueWindow()
	: BWindow(BRect(300, 150,
		300 + kDefaultQueueWindowWidth,
		150 + kDefaultQueueWindowHeight), B_TRANSLATE("Queue"),
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	HaifySettings s = SettingsController::Load();
	if (s.queueWindowW > 0) {
		MoveTo(s.queueWindowX, s.queueWindowY);
		ResizeTo(s.queueWindowW, s.queueWindowH);
	}
	_InitLayout();
	_LoadQueue();
}


bool
QueueWindow::QuitRequested()
{
	App* app = dynamic_cast<App*>(be_app);
	if (!(app && app->IsQuitting())) {
		BRect f = Frame();
		SettingsController::Update([&](HaifySettings& s) {
			s.queueWindowOpen = false;
			s.queueWindowX = f.left;  s.queueWindowY = f.top;
			s.queueWindowW = f.Width(); s.queueWindowH = f.Height();
		});
	}
	return true;
}


void
QueueWindow::_InitLayout()
{
	fMenuBar = new BMenuBar("MenuBar");
	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Refresh"),
		new BMessage('qRfr'), 'R'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Close Window"),
		new BMessage(B_QUIT_REQUESTED), 'W'));
	fMenuBar->AddItem(fileMenu);

	fList = new QueueListView();
	fList->SetSortingEnabled(false);
	fList->AddColumn(new QueueTitleColumn(B_TRANSLATE("Title"),    200, 80, 9999), 0);
	fList->AddColumn(new BStringColumn(B_TRANSLATE("Artist"),      130, 60, 9999,
		B_TRUNCATE_END), 1);
	fList->AddColumn(new BStringColumn(B_TRANSLATE("Duration"),     55, 40, 80,
		B_TRUNCATE_END, B_ALIGN_RIGHT), 2);

	BView* queueTab = new BView("QueueTab", 0);
	BLayoutBuilder::Group<>(queueTab, B_VERTICAL, 0)
		.Add(fList, 1)
	.End();

	fRecentList = new DiscoverListView("RecentList",
		std::vector<ColDef>{
			{ B_TRANSLATE("Track"),  200, kColPlayOnDouble },
			{ B_TRANSLATE("Artist"), 130, kColOpenOnDouble },
			{ B_TRANSLATE("Album"),  100, kColOpenOnDouble },
		});
	fRecentList->SetSortingEnabled(false);

	fTabView = new NotifyTabView();
	fTabView->AddTab(queueTab);
	fTabView->TabAt(0)->SetLabel(B_TRANSLATE("Queue"));
	fTabView->AddTab(fRecentList);
	fTabView->TabAt(1)->SetLabel(B_TRANSLATE("Recently Played"));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.Add(fTabView, 1)
	.End();

	SetSizeLimits(250, 100000, 200, 100000);
}


void
QueueWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTabSelected:
		{
			int32 tab = 0;
			message->FindInt32("tab", &tab);
			if (tab == 1 && !fRecentLoaded)
				_LoadRecent();
			break;
		}

		case 'qRfr':
		case 'lddt':
			_LoadQueue();
			if (fRecentLoaded) {
				fRecentLoaded = false;
				if (fTabView && fTabView->Selection() == 1)
					_LoadRecent();
			}
			break;

		case 'pStU':
		{
			const char* uri;
			if (message->FindString("trackUri", &uri) == B_OK)
				SetPlayingTrack(uri);
			break;
		}

		case 'qItm':
		{
			int32 incomingCount = 0;
			const char* incomingUri = nullptr;
			while (message->FindString("uri", incomingCount,
					&incomingUri) == B_OK)
				incomingCount++;
			bool sameSequence = incomingCount == fList->CountRows();
			for (int32 i = 0; sameSequence && i < incomingCount; i++) {
				QueueRow* row = dynamic_cast<QueueRow*>(fList->RowAt(i));
				const char* uri = message->FindString("uri", i);
				sameSequence = row && uri && row->fUri == uri;
			}
			if (sameSequence) {
				for (int32 i = 0; i < incomingCount; i++) {
					QueueRow* row = dynamic_cast<QueueRow*>(fList->RowAt(i));
					const char* title = message->FindString("title", i);
					const char* artist = message->FindString("artist", i);
					const char* duration = message->FindString("duration", i);
					bool playing = false;
					message->FindBool("playing", i, &playing);
					row->fIsPlaying = playing;
					if (QueueTitleField* field = dynamic_cast<QueueTitleField*>(
							row->GetField(0))) {
						field->SetString(title ? title : "");
						field->fIsPlaying = playing;
					}
					if (BStringField* field = dynamic_cast<BStringField*>(
							row->GetField(1)))
						field->SetString(artist ? artist : "");
					if (BStringField* field = dynamic_cast<BStringField*>(
							row->GetField(2)))
						field->SetString(duration ? duration : "");
					fList->UpdateRow(row);
				}
				if (!fCurrentUri.empty())
					SetPlayingTrack(fCurrentUri.c_str());
				break;
			}

			fList->Clear();

			const char* s;
			for (int32 i = 0; message->FindString("uri", i, &s) == B_OK; i++) {
				const char* title    = message->FindString("title",    i);
				const char* artist   = message->FindString("artist",   i);
				const char* duration = message->FindString("duration", i);
				bool        playing  = false;
				message->FindBool("playing", i, &playing);

				QueueRow* row = new QueueRow(s);
				row->fIsPlaying = playing;
				QueueTitleField* tf = new QueueTitleField(title ? title : "");
				tf->fIsPlaying = playing;
				row->SetField(tf,                                               0);
				row->SetField(new BStringField(artist   ? artist   : ""),       1);
				row->SetField(new BStringField(duration ? duration : ""),       2);
				fList->AddRow(row);
			}
			if (!fCurrentUri.empty())
				SetPlayingTrack(fCurrentUri.c_str());
			break;
		}

		case 'rRow':
		{

			fRecentList->Clear();
			const char* s;
			for (int32 i = 0; message->FindString("tUri", i, &s) == B_OK; i++) {
				const char* tName  = message->FindString("tName",  i);
				const char* aName  = message->FindString("aName",  i);
				const char* aUri   = message->FindString("aUri",   i);
				const char* lName  = message->FindString("lName",  i);
				const char* lUri   = message->FindString("lUri",   i);
				fRecentList->AddRow(new DiscoverRow(
					{ tName  ? tName  : "",
					  aName  ? aName  : "",
					  lName  ? lName  : "" },
					{ s,
					  aUri   ? aUri   : "",
					  lUri   ? lUri   : "" },
					{ tName  ? tName  : "",
					  aName  ? aName  : "",
					  lName  ? lName  : "" }
				));
			}
			if (!fCurrentUri.empty())
				((DiscoverListView*)fRecentList)->SetPlayingUri(fCurrentUri);
			break;
		}

		case 'tply':
		{
			const char* uri = message->GetString("trackUri", "");
			if (*uri) {
				BMessage play('play');
				play.AddString("uri", uri);
				be_app->PostMessage(&play);
			}
			break;
		}

		case 'play':
		{
			const char* uri = nullptr;
			if (message->FindString("uri", &uri) == B_OK && uri)
				be_app->PostMessage(message);
			break;
		}

		case 'open':
			be_app->PostMessage(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
QueueWindow::SetPlayingTrack(const char* uri)
{
	fCurrentUri = uri ? uri : "";
	bool anyChanged = false;
	for (int32 i = 0; i < fList->CountRows(); i++) {
		QueueRow* row = (QueueRow*)fList->RowAt(i);
		if (!row) continue;
		bool playing = (row->fUri == fCurrentUri);
		if (row->fIsPlaying != playing) {
			row->fIsPlaying = playing;
			QueueTitleField* f = (QueueTitleField*)row->GetField(0);
			if (f) f->fIsPlaying = playing;
			fList->InvalidateRow(row);
			anyChanged = true;
		}
	}

	if (fRecentList)
		((DiscoverListView*)fRecentList)->SetPlayingUri(fCurrentUri);
	if (anyChanged) {
		UpdateIfNeeded();
		Flush();
	}
}


void
QueueWindow::_LoadQueue()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api) return;

	BMessenger self(this);
	std::string currentUri = fCurrentUri;

	api->GetQueue([self, currentUri](bool ok, const nlohmann::json& data) {
		if (!ok) return;

		auto parseMs = [](int ms) -> std::string {
			int s = ms / 1000;
			char buf[16];
			snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
			return buf;
		};

		auto addItem = [&](BMessage* msg, const nlohmann::json& item, bool playing) {
			std::string type  = item.value("type", "");
			std::string uri   = item.value("uri",  "");
			std::string title = item.value("name", "");
			std::string artist;
			if (type == "episode") {
				if (item.contains("show") && item["show"].is_object())
					artist = item["show"].value("name", "");
			} else {
				if (item.contains("artists") && item["artists"].is_array()
						&& !item["artists"].empty())
					artist = item["artists"][0].value("name", "");
			}
			std::string dur = parseMs(item.value("duration_ms", 0));

			msg->AddString("uri",      uri.c_str());
			msg->AddString("title",    title.c_str());
			msg->AddString("artist",   artist.c_str());
			msg->AddString("duration", dur.c_str());
			msg->AddBool("playing",    playing);
		};

		BMessage* msg = new BMessage('qItm');

		if (data.contains("currently_playing")
				&& data["currently_playing"].is_object()) {
			addItem(msg, data["currently_playing"], true);
		}
		if (data.contains("queue") && data["queue"].is_array()) {
			for (const auto& item : data["queue"])
				if (item.is_object())
					addItem(msg, item, false);
		}

		self.SendMessage(msg);
		delete msg;
	});
}


void
QueueWindow::_LoadRecent()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api) return;

	fRecentLoaded = true;
	BMessenger self(this);

	api->GetRecentlyPlayed(50, [self](bool ok, const nlohmann::json& data) {
		if (!ok || !data.contains("items")) return;

		BMessage* msg = new BMessage('rRow');
		std::string prevUri;

		for (const auto& item : data["items"]) {
			if (!item.contains("track") || !item["track"].is_object()) continue;
			const auto& t = item["track"];
			std::string tUri = t.value("uri", "");
			if (tUri == prevUri) continue;
			prevUri = tUri;

			std::string tName = t.value("name", "");
			std::string aName, aUri, lName, lUri;
			if (t.contains("artists") && t["artists"].is_array()
					&& !t["artists"].empty()) {
				aName = t["artists"][0].value("name", "");
				aUri  = t["artists"][0].value("uri",  "");
			}
			if (t.contains("album") && t["album"].is_object()) {
				lName = t["album"].value("name", "");
				lUri  = t["album"].value("uri",  "");
			}

			msg->AddString("tUri",  tUri.c_str());
			msg->AddString("tName", tName.c_str());
			msg->AddString("aName", aName.c_str());
			msg->AddString("aUri",  aUri.c_str());
			msg->AddString("lName", lName.c_str());
			msg->AddString("lUri",  lUri.c_str());
		}

		self.SendMessage(msg);
		delete msg;
	});
}
