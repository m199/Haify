#include "PlaylistTrackListView.h"

#include "ui/drag/DropMarkerStyle.h"
#include "ui/drag/HaifyDragState.h"
#include "app/HaifyDebug.h"
#include "messages/Messages.h"
#include "messages/MessageContracts.h"
#include "PlaylistTrackRow.h"
#include "ui/windows/PlaylistWindow.h"
#include "spotify/SpotifyUri.h"

#include <Application.h>
#include <ColumnTypes.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Rect.h>
#include <ScrollBar.h>
#include <View.h>
#include <Window.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

static const uint32 kMsgCheckLazyLoad = 'ckLm';
static const uint32 kMsgDropMarkerCleanup = 'dmCl';

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


bool
FindTrackMouseMovedScreenPoint(BMessage* message, BView* view, BPoint& screen)
{
	if (message->FindPoint("screen_where", &screen) == B_OK)
		return true;

	BPoint where;
	if (message->FindPoint("where", &where) != B_OK)
		return false;

	screen = view->ConvertToScreen(where);
	return true;
}


BPoint
TrackRowPointFromScreen(BColumnListView* list, BPoint screen)
{
	if (!list)
		return screen;
	BPoint point = screen;
	if (BView* outline = list->ScrollView())
		outline->ConvertFromScreen(&point);
	else
		list->ConvertFromScreen(&point);
	return point;
}

}


class TrackListView::MouseDownFilter : public BMessageFilter {
public:
	MouseDownFilter(TrackListView* owner)
		:
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
		fOwner(owner)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		if (!fOwner || !message || message->what != B_MOUSE_DOWN)
			return B_DISPATCH_MESSAGE;
		BView* view = dynamic_cast<BView*>(*target);
		if (!IsTrackListContentView(view))
			return B_DISPATCH_MESSAGE;
		if (!_IsInsideOwner(view))
			return B_DISPATCH_MESSAGE;
		fOwner->fPreserveGroupOnMouseDown = false;
		fOwner->fDeferredGroupClick = false;
		if (!IsSecondaryTrackMouseClick(message)) {
			fOwner->fMouseDownDrag = {};
			BPoint screen;
			// Preserve the pre-click group for dragging, including an Alt/Ctrl
			// click that would otherwise toggle the grabbed selected row off.
			if ((modifiers() & B_SHIFT_KEY) == 0
					&& FindTrackScreenPoint(message, view, screen))
				fOwner->_RememberDragSelection(TrackRowPointFromScreen(fOwner, screen));
			fOwner->fPreserveGroupOnMouseDown = !fOwner->fMouseDownDrag.sourceIndices.empty()
				&& (modifiers() & (B_CONTROL_KEY | B_SHIFT_KEY | B_OPTION_KEY | B_COMMAND_KEY)) == 0;
			return B_DISPATCH_MESSAGE;
		}
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

class TrackListView::MouseUpFilter : public BMessageFilter {
public:
	MouseUpFilter(TrackListView* owner)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_UP), fOwner(owner) {}

	filter_result Filter(BMessage*, BHandler**) override
	{
		// Apply an ordinary click before native MouseUp handles double-clicks.
		fOwner->_FinishDeferredClick();
		return B_DISPATCH_MESSAGE;
	}

private:
	TrackListView* fOwner;
};


class TrackMouseMovedFilter : public BMessageFilter {
public:
	TrackMouseMovedFilter(TrackListView* owner)
		:
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_MOVED),
		fOwner(owner)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		if (!fOwner || !message || message->what != B_MOUSE_MOVED)
			return B_DISPATCH_MESSAGE;
		BMessage drag;
		if (!GetHaifyActiveDragMessage(drag) || drag.what != MSG_DRAG_ITEM)
			return B_DISPATCH_MESSAGE;

		BView* view = dynamic_cast<BView*>(*target);
		if (!IsTrackListContentView(view))
			return B_DISPATCH_MESSAGE;
		for (BView* parent = view; parent; parent = parent->Parent()) {
			if (parent == fOwner || parent == fOwner->ScrollView()) {
				int32 buttons = 0;
				if (message->FindInt32("buttons", &buttons) == B_OK
						&& (buttons & B_PRIMARY_MOUSE_BUTTON) == 0) {
					fOwner->ClearDropMarker();
					ClearHaifyActiveDragMessage();
					if (be_app)
						be_app->PostMessage(MSG_HAIFY_DRAG_ENDED);
					return B_DISPATCH_MESSAGE;
				}
				BPoint screen;
				if (FindTrackMouseMovedScreenPoint(message, view, screen))
					fOwner->UpdateDropMarkerFromDrag(screen, &drag);
				break;
			}
		}
		return B_DISPATCH_MESSAGE;
	}

private:
	TrackListView* fOwner;
};


TrackListView::TrackListView(const char* name, uint32 flags,
	border_style border, bool showHorizontalScrollbar)
	:
	BColumnListView(name, flags, border, showHorizontalScrollbar),
	fDropMarker(this, [](BRow* row, int32 position, bool target) {
		TrackRow* trackRow = dynamic_cast<TrackRow*>(row);
		(void)target;
		return trackRow && trackRow->SetDropMarkerPosition(position);
	})
{
}


