#include "PlaybackSeekBarView.h"
#include "Config.h"

#include "Messages.h"
#include "SettingsController.h"
#include "UiScale.h"

#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

static const float kMinimumBarThickness = 18.0f;
static const float kMinimumViewHeight = 20.0f;


static float
FontLineHeight()
{
    return UiScale::LineHeight();
}


static float
ScaledViewHeight(float scale)
{
    return std::max(kMinimumViewHeight * scale, FontLineHeight() + 8.0f);
}


static float
ScaledBarThickness(float scale)
{
    return std::max(kMinimumBarThickness * scale,
        ScaledViewHeight(scale) - 6.0f);
}


static void
ApplyScaledLayoutSize(BView* view, float scale)
{
    if (!view)
        return;
    float fontH = FontLineHeight();
    float viewH = ScaledViewHeight(scale);
    view->SetExplicitMinSize(BSize(fontH * 10.0f * scale, viewH));
    view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, viewH));
    view->SetExplicitPreferredSize(BSize(fontH * 24.0f * scale, viewH));
}

static uint8
BlendChannel(uint8 source, uint8 target, float amount)
{
    return (uint8)std::clamp((int32)std::round(source
        + (target - source) * amount), 0, 255);
}

static rgb_color
FillHighlight(rgb_color color)
{
    uint8 brightest = std::max(color.red,
        std::max(color.green, color.blue));
    uint8 target = std::max<uint8>(brightest, 220);
    return (rgb_color) {
        BlendChannel(color.red, target, 0.588f),
        BlendChannel(color.green, target, 0.588f),
        BlendChannel(color.blue, target, 0.588f),
        color.alpha
    };
}

static rgb_color
FillShadow(rgb_color color)
{
    uint8 darkest = std::min(color.red,
        std::min(color.green, color.blue));
    int32 shadowBase = (int32)std::round(darkest * 0.72f);
    auto shadowChannel = [darkest, shadowBase](uint8 channel) {
        return (uint8)std::clamp((int32)std::round(shadowBase
            + (channel - darkest) * 0.667f), 0, 255);
    };
    return (rgb_color) {
        shadowChannel(color.red),
        shadowChannel(color.green),
        shadowChannel(color.blue),
        color.alpha
    };
}

PlaybackSeekBarView::PlaybackSeekBarView(const char* name)
    : BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_SUPPORTS_LAYOUT),
      fTracking(false)
{
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    _InitColors();
    ApplyScaledLayoutSize(this, fLayoutScale);
}

PlaybackSeekBarView::PlaybackSeekBarView(BMessage* archive)
    : BView(archive), fTracking(false)
{
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    _InitColors();
    ApplyScaledLayoutSize(this, fLayoutScale);

    archive->FindInt64("duration", &fDuration);
    archive->FindInt64("position", &fPosition);

    ssize_t size;
    const rgb_color* c;
    if (archive->FindData("fill_color", B_RGB_COLOR_TYPE,
            (const void**)&c, &size) == B_OK && size == sizeof(rgb_color))
        fFill = *c;
}

void PlaybackSeekBarView::_InitColors() {
    fBg     = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT);
    fFill   = (rgb_color) {
        (uint8)kDefaultSeekBarColorRed,
        (uint8)kDefaultSeekBarColorGreen,
        (uint8)kDefaultSeekBarColorBlue,
        (uint8)kDefaultSeekBarColorAlpha
    };
}

BArchivable* PlaybackSeekBarView::Instantiate(BMessage* data) {
    if (validate_instantiation(data, "PlaybackSeekBarView"))
        return new PlaybackSeekBarView(data);
    return nullptr;
}

status_t PlaybackSeekBarView::Archive(BMessage* data, bool) const {
    BView::Archive(data, false);
    data->AddString("add_on", HAIFY_MIME_SIG);
    data->AddInt64("duration", fDuration);
    data->AddInt64("position", fPosition);
    data->AddData("fill_color", B_RGB_COLOR_TYPE, &fFill, sizeof(rgb_color));
    return B_OK;
}

void PlaybackSeekBarView::AttachedToWindow() {
    SetViewColor(fTransparentBackground ? B_TRANSPARENT_COLOR
        : ui_color(B_PANEL_BACKGROUND_COLOR));
    SetLowColor(ViewColor());
}


void PlaybackSeekBarView::SetDuration(bigtime_t duration) {
    fDuration = std::max((bigtime_t)0, duration);
    if (fPosition > fDuration)
        fPosition = fDuration;
    Invalidate();
}

void PlaybackSeekBarView::SetPosition(bigtime_t pos) {
    pos = std::max((bigtime_t)0, pos);
    if (fDuration > 0)
        pos = std::min(pos, fDuration);
    fPosition = pos;
    Invalidate();
}

void PlaybackSeekBarView::SetColors(rgb_color bg, rgb_color fill) {
    fBg     = bg;
    fFill   = fill;
    Invalidate();
}

