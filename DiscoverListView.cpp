#include "DiscoverListView.h"
#include "spotify/SpotifyUri.h"

#include <Message.h>
#include <MessageFilter.h>
#include <ScrollBar.h>
#include <Window.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <cstring>

static bool
IsSecondaryMouseClick(BMessage* message)
{
	int32 buttons = 0;
	return message && message->FindInt32("buttons", &buttons) == B_OK
		&& (buttons & (B_SECONDARY_MOUSE_BUTTON
			| B_TERTIARY_MOUSE_BUTTON)) != 0;
}

static bool
IsListContentView(BView* view)
{
	if (!view || dynamic_cast<BScrollBar*>(view))
		return false;
	return !view->Name() || strcmp(view->Name(), "header") != 0;
}

static bool
ViewBelongsToDiscoverList(BView* view, DiscoverListView* owner)
{
	for (BView* parent = view; parent; parent = parent->Parent()) {
		if (parent == owner || parent == owner->ScrollView())
			return true;
	}
	return false;
}

static bool
FindScreenPoint(BMessage* message, BView* view, BPoint& screenWhere)
{
	if (message->FindPoint("screen_where", &screenWhere) == B_OK)
		return true;

	BPoint where;
	if (message->FindPoint("where", &where) != B_OK)
		return false;
	screenWhere = view->ConvertToScreen(where);
	return true;
}


void
BoldStringColumn::DrawField(BField* field, BRect rect, BView* parent)
{
	BoldStringField* f = static_cast<BoldStringField*>(field);
	if (!f || (!f->fIsPlaying && f->fEnabled)) {
		BStringColumn::DrawField(field, rect, parent);
		return;
	}

	BFont originalFont;
	rgb_color originalColor;
	if (f->fIsPlaying) {
		parent->GetFont(&originalFont);
		BFont boldFont(be_bold_font);
		boldFont.SetSize(originalFont.Size());
		parent->SetFont(&boldFont);
	}
	if (!f->fEnabled) {
		originalColor = parent->HighColor();
		parent->SetHighColor(tint_color(ui_color(B_PANEL_TEXT_COLOR),
			B_DISABLED_LABEL_TINT));
	}
	BStringColumn::DrawField(field, rect, parent);
	if (!f->fEnabled)
		parent->SetHighColor(originalColor);
	if (f->fIsPlaying)
		parent->SetFont(&originalFont);
}

class DiscoverRightClickFilter : public BMessageFilter {
public:
	DiscoverRightClickFilter(DiscoverListView* owner)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
		  fOwner(owner)
	{
	}

	filter_result Filter(BMessage* msg, BHandler** target) override
	{
		if (!fOwner || !msg || msg->what != B_MOUSE_DOWN)
			return B_DISPATCH_MESSAGE;
		if (!IsSecondaryMouseClick(msg))
			return B_DISPATCH_MESSAGE;

		BView* view = dynamic_cast<BView*>(*target);
		if (!IsListContentView(view))
			return B_DISPATCH_MESSAGE;
		if (!ViewBelongsToDiscoverList(view, fOwner))
			return B_DISPATCH_MESSAGE;

		BPoint screenWhere;
		if (!FindScreenPoint(msg, view, screenWhere))
			return B_DISPATCH_MESSAGE;

		BMessage show('rCf!');
		show.AddPoint("screenPt", screenWhere);
		if (fOwner->Looper())
			fOwner->Looper()->PostMessage(&show, fOwner);
		return B_SKIP_MESSAGE;
	}

private:
	DiscoverListView* fOwner;
};

class DiscoverDropFilter : public BMessageFilter {
public:
	DiscoverDropFilter(DiscoverListView* owner)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
		  fOwner(owner)
	{
	}

	filter_result Filter(BMessage* msg, BHandler** target) override
	{
		if (!fOwner || !msg || !msg->WasDropped() || msg->what != 'drag')
			return B_DISPATCH_MESSAGE;

		BView* view = dynamic_cast<BView*>(*target);
		if (!view || dynamic_cast<BScrollBar*>(view))
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

		fOwner->ForwardDroppedMessage(msg);
		return B_SKIP_MESSAGE;
	}

private:
	DiscoverListView* fOwner;
};


DiscoverRow::DiscoverRow(const std::vector<std::string>& vals,
                         const std::vector<std::string>& uris,
                         const std::vector<std::string>& titles,
                         bool writable, bool owned)
	: BRow(), fUris(uris), fTitles(titles), fWritable(writable),
	  fOwned(owned)
{
	for (int32 i = 0; i < (int32)vals.size(); i++) {
		SetField(new BoldStringField(vals[i].c_str(), writable), i);
	}
}


