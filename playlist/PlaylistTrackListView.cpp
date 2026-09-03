#include "PlaylistTrackListView.h"

#include "HaifyDebug.h"
#include "Messages.h"
#include "PlaylistTrackRow.h"
#include "PlaylistWindow.h"
#include "spotify/SpotifyUri.h"

#include <Application.h>
#include <ColumnTypes.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Rect.h>
#include <ScrollBar.h>
#include <View.h>
#include <Window.h>

#include <cstring>
#include <string>

static const uint32 kMsgCheckLazyLoad = 'ckLm';

namespace {

bool
IsSecondaryTrackMouseClick(BMessage* message)
{
	int32 buttons = 0;
	return message && message->FindInt32("buttons", &buttons) == B_OK
		&& (buttons & (B_SECONDARY_MOUSE_BUTTON
			| B_TERTIARY_MOUSE_BUTTON)) != 0;
}


bool
IsTrackListContentView(BView* view)
{
	if (!view || dynamic_cast<BScrollBar*>(view))
		return false;
	return !view->Name() || strcmp(view->Name(), "header") != 0;
}


bool
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

}


class TrackListView::RightClickFilter : public BMessageFilter {
public:
	RightClickFilter(TrackListView* owner)
		:
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
		fOwner(owner)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		if (!fOwner || !message || message->what != B_MOUSE_DOWN)
			return B_DISPATCH_MESSAGE;
		if (!IsSecondaryTrackMouseClick(message))
			return B_DISPATCH_MESSAGE;
		BView* view = dynamic_cast<BView*>(*target);
		if (!IsTrackListContentView(view))
			return B_DISPATCH_MESSAGE;
		if (!_IsInsideOwner(view))
			return B_DISPATCH_MESSAGE;
		BPoint screen;
		if (!FindTrackScreenPoint(message, view, screen))
			return B_DISPATCH_MESSAGE;
		BMessage show('rCf!');
		show.AddPoint("screenPt", screen);
		if (fOwner->Looper())
			fOwner->Looper()->PostMessage(&show, fOwner);
		return B_SKIP_MESSAGE;
	}

private:
	bool _IsInsideOwner(BView* view) const
	{
		for (BView* parent = view; parent; parent = parent->Parent()) {
			if (parent == fOwner || parent == fOwner->ScrollView())
				return true;
		}
		return false;
	}

	TrackListView* fOwner;
};


TrackListView::TrackListView(const char* name, uint32 flags,
	border_style border, bool showHorizontalScrollbar)
	:
	BColumnListView(name, flags, border, showHorizontalScrollbar)
{
}


void
TrackListView::AttachedToWindow()
{
	BColumnListView::AttachedToWindow();
	if (BView* outline = ScrollView())
		outline->AddFilter(new RightClickFilter(this));
	else
		AddFilter(new RightClickFilter(this));
}


void
TrackListView::MouseDown(BPoint point)
{
	BMessage* message = Window()->CurrentMessage();
	int32 buttons = 0;
	if (message)
		message->FindInt32("buttons", &buttons);
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


void
TrackListView::MessageReceived(BMessage* message)
{
	if (message->what == 'rCf!') {
		BPoint screen;
		if (message->FindPoint("screenPt", &screen) == B_OK) {
			BPoint where = screen;
			if (BView* outline = ScrollView())
				outline->ConvertFromScreen(&where);
			else
				ConvertFromScreen(&where);
			((PlaylistWindow*)Window())->ShowContextMenu(this, where, screen);
		}
		return;
	}
	if (message->WasDropped()) {
		DEBUG_PRINT("TrackListView received dropped message (what=%.4s)\n",
			(char*)&message->what);
	}
	if (message->WasDropped() && message->what == 'drag') {
		DEBUG_PRINT("TrackListView: Posting 'drag' drop to Window\n");
		Window()->PostMessage(message);
		return;
	}
	BColumnListView::MessageReceived(message);
}


void
TrackListView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1 && bytes[0] == B_DELETE) {
		BRow* baseRow = CurrentSelection();
		DEBUG_PRINT("TrackListView: DEL pressed. baseRow=%p\n", baseRow);
		if (baseRow) {
			TrackRow* row = (TrackRow*)baseRow;
			DEBUG_PRINT("TrackListView: deleting track %s\n",
				row->fTrackUri.c_str());
			if (!row->fTrackUri.empty()) {
				BMessage message('remT');
				message.AddString("trackUri", row->fTrackUri.c_str());
				Window()->PostMessage(&message);
			}
		}
		return;
	}
	BColumnListView::KeyDown(bytes, numBytes);
}


