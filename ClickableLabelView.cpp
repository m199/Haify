#include "ClickableLabelView.h"
#include "Messages.h"
#include <Cursor.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Messenger.h>
#include <Message.h>
#include <String.h>
#include <View.h>
#include <Window.h>
#include <math.h>

ClickableLabelView::ClickableLabelView(const char* name, uint32 msgWhat)
    : BView(name, B_WILL_DRAW), fMsgWhat(msgWhat)
{
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}

void ClickableLabelView::SetText(const char* text) {
    fText = text ? text : "";
    InvalidateLayout();
    Invalidate();
}

void ClickableLabelView::UpdateColors() {
    rgb_color bg = ui_color(B_PANEL_BACKGROUND_COLOR);
    SetViewColor(bg);
    SetLowColor(bg);
    Invalidate();
}

void ClickableLabelView::AttachedToWindow() {
    BView::AttachedToWindow();
    UpdateColors();
}

void ClickableLabelView::Draw(BRect updateRect) {
    SetHighColor(ViewColor());
    FillRect(updateRect);

    font_height fh;
    GetFontHeight(&fh);
    SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
    BString text(fText.c_str());
    BFont font;
    GetFont(&font);
    font.TruncateString(&text, B_TRUNCATE_END, Bounds().Width());
    float textHeight = fh.ascent + fh.descent;
    float y = floorf((Bounds().Height() - textHeight) / 2.0f + fh.ascent);
    DrawString(text.String(), BPoint(0.0f, y));
}

void ClickableLabelView::MouseDown(BPoint where) {
    BMessage* current = Window() ? Window()->CurrentMessage() : nullptr;
    int32 buttons = 0;
    if (current)
        current->FindInt32("buttons", &buttons);
    if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
        BView* parent = Parent();
        if (parent) {
            BPoint screenWhere = where;
            ConvertToScreen(&screenWhere);
            BMessage msg(MSG_SHOW_REPLICANT_MENU);
            msg.AddPoint("screen_where", screenWhere);
            BMessenger(parent).SendMessage(&msg);
        }
        return;
    }

    if (fMsgWhat && !fText.empty()) {
        BView* parent = Parent();
        if (parent) {
            BMessage msg(fMsgWhat);
            BMessenger(parent).SendMessage(&msg);
        }
        BView::MouseDown(BPoint());
        return;
    }
    BView::MouseDown(where);
}

void ClickableLabelView::MouseMoved(BPoint, uint32 transit, const BMessage*) {
    if (transit == B_ENTERED_VIEW && fMsgWhat && !fText.empty()) {
        BCursor c(B_CURSOR_ID_FOLLOW_LINK);
        SetViewCursor(&c);
    } else if (transit == B_EXITED_VIEW) {
        BCursor c(B_CURSOR_ID_SYSTEM_DEFAULT);
        SetViewCursor(&c);
    }
}

BSize ClickableLabelView::MinSize() {
    font_height fh;
    GetFontHeight(&fh);
    float w = fText.empty() ? 0.0f : StringWidth(fText.c_str());
    if (w > 24.0f)
        w = 24.0f;
    return BSize(w, fh.ascent + fh.descent);
}

BSize ClickableLabelView::PreferredSize() {
    font_height fh;
    GetFontHeight(&fh);
    float w = fText.empty() ? 0.0f : StringWidth(fText.c_str());
    if (w > 260.0f) w = 260.0f;
    return BSize(w, fh.ascent + fh.descent);
}

BSize ClickableLabelView::MaxSize() {
    font_height fh;
    GetFontHeight(&fh);
    return BSize(B_SIZE_UNLIMITED, fh.ascent + fh.descent);
}