DiscoverListView::DiscoverListView(const char* name,
                                   const std::vector<ColDef>& cols,
                                   int32 logicalTab,
                                   bool showHorizontalScrollbar)
	: BColumnListView(name, B_NAVIGABLE, B_NO_BORDER,
		showHorizontalScrollbar),
	  fLogicalTab(logicalTab)
{
	SetViewUIColor(B_LIST_BACKGROUND_COLOR);

	for (int32 i = 0; i < (int32)cols.size(); i++) {
		BColumn* col = new BoldStringColumn(cols[i].label, cols[i].width,
			60, 9999, B_TRUNCATE_END);
		AddColumn(col, i);
		fActions.push_back(cols[i].action);
	}
}

void
DiscoverListView::AttachedToWindow()
{
	BColumnListView::AttachedToWindow();
	if (fFiltersInstalled)
		return;
	if (BView* outline = ScrollView()) {
		outline->AddFilter(new DiscoverRightClickFilter(this));
		outline->AddFilter(new DiscoverDropFilter(this));
	} else {
		AddFilter(new DiscoverRightClickFilter(this));
		AddFilter(new DiscoverDropFilter(this));
	}
	fFiltersInstalled = true;
}

void
DiscoverListView::MouseDown(BPoint where)
{
	BMessage* msg = Window() ? Window()->CurrentMessage() : nullptr;
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

	BPoint screen = where;
	ConvertToScreen(&screen);
	_ShowContextMenuAt(screen);
}

void
DiscoverListView::_ShowContextMenuAt(BPoint screenWhere)
{
	BPoint where = screenWhere;
	if (BView* outline = ScrollView())
		outline->ConvertFromScreen(&where);
	else
		ConvertFromScreen(&where);

	DiscoverRow* row = dynamic_cast<DiscoverRow*>(RowAt(where));
	if (!row) row = dynamic_cast<DiscoverRow*>(CurrentSelection());
	if (!row || row->fUris.empty()) return;
	AddToSelection(row);

	int32 col = _ColumnAt(where.x);
	if (col < 0 || col >= (int32)row->fUris.size()
			|| row->fUris[col].empty()) {
		col = 0;
	}
	if (col >= (int32)row->fUris.size() || row->fUris[col].empty()) return;

	BMessage rClk('rClk');
	rClk.AddString("uri",      row->fUris[col].c_str());
	rClk.AddInt32("tab", fLogicalTab);
	rClk.AddBool("owned", row->fOwned);
	if (col < (int32)row->fTitles.size())
		rClk.AddString("title", row->fTitles[col].c_str());
	rClk.AddPoint ("screenPt", screenWhere);
	Window()->PostMessage(&rClk);
}


void
DiscoverListView::SelectionChanged()
{
	BColumnListView::SelectionChanged();

	BMessage* cur = Window() ? Window()->CurrentMessage() : nullptr;
	if (!cur || cur->what != B_MOUSE_DOWN) return;

	_DispatchClick(false);
}

void
DiscoverListView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && message->what == 'drag') {
		ForwardDroppedMessage(message);
		return;
	}
	if (message->what == 'rCf!') {
		BPoint screen;
		if (message->FindPoint("screenPt", &screen) == B_OK)
			_ShowContextMenuAt(screen);
		return;
	}
	BColumnListView::MessageReceived(message);
}


void
DiscoverListView::ForwardDroppedMessage(BMessage* message)
{
	if (!message || message->what != 'drag')
		return;

	BMessage drop(*message);
	drop.what = 'dDrp';
	drop.AddInt32("tab", fLogicalTab);
	BPoint point = message->DropPoint();
	if (BView* outline = ScrollView())
		outline->ConvertFromScreen(&point);
	else
		ConvertFromScreen(&point);
	DiscoverRow* targetRow = dynamic_cast<DiscoverRow*>(RowAt(point));
	if (targetRow && !targetRow->fUris.empty()
			&& !targetRow->fUris[0].empty()) {
		drop.AddString("targetUri", targetRow->fUris[0].c_str());
		drop.AddBool("targetWritable", targetRow->fWritable);
		if (!targetRow->fTitles.empty())
			drop.AddString("targetTitle", targetRow->fTitles[0].c_str());
		AddToSelection(targetRow);
	}
	if (Window())
		Window()->PostMessage(&drop);
}


void
DiscoverListView::ItemInvoked()
{
	BColumnListView::ItemInvoked();
	_DispatchClick(true);
}


