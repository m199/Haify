#include "MediaDescriptionView.h"

#include <Application.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Rect.h>
#include <Region.h>
#include <Roster.h>
#include <Window.h>

namespace {

bool
IsPrimaryMouseClick(BMessage* message)
{
	int32 buttons = 0;
	if (!message || message->FindInt32("buttons", &buttons) != B_OK)
		return true;
	return (buttons & B_PRIMARY_MOUSE_BUTTON) != 0
		&& (buttons & (B_SECONDARY_MOUSE_BUTTON
			| B_TERTIARY_MOUSE_BUTTON)) == 0;
}


bool
StayedNear(BPoint first, BPoint second)
{
	const float x = first.x - second.x;
	const float y = first.y - second.y;
	return x * x + y * y <= 9.0f;
}

} // namespace


MediaDescriptionView::MediaDescriptionView(const char* name)
	:
	BTextView(name)
{
	MakeEditable(false);
	MakeSelectable(true);
	SetWordWrap(true);
	SetInsets(4, 4, 4, 4);
}


void
MediaDescriptionView::AttachedToWindow()
{
	BTextView::AttachedToWindow();
	_UpdateTextRect();
}


void
MediaDescriptionView::FrameResized(float width, float height)
{
	BTextView::FrameResized(width, height);
	_UpdateTextRect();
	if (fResetOnNextResize) {
		_ResetToTop();
		fResetOnNextResize = false;
	}
}


void
MediaDescriptionView::MouseDown(BPoint where)
{
	const MediaDescriptionLink* link = _LinkAt(where);
	fPendingLink = link != nullptr && IsPrimaryMouseClick(
		Window() ? Window()->CurrentMessage() : nullptr);
	fPendingLinkUrl = fPendingLink ? link->url : "";
	fPendingLinkPoint = where;
	fResetOnNextResize = false;

	BTextView::MouseDown(where);
}


void
MediaDescriptionView::MouseUp(BPoint where)
{
	BTextView::MouseUp(where);

	int32 selectionStart = 0;
	int32 selectionEnd = 0;
	GetSelection(&selectionStart, &selectionEnd);
	if (fPendingLink && selectionStart == selectionEnd
			&& StayedNear(fPendingLinkPoint, where) && be_roster) {
		const char* argv[] = {fPendingLinkUrl.c_str(), nullptr};
		be_roster->Launch("text/html", 1, argv);
	}
	fPendingLink = false;
	fPendingLinkUrl.clear();
}


void
MediaDescriptionView::SetLinks(const std::vector<MediaDescriptionLink>& links)
{
	fLinks = links;
	_ResetToTop();
	fResetOnNextResize = true;
}


void
MediaDescriptionView::Reflow()
{
	SetWordWrap(true);
	_UpdateTextRect();
	_ResetToTop();
	fResetOnNextResize = true;
	Invalidate();
}


void
MediaDescriptionView::_ResetToTop()
{
	ScrollTo(0.0f, 0.0f);
	fPendingLink = false;
	fPendingLinkUrl.clear();
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
	BRect textRect = TextRect();
	textRect.left = left;
	textRect.top = top;
	textRect.right = bounds.Width() - right;
	float visibleBottom = bounds.Height() - bottom;
	if (textRect.bottom < visibleBottom)
		textRect.bottom = visibleBottom;
	if (textRect.right < textRect.left)
		textRect.right = textRect.left;
	if (textRect.bottom < textRect.top)
		textRect.bottom = textRect.top;
	SetTextRect(textRect);
}


const MediaDescriptionLink*
MediaDescriptionView::_LinkAt(BPoint where) const
{
	for (const MediaDescriptionLink& link : fLinks) {
		BRegion linkRegion;
		GetTextRegion(link.start, link.end, &linkRegion);
		if (linkRegion.Contains(where))
			return &link;
	}
	return nullptr;
}
