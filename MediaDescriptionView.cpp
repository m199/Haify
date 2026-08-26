#include "MediaDescriptionView.h"

#include <Application.h>
#include <Cursor.h>
#include <InterfaceDefs.h>
#include <Rect.h>
#include <Roster.h>


MediaDescriptionView::MediaDescriptionView(const char* name)
	:
	BTextView(name)
{
	MakeEditable(false);
	MakeSelectable(false);
	SetWordWrap(true);
	SetInsets(4, 4, 4, 4);
}


void
MediaDescriptionView::AttachedToWindow()
{
	BTextView::AttachedToWindow();
	SetEventMask(B_POINTER_EVENTS, 0);
	_UpdateTextRect();
}


void
MediaDescriptionView::FrameResized(float width, float height)
{
	BTextView::FrameResized(width, height);
	_UpdateTextRect();
}


void
MediaDescriptionView::MouseDown(BPoint where)
{
	const MediaDescriptionLink* link = _LinkAt(where);
	if (link && be_roster) {
		const char* argv[] = {link->url.c_str(), nullptr};
		be_roster->Launch("text/html", 1, argv);
		return;
	}
	BView::MouseDown(where);
}


void
MediaDescriptionView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* message)
{
	bool overLink = transit != B_EXITED_VIEW && _LinkAt(where) != nullptr;
	if (overLink != fOverLink) {
		fOverLink = overLink;
		BCursor cursor(overLink ? B_CURSOR_ID_FOLLOW_LINK
			: B_CURSOR_ID_SYSTEM_DEFAULT);
		SetViewCursor(&cursor);
	}
}


void
MediaDescriptionView::SetLinks(const std::vector<MediaDescriptionLink>& links)
{
	fLinks = links;
}


void
MediaDescriptionView::Reflow()
{
	SetWordWrap(true);
	_UpdateTextRect();
	Invalidate();
}


void
MediaDescriptionView::_UpdateTextRect()
{
	float left;
	float top;
	float right;
	float bottom;
	GetInsets(&left, &top, &right, &bottom);

	BRect bounds = Bounds();
	BRect textRect(left, top, bounds.Width() - right, 100000.0f);
	if (textRect.right < textRect.left)
		textRect.right = textRect.left;
	SetTextRect(textRect);
}


const MediaDescriptionLink*
MediaDescriptionView::_LinkAt(BPoint where) const
{
	int32 offset = OffsetAt(where);
	for (const MediaDescriptionLink& link : fLinks) {
		if (offset >= link.start && offset < link.end)
			return &link;
	}
	return nullptr;
}