bool
TrackListView::InitiateDrag(BPoint point, bool wasSelected)
{
	BRow* baseRow = CurrentSelection();
	if (!baseRow) {
		DEBUG_PRINT("InitiateDrag: No selection, using RowAt(pt)\n");
		baseRow = RowAt(point);
	}

	if (baseRow) {
		TrackRow* row = (TrackRow*)baseRow;
		DEBUG_PRINT("InitiateDrag: Initiating drag for track %s\n",
			row->fTrackUri.c_str());
		if (!row->fTrackUri.empty()) {
			BMessage dragMessage('drag');
			dragMessage.AddString("uri", row->fTrackUri.c_str());
			dragMessage.AddString("itemType",
				SpotifyItemTypeName(SpotifyItemKindForUri(row->fTrackUri)));
			dragMessage.AddString("trackUri", row->fTrackUri.c_str());
			if (PlaylistWindow* window =
					dynamic_cast<PlaylistWindow*>(Window())) {
				dragMessage.AddString("sourcePlaylist",
					window->GetUri().c_str());
				for (int32 i = 0; i < CountRows(); i++) {
					if (RowAt(i) == row) {
						dragMessage.AddInt32("sourceIndex", i);
						break;
					}
				}
			}
			auto getString = [&](int32 column) -> const char* {
				BStringField* field =
					dynamic_cast<BStringField*>(row->GetField(column));
				return field ? field->String() : "";
			};
			dragMessage.AddString("title", getString(1));
			dragMessage.AddString("artist", getString(2));
			dragMessage.AddString("album", getString(5));
			dragMessage.AddString("duration", getString(6));

			BRect dragRect(point.x - 100, point.y - 10, point.x + 100,
				point.y + 10);
			DragMessage(&dragMessage, dragRect, this);
			return true;
		}
	} else {
		DEBUG_PRINT("InitiateDrag: No row found for drag\n");
	}
	return false;
}


void
TrackListView::MouseMoved(BPoint point, uint32 transit,
	const BMessage* dragMessage)
{
	BColumnListView::MouseMoved(point, transit, dragMessage);
	if (Window())
		Window()->PostMessage(kMsgCheckLazyLoad);
}


void
TrackListView::SelectionChanged()
{
	BColumnListView::SelectionChanged();
	if (Window())
		Window()->PostMessage(kMsgCheckLazyLoad);
	TrackRow* row = (TrackRow*)CurrentSelection();
	if (!row || !Window())
		return;
	if (!row->fDescription.empty()) {
		BMessage message('epSl');
		message.AddString("description", row->fDescription.c_str());
		Window()->PostMessage(&message);
	}
}


void
TrackListView::ItemInvoked()
{
	TrackRow* row = (TrackRow*)CurrentSelection();
	if (!row || !Window())
		return;

	BPoint where;
	uint32 buttons;
	GetMouse(&where, &buttons, false);
	int32 column = _ColumnAt(where.x);

	if (column == 1 && !row->fTrackUri.empty()) {
		Window()->PostMessage(new BMessage(MSG_TRACK_INVOKED));
	} else if (column == 2 && !row->fArtistUri.empty()) {
		std::string id = SpotifyItemKindForUri(row->fArtistUri)
			== kSpotifyItemArtist
			? SpotifyItemIdForUri(row->fArtistUri) : row->fArtistUri;
		if (id.empty())
			return;
		BMessage message(MSG_SHOW_ARTIST);
		message.AddString("id", id.c_str());
		be_app->PostMessage(&message);
	} else if (column == 5 && !row->fAlbumUri.empty()) {
		std::string id = SpotifyItemKindForUri(row->fAlbumUri)
			== kSpotifyItemAlbum
			? SpotifyItemIdForUri(row->fAlbumUri) : row->fAlbumUri;
		if (id.empty())
			return;
		BMessage message(MSG_SHOW_ALBUM);
		message.AddString("id", id.c_str());
		be_app->PostMessage(&message);
	}
}


int32
TrackListView::_ColumnAt(float x) const
{
	float left = 0;
	for (int32 i = 0; i < CountColumns(); i++) {
		BColumn* column = ColumnAt(i);
		if (!column)
			break;
		left += column->Width();
		if (x < left)
			return i;
	}
	return CountColumns() - 1;
}


BScrollBar*
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