TrackListView::~TrackListView()
{
	_StopDropMarkerCleanupRunner();
}


void
TrackListView::AttachedToWindow()
{
	BColumnListView::AttachedToWindow();
	if (BView* outline = ScrollView()) {
		outline->AddFilter(new MouseDownFilter(this));
		outline->AddFilter(new MouseUpFilter(this));
		outline->AddFilter(new TrackMouseMovedFilter(this));
	} else {
		AddFilter(new MouseDownFilter(this));
		AddFilter(new MouseUpFilter(this));
		AddFilter(new TrackMouseMovedFilter(this));
	}
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
	if (message->what == kMsgDropMarkerCleanup) {
		_ClearDropMarkerIfDragEnded();
		return;
	}
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
	if (message->WasDropped() && message->what == MSG_DRAG_ITEM) {
		DEBUG_PRINT("TrackListView: Posting 'drag' drop to Window\n");
		ClearDropMarker();
		ClearHaifyActiveDragMessage();
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
			TrackRow* row = static_cast<TrackRow*>(baseRow);
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
TrackListView::InitiateDrag(BPoint point, bool)
{
	fPreserveGroupOnMouseDown = false;
	fDeferredGroupClick = false;
	BRow* baseRow = RowAt(point);
	if (!baseRow)
		baseRow = CurrentSelection();
	if (!_RestoreDragSelection(baseRow))
		return false;

	if (baseRow) {
		TrackRow* row = static_cast<TrackRow*>(baseRow);
		DEBUG_PRINT("InitiateDrag: Initiating drag for track %s\n",
			row->fTrackUri.c_str());
		if (!row->fTrackUri.empty()) {
			MessageContracts::DragItem item = _DragItemForRow(row);
			BMessage dragMessage = MessageContracts::MakeDragItem(item);
			auto getString = [&](int32 column) -> const char* {
				BStringField* field =
					dynamic_cast<BStringField*>(row->GetField(column));
				return field ? field->String() : "";
			};
			dragMessage.AddString(MessageFields::Title, getString(1));
			dragMessage.AddString(MessageFields::Artist, getString(2));
			dragMessage.AddString(MessageFields::Album, getString(5));
			dragMessage.AddString(MessageFields::Duration, getString(6));

			BRect dragRect(point.x - 100, point.y - 10, point.x + 100,
				point.y + 10);
			SetHaifyActiveDragMessage(dragMessage);
			DragMessage(&dragMessage, dragRect, this);
			return true;
		}
	} else {
		DEBUG_PRINT("InitiateDrag: No row found for drag\n");
	}
	return false;
}

MessageContracts::DragItem
TrackListView::_DragItemForRow(BRow* baseRow) const
{
	auto row = dynamic_cast<TrackRow*>(baseRow);
	if (!row)
		return {};
	MessageContracts::DragItem item{row->fTrackUri, SpotifyItemKindForUri(row->fTrackUri)};
	auto window = dynamic_cast<PlaylistWindow*>(Window());
	if (!window)
		return item;
	item.sourcePlaylist = window->GetUri();
	for (int32 i = 0; i < CountRows(); i++) {
		if (RowAt(i) == row) {
			item.sourceIndex = i;
			break;
		}
	}
	auto indices = SelectedRowIndices();
	if (SpotifyItemKindForUri(item.sourcePlaylist) != kSpotifyItemPlaylist
			|| indices.size() < 2
			|| !std::binary_search(indices.begin(), indices.end(), item.sourceIndex))
		return item;
	item.sourceIndices = indices;
	for (int32_t index : indices)
		item.sourceUris.push_back(static_cast<const TrackRow*>(RowAt(index))->fTrackUri);
	item.sourceSnapshot = window->GetPlaylistSnapshot();
	item.intent = MessageContracts::DropIntent::Reorder;
	return item;
}

void
TrackListView::_RememberDragSelection(BPoint point)
{
	fMouseDownDrag = _DragItemForRow(RowAt(point));
}

bool
TrackListView::_DragSelectionIsCurrent(const MessageContracts::DragItem& saved,
	BRow* row) const
{
	auto window = dynamic_cast<PlaylistWindow*>(Window());
	if (!row || !window
			|| saved.sourcePlaylist != window->GetUri()
			|| saved.sourceSnapshot != window->GetPlaylistSnapshot()
			|| RowAt(saved.sourceIndex) != row)
		return false;
	for (size_t i = 0; i < saved.sourceIndices.size(); i++) {
		auto selected = dynamic_cast<const TrackRow*>(RowAt(saved.sourceIndices[i]));
		if (!selected || selected->fTrackUri != saved.sourceUris[i])
			return false;
	}
	return true;
}

bool
TrackListView::_RestoreDragSelection(BRow* row)
{
	auto saved = std::move(fMouseDownDrag);
	fMouseDownDrag = {};
	if (saved.sourceIndices.empty())
		return true;
	if (!_DragSelectionIsCurrent(saved, row))
		return false;
	DeselectAll();
	for (int32_t index : saved.sourceIndices)
		AddToSelection(RowAt(index));
	return true;
}

void
TrackListView::_FinishDeferredClick()
{
	fPreserveGroupOnMouseDown = false;
	if (!fDeferredGroupClick)
		return;
	fDeferredGroupClick = false;
	auto saved = std::move(fMouseDownDrag);
	fMouseDownDrag = {};
	BRow* row = RowAt(saved.sourceIndex);
	if (!_DragSelectionIsCurrent(saved, row))
		return;
	DeselectAll();
	AddToSelection(row);
	SelectionChanged();
}

std::vector<int32_t>
TrackListView::SelectedRowIndices() const
{
	std::vector<int32_t> indices;
	for (BRow* row = CurrentSelection(); row; row = CurrentSelection(row)) {
		for (int32 i = 0; i < CountRows(); i++) {
			if (RowAt(i) == row) {
				indices.push_back(i);
				break;
			}
		}
	}
	std::sort(indices.begin(), indices.end());
	return indices;
}


void
TrackListView::MouseMoved(BPoint point, uint32 transit,
	const BMessage* dragMessage)
{
	if (transit == B_EXITED_VIEW)
		ClearDropMarker();
	else if (dragMessage && dragMessage->what == MSG_DRAG_ITEM) {
		_UpdateDropMarker(point, dragMessage);
		if (Window())
			Window()->PostMessage(kMsgCheckLazyLoad);
		return;
	}
	BColumnListView::MouseMoved(point, transit, dragMessage);
	if (Window())
		Window()->PostMessage(kMsgCheckLazyLoad);
}


void
TrackListView::Draw(BRect update)
{
	BColumnListView::Draw(update);
	fDropMarker.Draw(this);
}


void
TrackListView::SelectionChanged()
{
	if (fPreserveGroupOnMouseDown) {
		fPreserveGroupOnMouseDown = false;
		// Native MouseDown just selected the grabbed row. Restore the rest before
		// returning to the looper, so no frame is drawn with a collapsed group.
		if (_DragSelectionIsCurrent(fMouseDownDrag, RowAt(fMouseDownDrag.sourceIndex))) {
			for (int32_t index : fMouseDownDrag.sourceIndices)
				AddToSelection(RowAt(index));
			fDeferredGroupClick = true;
		}
	}
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
		if (!column->IsVisible())
			continue;
		left += column->Width();
		if (x < left)
			return column->LogicalFieldNum();
	}
	return CountColumns() - 1;
}


void
TrackListView::_UpdateDropMarker(BPoint point, const BMessage* dragMessage)
{
	DropFeedbackFlags flags = fDropMarker.Flags();
	if ((flags & (kDropFeedbackInsertMarker
			| kDropFeedbackAppendMarker)) == 0) {
		ClearDropMarker();
		return;
	}

	bool canReorderDrag = false;
	if (PlaylistWindow* window = dynamic_cast<PlaylistWindow*>(Window())) {
		std::string sourcePlaylist = dragMessage
			? dragMessage->GetString(MessageFields::SourcePlaylist, "") : "";
		canReorderDrag = !sourcePlaylist.empty()
			&& sourcePlaylist == window->GetUri()
			&& dragMessage->GetInt32(MessageFields::SourceIndex, -1) >= 0;
	}

	if (canReorderDrag && (flags & kDropFeedbackInsertMarker) != 0) {
		fDropMarker.UpdateInsertForPoint(point);
	} else {
		fDropMarker.UpdateAppend();
	}
	if (fDropMarker.IsActive())
		_StartDropMarkerCleanupRunner();
}


void
TrackListView::UpdateDropMarkerFromDrag(BPoint screenWhere,
	const BMessage* dragMessage)
{
	BPoint point = TrackRowPointFromScreen(this, screenWhere);
	_UpdateDropMarker(point, dragMessage);
}


void
TrackListView::ClearDropMarker()
{
	if (fDropMarker.Clear())
		_StopDropMarkerCleanupRunner();
}


void
TrackListView::SetDropFeedbackFlags(DropFeedbackFlags flags)
{
	if (fDropMarker.SetFlags(flags))
		_StopDropMarkerCleanupRunner();
}


void
TrackListView::_StartDropMarkerCleanupRunner()
{
	if (fDropMarkerCleanupRunner || !Window())
		return;
	BMessage message(kMsgDropMarkerCleanup);
	fDropMarkerCleanupRunner = new BMessageRunner(BMessenger(this),
		&message, 100000LL);
}


void
TrackListView::_StopDropMarkerCleanupRunner()
{
	delete fDropMarkerCleanupRunner;
	fDropMarkerCleanupRunner = nullptr;
}


void
TrackListView::_ClearDropMarkerIfDragEnded()
{
	BMessage drag;
	BPoint where;
	uint32 buttons = 0;
	GetMouse(&where, &buttons, false);
	if ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0
			&& GetHaifyActiveDragMessage(drag)) {
		return;
	}
	ClearHaifyActiveDragMessage();
	ClearDropMarker();
	if (be_app)
		be_app->PostMessage(MSG_HAIFY_DRAG_ENDED);
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