void PlaybackSeekBarView::SetTransparentBackground(bool transparent) {
    fTransparentBackground = transparent;
    SetFlags(transparent
        ? Flags() | B_TRANSPARENT_BACKGROUND
        : Flags() & ~B_TRANSPARENT_BACKGROUND);
    SetViewColor(transparent ? B_TRANSPARENT_COLOR
        : ui_color(B_PANEL_BACKGROUND_COLOR));
    SetLowColor(ViewColor());
    Invalidate();
}

void PlaybackSeekBarView::SetLayoutScale(float scale) {
    if (scale < 1.0f)
        scale = 1.0f;
    if (fLayoutScale == scale)
        return;
    fLayoutScale = scale;
    ApplyScaledLayoutSize(this, fLayoutScale);
    InvalidateLayout();
    Invalidate();
}

void PlaybackSeekBarView::Draw(BRect) {
    _DrawBar(Bounds());
}

void PlaybackSeekBarView::_DrawBar(const BRect& r) {
    if (!fTransparentBackground) {
        SetHighColor(ViewColor());
        FillRect(r);
    }

    BRect track = _TrackRect();

    // SoundPlay-style square track on the normal panel background. Its outer
    // bevel is separate from the dynamically colored bevel of the fill.
    SetHighColor(fBg);
    FillRect(track);
    SetHighColor((rgb_color) { 154, 154, 156, 255 });
    StrokeLine(track.LeftBottom(), track.LeftTop());
    StrokeLine(track.LeftTop(), track.RightTop());
    SetHighColor((rgb_color) { 254, 254, 252, 255 });
    StrokeLine(track.RightTop(), track.RightBottom());
    StrokeLine(track.RightBottom(), track.LeftBottom());

    BRect inner = track.InsetByCopy(1.0f, 1.0f);
    if (!inner.IsValid())
        return;
    SetHighColor(fBg);
    FillRect(inner);

    if (fDuration > 0) {
        float ratio = std::clamp((float)fPosition / (float)fDuration, 0.0f, 1.0f);
        int32 fillWidth = (int32)std::round(
            ratio * (inner.IntegerWidth() + 1));
        if (fillWidth > 0) {
            BRect fill = inner;
            fill.right = fill.left + fillWidth - 1;
            SetHighColor(fFill);
            FillRect(fill);
            if (fill.Width() >= 1.0f && fill.Height() >= 1.0f) {
                SetHighColor(FillHighlight(fFill));
                StrokeLine(fill.LeftBottom(), fill.LeftTop());
                StrokeLine(fill.LeftTop(), fill.RightTop());
                SetHighColor(FillShadow(fFill));
                StrokeLine(fill.RightTop(), fill.RightBottom());
                StrokeLine(fill.RightBottom(), fill.LeftBottom());
            }
        }
    }
}

BRect PlaybackSeekBarView::_TrackRect() const {
    BRect bounds = Bounds();
    BRect track = bounds;
    float availableHeight = bounds.Height() + 1.0f;
    float trackHeight = std::min(ScaledBarThickness(fLayoutScale),
        availableHeight);
    track.top = std::floor(bounds.top
        + (availableHeight - trackHeight) / 2.0f);
    track.bottom = track.top + trackHeight - 1.0f;
    return track;
}

void PlaybackSeekBarView::MouseDown(BPoint where) {
    _SeekFromPoint(where);
    fTracking = true;
    SetMouseEventMask(B_POINTER_EVENTS, 0);
}

void PlaybackSeekBarView::MouseUp(BPoint) {
    fTracking = false;
}

void PlaybackSeekBarView::MouseMoved(BPoint where, uint32, const BMessage*) {
    if (fTracking)
        _SeekFromPoint(where);
}

void PlaybackSeekBarView::SetTarget(BMessenger target) {
    fTarget = target;
}

void PlaybackSeekBarView::_SeekFromPoint(BPoint where) {
    if (fDuration <= 0)
        return;
    BRect track = _TrackRect().InsetByCopy(1.0f, 1.0f);
    if (!track.IsValid() || track.Width() <= 0.0f)
        return;
    float ratio = std::clamp(
        (where.x - track.left) / track.Width(), 0.0f, 1.0f);
    bigtime_t newPos = (bigtime_t)(ratio * fDuration);
    SetPosition(newPos);

    BMessage msg(MSG_SEEK_REQUEST);
    msg.AddInt64("position", newPos);
    BMessenger target = fTarget.IsValid() ? fTarget : BMessenger(nullptr, Window());
    target.SendMessage(&msg);
}

void PlaybackSeekBarView::MessageReceived(BMessage* msg) {
    if (msg->WasDropped()) {
        const rgb_color* color;
        ssize_t size;
        if (msg->FindData("RGBColor", B_RGB_COLOR_TYPE,
                (const void**)&color, &size) == B_OK
                && size == sizeof(rgb_color)) {
            fFill = *color;
            Invalidate();
            BMessage notify(MSG_SEEKBAR_COLOR_DROPPED);
            notify.AddData("color", B_RGB_COLOR_TYPE, color, sizeof(rgb_color));
            BMessenger target = fTarget.IsValid()
                ? fTarget : BMessenger(nullptr, Window());
            target.SendMessage(&notify);
            return;
        }
    }
    BView::MessageReceived(msg);
}