bool
DiscoverListView::InitiateDrag(BPoint point, bool)
{
	DiscoverRow* row = dynamic_cast<DiscoverRow*>(CurrentSelection());
	if (!row)
		row = dynamic_cast<DiscoverRow*>(RowAt(point));
	if (!row || row->fUris.empty())
		return false;

	int32 column = _ColumnAt(point.x);
	if (column < 0 || column >= (int32)row->fUris.size()
			|| row->fUris[column].empty())
		column = 0;
	if (column >= (int32)row->fUris.size() || row->fUris[column].empty())
		return false;

	std::string uri = row->fUris[column];
	SpotifyItemKind kind = SpotifyItemKindForUri(uri);
	if (kind == kSpotifyItemUnknown)
		return false;
	std::string itemType = SpotifyItemTypeName(kind);

	BMessage drag('drag');
	drag.AddString("uri", uri.c_str());
	drag.AddString("itemType", itemType.c_str());
	if (SpotifyItemCanAddToPlaylist(kind))
		drag.AddString("trackUri", uri.c_str());
	if (kind == kSpotifyItemAlbum)
		drag.AddString("albumUri", uri.c_str());
	if (column < (int32)row->fTitles.size())
		drag.AddString("title", row->fTitles[column].c_str());
	else if (column < (int32)row->fUris.size())
		drag.AddString("title", "");
	if (row->fTitles.size() > 1)
		drag.AddString("artist", row->fTitles[1].c_str());

	BRect dragRect(point.x - 100, point.y - 10, point.x + 100, point.y + 10);
	DragMessage(&drag, dragRect, this);
	return true;
}


void
DiscoverListView::SetPlayingUri(const std::string& uri)
{
	for (int32 i = 0; i < CountRows(); i++) {
		DiscoverRow* row = (DiscoverRow*)RowAt(i);
		if (!row) continue;
		bool playing = (!uri.empty() && !row->fUris.empty()
		                && row->fUris[0] == uri);
		if (row->fIsPlaying == playing)
			continue;
		row->fIsPlaying = playing;
		for (int32 column = 0; column < CountColumns(); column++) {
			BoldStringField* f =
				static_cast<BoldStringField*>(row->GetField(column));
			if (f)
				f->fIsPlaying = playing;
		}
		InvalidateRow(row);
	}
}


void
DiscoverListView::_DispatchClick(bool isDouble)
{
	if (!isDouble)
		return;

	DiscoverRow* row = nullptr;
	int32 col = -1;
	std::string title;
	if (!_FindClickTarget(row, col, title))
		return;

	const std::string& uri = row->fUris[col];

	switch (fActions[col]) {
		case kColPlayOnDouble:
			_PostPlayClick(row, uri, title);
			break;

		case kColOpenOnDouble:
			_PostOpenClick(uri, title);
			break;

		case kColRouteOnDouble:
			_PostRouteClick(row, uri, title);
			break;

		default:
			break;
	}
}

bool
DiscoverListView::_FindClickTarget(DiscoverRow*& row, int32& column,
	std::string& title)
{
	if (!Window())
		return false;

	row = (DiscoverRow*)CurrentSelection();
	if (!row)
		return false;

	BPoint where;
	uint32 buttons;
	GetMouse(&where, &buttons, false);

	column = _ColumnAt(where.x);
	if (column < 0 || column >= (int32)fActions.size())
		return false;
	if (column >= (int32)row->fUris.size() || row->fUris[column].empty())
		return false;

	title = column < (int32)row->fTitles.size() ? row->fTitles[column] : "";
	return true;
}

void
DiscoverListView::_PostPlayClick(DiscoverRow* row, const std::string& uri,
	const std::string& title)
{
	SetPlayingUri(uri);
	BMessage msg('play');
	msg.AddString("uri", uri.c_str());
	msg.AddString("title", title.c_str());
	if (row->fTitles.size() > 1)
		msg.AddString("artist", row->fTitles[1].c_str());
	Window()->PostMessage(&msg);
}

void
DiscoverListView::_PostOpenClick(const std::string& uri,
	const std::string& title)
{
	BMessage msg('open');
	msg.AddString("uri", uri.c_str());
	msg.AddString("title", title.c_str());
	Window()->PostMessage(&msg);
}

void
DiscoverListView::_PostRouteClick(DiscoverRow* row, const std::string& uri,
	const std::string& title)
{
	bool playable = SpotifyItemIsPlayable(SpotifyItemKindForUri(uri));
	BMessage msg(playable ? 'play' : 'open');
	msg.AddString("uri", uri.c_str());
	msg.AddString("title", title.c_str());
	if (row->fTitles.size() > 1)
		msg.AddString("artist", row->fTitles[1].c_str());
	Window()->PostMessage(&msg);
}

int32
DiscoverListView::_ColumnAt(float x) const
{
	float left = 0;
	for (int32 i = 0; i < CountColumns(); i++) {
		BColumn* col = ColumnAt(i);
		if (!col) break;
		left += col->Width();
		if (x < left) return i;
	}
	return CountColumns() - 1;
}
