#include "PlayerBarView.h"
#include "Config.h"
#include "Messages.h"
#include "PlaybackSeekBarView.h"
#include "SettingsController.h"
#include <Message.h>
#include <MessageRunner.h>
#include <Dragger.h>
#include <Catalog.h>
#include <Cursor.h>
#include <File.h>
#include <image.h>
#include <OS.h>
#include <Roster.h>

static const uint32 kMsgTick        = 'tik!';
static const uint32 kMsgRefreshReplicantColor = 'rrcl';
static const char* kNoTrackText     = "Waiting for track information...";
static const float kPlayerBarHeight = 62.0f;
static const float kDraggerRightInset = 8.0f;
static const float kDraggerBottomInset = 3.0f;
static const rgb_color kBlack       = { 0, 0, 0, 255 };
static const rgb_color kWhite       = { 255, 255, 255, 255 };

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlayerBarView"

#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Font.h>
#include <GroupLayout.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <algorithm>
#include <cstdio>
#include <math.h>
#include <string.h>


static rgb_color
_LowColorFor(bool transparent, rgb_color background, rgb_color textColor)
{
    return transparent
        ? blend_color(B_TRANSPARENT_COLOR, textColor, 192) : background;
}


static void
_SetTransparentBackground(BView* view, bool transparent)
{
    if (!view)
        return;

    view->SetFlags(transparent
        ? view->Flags() | B_TRANSPARENT_BACKGROUND
        : view->Flags() & ~B_TRANSPARENT_BACKGROUND);
}


static rgb_color
_ReplicantTextColorFor(BView* view)
{
    rgb_color parentColor = ui_color(B_DESKTOP_COLOR);
    if (view && view->Parent()) {
        rgb_color candidate = view->Parent()->ViewColor();
        if (candidate.alpha != 0)
            parentColor = candidate;
    }
    return parentColor.Brightness() <= 127
        ? kWhite : kBlack;
}


static bool
_ColorsEqual(rgb_color first, rgb_color second)
{
    return first.red == second.red && first.green == second.green
        && first.blue == second.blue && first.alpha == second.alpha;
}


class VolumeIconView : public BView {
public:
    VolumeIconView()
        : BView("volumeIcon", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
    {
        _UpdateBackgroundColor();
    }

    void SetTransparentBackground(bool transparent, rgb_color textColor)
    {
        fTransparentBackground = transparent;
        fTextColor = textColor;
        _UpdateBackgroundColor();
    }

    void SetIcon(BBitmap* icon)
    {
        if (fIcon == icon)
            return;
        fIcon = icon;
        Invalidate();
    }

    void AttachedToWindow() override
    {
        BView::AttachedToWindow();
        _UpdateBackgroundColor();
    }

    void Draw(BRect updateRect) override
    {
        if (!fTransparentBackground) {
            SetHighColor(ViewColor());
            FillRect(updateRect);
        }

        if (!fIcon)
            return;

        BRect b = Bounds();
        BRect ib = fIcon->Bounds();
        BPoint where(floorf(b.left + (b.Width() - ib.Width()) / 2.0f),
            floorf(b.top + (b.Height() - ib.Height()) / 2.0f));
        SetDrawingMode(B_OP_ALPHA);
        DrawBitmap(fIcon, where);
        SetDrawingMode(B_OP_COPY);
    }

    void MouseDown(BPoint where) override
    {
        BMessage* current = Window() ? Window()->CurrentMessage() : nullptr;
        int32 buttons = 0;
        if (current)
            current->FindInt32("buttons", &buttons);
        if ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0 && Parent()) {
            BMessenger(Parent()).SendMessage(MSG_TOGGLE_MUTE);
            return;
        }

        BView::MouseDown(where);
    }

    void MouseMoved(BPoint, uint32 transit, const BMessage*) override
    {
        if (transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW) {
            BCursor cursor(B_CURSOR_ID_FOLLOW_LINK);
            SetViewCursor(&cursor);
        }
    }

private:
    void _UpdateBackgroundColor()
    {
        rgb_color background = fTransparentBackground
            ? B_TRANSPARENT_COLOR : ui_color(B_PANEL_BACKGROUND_COLOR);
        _SetTransparentBackground(this, fTransparentBackground);
        SetViewColor(background);
        SetLowColor(_LowColorFor(fTransparentBackground, background,
            fTextColor));
        Invalidate();
    }

    BBitmap* fIcon = nullptr;
    bool fTransparentBackground = false;
    rgb_color fTextColor = ui_color(B_PANEL_TEXT_COLOR);
};

class TimeLabelView : public BView {
public:
    TimeLabelView(const char* name, const char* text)
        : BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE
            | B_SUPPORTS_LAYOUT),
          fText(text ? text : "")
    {
        _UpdateBackgroundColor();
    }

    void SetText(const char* text)
    {
        BString next(text ? text : "");
        if (fText == next)
            return;

        fText = next;
        Invalidate();
    }

    void SetAlignment(alignment align)
    {
        fAlignment = align;
        Invalidate();
    }

    alignment Alignment() const
    {
        return fAlignment;
    }

    void SetTransparentBackground(bool transparent, rgb_color textColor)
    {
        fTransparentBackground = transparent;
        fTextColor = textColor;
        _UpdateBackgroundColor();
    }

    void Draw(BRect updateRect) override
    {
        if (!fTransparentBackground) {
            SetHighColor(ViewColor());
            FillRect(updateRect);
        }

        SetDrawingMode(fTransparentBackground ? B_OP_ALPHA : B_OP_COPY);
        font_height fh;
        GetFontHeight(&fh);
        float textHeight = fh.ascent + fh.descent;
        float y = floorf((Bounds().Height() - textHeight) / 2.0f + fh.ascent);

        SetHighColor(fTextColor);
        float x = 0.0f;
        if (Alignment() == B_ALIGN_RIGHT)
            x = Bounds().Width() - StringWidth(fText.String());
        DrawString(fText.String(), BPoint(x, y));
        SetDrawingMode(B_OP_COPY);
    }

private:
    void _UpdateBackgroundColor()
    {
        rgb_color background = fTransparentBackground
            ? B_TRANSPARENT_COLOR : ui_color(B_PANEL_BACKGROUND_COLOR);
        _SetTransparentBackground(this, fTransparentBackground);
        SetViewColor(background);
        SetLowColor(_LowColorFor(fTransparentBackground, background,
            fTextColor));
        SetHighColor(fTextColor);
        Invalidate();
    }

    bool fTransparentBackground = false;
    rgb_color fTextColor = ui_color(B_PANEL_TEXT_COLOR);
    BString fText;
    alignment fAlignment = B_ALIGN_LEFT;
};

class TrackInfoView : public BView {
public:
    TrackInfoView()
        : BView("trackInfo", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
    {
        UpdateColors();
    }

    void SetTransparentBackground(bool transparent, rgb_color textColor)
    {
        fTransparentBackground = transparent;
        fTextColor = textColor;
        UpdateColors();
    }

    void UpdateColors()
    {
        rgb_color bg = fTransparentBackground
            ? B_TRANSPARENT_COLOR : ui_color(B_PANEL_BACKGROUND_COLOR);
        _SetTransparentBackground(this, fTransparentBackground);
        SetViewColor(bg);
        SetLowColor(_LowColorFor(fTransparentBackground, bg, fTextColor));
        SetHighColor(fTextColor);
        Invalidate();
    }

    void SetTrack(const char* title, const char* artist)
    {
        fTitle = title ? title : "";
        fArtist = artist ? artist : "";
        InvalidateLayout();
        Invalidate();
    }

    void SetTrackIds(const char* albumId, const char* artistId)
    {
        fAlbumId = albumId ? albumId : "";
        fArtistId = artistId ? artistId : "";
    }

    void Draw(BRect updateRect) override
    {
        if (!fTransparentBackground) {
            SetHighColor(ViewColor());
            FillRect(updateRect);
        }

        BRect bounds = Bounds();
        if (!bounds.IsValid())
            return;

        BFont plainFont;
        GetFont(&plainFont);
        BFont titleFont(*be_bold_font);

        font_height fh;
        titleFont.GetHeight(&fh);
        float textHeight = fh.ascent + fh.descent;
        float y = floorf((bounds.Height() - textHeight) / 2.0f + fh.ascent);

        SetHighColor(fTextColor);
        SetDrawingMode(fTransparentBackground ? B_OP_ALPHA : B_OP_COPY);

        if (fTitle.empty()) {
            BString placeholder(kNoTrackText);
            titleFont.TruncateString(&placeholder, B_TRUNCATE_END,
                bounds.Width());
            SetFont(&titleFont);
            DrawString(placeholder.String(), BPoint(bounds.left, y));
            fTitleFrame = bounds;
            fArtistFrame = BRect();
            SetFont(&plainFont);
            SetDrawingMode(B_OP_COPY);
            return;
        }

        const char* separator = fArtist.empty() ? "" : " - ";
        float separatorWidth = plainFont.StringWidth(separator);
        float available = bounds.Width();

        float fullTitleWidth = titleFont.StringWidth(fTitle.c_str());
        float fullArtistWidth = plainFont.StringWidth(fArtist.c_str());
        float titleWidth = fullTitleWidth;
        float artistWidth = fullArtistWidth;

        if (!fArtist.empty()
                && fullTitleWidth + separatorWidth + fullArtistWidth
                    > available) {
            float textSpace = available - separatorWidth;
            if (textSpace < 0.0f)
                textSpace = 0.0f;

            float artistShare = textSpace * 0.42f;
            if (artistShare > fullArtistWidth)
                artistShare = fullArtistWidth;
            if (artistShare < 40.0f && textSpace > 80.0f)
                artistShare = 40.0f;

            artistWidth = artistShare;
            titleWidth = textSpace - artistWidth;
            if (titleWidth < 24.0f && textSpace > 24.0f) {
                titleWidth = 24.0f;
                artistWidth = textSpace - titleWidth;
            }
        }

        BString title(fTitle.c_str());
        titleFont.TruncateString(&title, B_TRUNCATE_END, titleWidth);
        BString artist(fArtist.c_str());
        plainFont.TruncateString(&artist, B_TRUNCATE_END, artistWidth);

        float x = bounds.left;
        SetFont(&titleFont);
        DrawString(title.String(), BPoint(x, y));
        fTitleFrame = BRect(x, bounds.top, x + titleWidth, bounds.bottom);
        x += titleWidth;

        if (!fArtist.empty()) {
            SetFont(&plainFont);
            DrawString(separator, BPoint(x, y));
            x += separatorWidth;
            DrawString(artist.String(), BPoint(x, y));
            fArtistFrame = BRect(x, bounds.top, x + artistWidth,
                bounds.bottom);
        } else {
            fArtistFrame = BRect();
        }

        SetDrawingMode(B_OP_COPY);
        SetFont(&plainFont);
    }

    void MouseDown(BPoint where) override
    {
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

        BView* parent = Parent();
        if (parent && fTitleFrame.Contains(where) && !fAlbumId.empty()) {
            BMessage msg(MSG_SHOW_ALBUM);
            msg.AddString("id", fAlbumId.c_str());
            BMessenger(parent).SendMessage(&msg);
            return;
        }
        if (parent && fArtistFrame.Contains(where) && !fArtistId.empty()) {
            BMessage msg(MSG_SHOW_ARTIST);
            msg.AddString("id", fArtistId.c_str());
            BMessenger(parent).SendMessage(&msg);
            return;
        }

        BView::MouseDown(where);
    }

    void MouseMoved(BPoint where, uint32 transit, const BMessage*) override
    {
        bool link = (fTitleFrame.Contains(where) && !fAlbumId.empty())
            || (fArtistFrame.Contains(where) && !fArtistId.empty());
        if (transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW) {
            BCursor cursor(link ? B_CURSOR_ID_FOLLOW_LINK
                : B_CURSOR_ID_SYSTEM_DEFAULT);
            SetViewCursor(&cursor);
        }
    }

    BSize MinSize() override
    {
        font_height fh;
        GetFontHeight(&fh);
        return BSize(24.0f, fh.ascent + fh.descent);
    }

    BSize PreferredSize() override
    {
        font_height fh;
        GetFontHeight(&fh);
        return BSize(260.0f, fh.ascent + fh.descent);
    }

    BSize MaxSize() override
    {
        font_height fh;
        GetFontHeight(&fh);
        return BSize(B_SIZE_UNLIMITED, fh.ascent + fh.descent);
    }

private:
    bool        fTransparentBackground = false;
    rgb_color   fTextColor = ui_color(B_PANEL_TEXT_COLOR);
    std::string fTitle;
    std::string fArtist;
    std::string fAlbumId;
    std::string fArtistId;
    BRect fTitleFrame;
    BRect fArtistFrame;
};

static BBitmap*
_LoadVectorIcon(int32 resId, float size)
{
    BResources* ownedRes = nullptr;
    BResources* res = nullptr;

    if (be_app) {
        app_info info;
        if (be_app->GetAppInfo(&info) == B_OK
            && strcmp(info.signature, HAIFY_MIME_SIG) == 0) {
            res = be_app->AppResources();
        }
    }

    if (!res) {
        image_info imgInfo;
        int32 cookie = 0;
        while (get_next_image_info(B_CURRENT_TEAM, &cookie, &imgInfo) == B_OK) {
            if (strstr(imgInfo.name, "Haify") != nullptr) {
                BFile file(imgInfo.name, B_READ_ONLY);
                if (file.InitCheck() == B_OK) {
                    ownedRes = new BResources(&file, false);
                    res = ownedRes;
                }
                break;
            }
        }
    }

    if (!res) {
        delete ownedRes;
        return nullptr;
    }

    size_t dataSize = 0;
    const void* data = res->LoadResource('VICN', resId, &dataSize);
    BBitmap* bitmap = nullptr;
    if (data && dataSize > 0) {
        int32 side = (int32)size;
        bitmap = new BBitmap(BRect(0, 0, side - 1, side - 1), B_RGBA32);
        if (BIconUtils::GetVectorIcon((const uint8*)data, dataSize,
                bitmap) != B_OK) {
            delete bitmap;
            bitmap = nullptr;
        }
    }

    delete ownedRes;
    return bitmap;
}

static void
_ApplyButtonIcon(BButton* button, BBitmap* icon)
{
    if (button && icon && button->SetIcon(icon) == B_OK)
        button->SetLabel("");
}

static BString
_FormatTime(bigtime_t time)
{
    if (time < 0)
        time = 0;

    int64 totalSeconds = time / 1000000LL;
    int64 seconds = totalSeconds % 60;
    int64 minutes = (totalSeconds / 60) % 60;
    int64 hours = totalSeconds / 3600;

    char buffer[32];
    if (hours > 0)
        snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld",
            (long long)hours, (long long)minutes, (long long)seconds);
    else
        snprintf(buffer, sizeof(buffer), "%lld:%02lld",
            (long long)minutes, (long long)seconds);
    return BString(buffer);
}

PlayerBarView::PlayerBarView()
    : BView("PlayerBarView",
            B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE
                | B_SUPPORTS_LAYOUT)
{
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    _BuildUI();
    _LoadReplicantAppearance();
}

PlayerBarView::PlayerBarView(BMessage* archive)
    : BView(archive)
{
    SetFlags(Flags() | B_WILL_DRAW | B_FRAME_EVENTS
        | B_FULL_UPDATE_ON_RESIZE | B_SUPPORTS_LAYOUT);
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    _BuildUI();
    _LoadReplicantAppearance();

    archive->FindBool("playing", &fIsPlaying);
    archive->FindBool("seekbar_use_system_color", &fUseSystemSeekBarColor);
    ssize_t colorSize = 0;
    const rgb_color* color = nullptr;
    if (archive->FindData("seekbar_color", B_RGB_COLOR_TYPE,
            (const void**)&color, &colorSize) == B_OK
            && colorSize == sizeof(rgb_color)) {
        fSeekBarColor = *color;
    }
    _ApplyReplicantAppearance(archive);
    _ApplySeekBarColors();
    SetPlaying(fIsPlaying);
}

PlayerBarView::~PlayerBarView()
{
    delete fIcoPlay;
    delete fIcoPause;
    delete fIcoPrev;
    delete fIcoNext;
    delete fIcoShuffle;
    delete fIcoShuffleActive;
    delete fIcoRepeat;
    delete fIcoRepeatContext;
    delete fIcoRepeatTrack;
    delete fIcoVolumeMuted;
    delete fIcoVolume;
}

BArchivable* PlayerBarView::Instantiate(BMessage* data) {
    if (!validate_instantiation(data, "PlayerBarView"))
        return nullptr;
    return new PlayerBarView(data);
}

status_t PlayerBarView::Archive(BMessage* data, bool) const {
    BView::Archive(data, false);
    data->AddString("add_on", HAIFY_MIME_SIG);
    data->AddInt32("be:add_on_version", 5);
    data->AddBool("be:load_each_time", true);
    data->AddBool("be:unload_on_delete", true);
    data->AddInt32("version", 5);
    data->AddBool("playing", fIsPlaying);
    data->AddBool("seekbar_use_system_color", fUseSystemSeekBarColor);
    data->AddData("seekbar_color", B_RGB_COLOR_TYPE, &fSeekBarColor,
        sizeof(rgb_color));
    data->AddBool("appearance_automatic", fUseAutomaticReplicantColor);
    data->AddInt32("appearance_red", fReplicantColor.red);
    data->AddInt32("appearance_green", fReplicantColor.green);
    data->AddInt32("appearance_blue", fReplicantColor.blue);
    data->AddInt32("appearance_alpha", fReplicantColor.alpha);
    return B_OK;
}

void PlayerBarView::_BuildUI() {
    const float lineHeight = 20.0f;

    fTrackInfoView = new TrackInfoView();
    fTrackInfoView->SetExplicitMinSize(BSize(0.0f, B_SIZE_UNSET));
    fTrackInfoView->SetExplicitAlignment(
        BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER));

    fAddTrackButton = new BButton("addTrack", "+",
        new BMessage(MSG_SHOW_ADD_TRACK_MENU));
    fAddTrackButton->SetExplicitMinSize(BSize(24.0f, lineHeight));
    fAddTrackButton->SetExplicitPreferredSize(BSize(24.0f, lineHeight));
    fAddTrackButton->SetExplicitMaxSize(BSize(24.0f, lineHeight));
    fAddTrackButton->SetExplicitAlignment(
        BAlignment(B_ALIGN_RIGHT, B_ALIGN_VERTICAL_CENTER));
    fAddTrackButton->SetEnabled(false);

    fShuffleButton = new BButton("shuffle", "S", new BMessage(MSG_TOGGLE_SHUFFLE));
    fPrevButton   = new BButton("previous",  "<", new BMessage(MSG_PREV_TRACK));
    fPlayButton   = new BButton("playPause", ">", new BMessage(MSG_PLAY_PAUSE));
    fNextButton   = new BButton("next",      ">", new BMessage(MSG_NEXT_TRACK));
    fRepeatButton = new BButton("repeat",    "R", new BMessage(MSG_TOGGLE_REPEAT));

    fPositionView = new TimeLabelView("position", "0:00");
    fPositionView->SetAlignment(B_ALIGN_RIGHT);
    fPositionView->SetExplicitMinSize(BSize(36.0f, lineHeight));
    fPositionView->SetExplicitMaxSize(BSize(36.0f, lineHeight));
    fPositionView->SetExplicitAlignment(
        BAlignment(B_ALIGN_RIGHT, B_ALIGN_VERTICAL_CENTER));

    fDurationView = new TimeLabelView("duration", "0:00");
    fDurationView->SetExplicitMinSize(BSize(36.0f, lineHeight));
    fDurationView->SetExplicitMaxSize(BSize(36.0f, lineHeight));
    fDurationView->SetExplicitAlignment(
        BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));

    fSeekBar = new PlaybackSeekBarView("seekBar");
    fSeekBar->SetExplicitMinSize(BSize(120.0f, lineHeight));
    fSeekBar->SetExplicitPreferredSize(BSize(240.0f, lineHeight));
    fSeekBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, lineHeight));
    fSeekBar->SetExplicitAlignment(
        BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER));

    fVolumeLabel = new VolumeIconView();
    fVolumeLabel->SetExplicitMinSize(BSize(18.0f, lineHeight));
    fVolumeLabel->SetExplicitMaxSize(BSize(18.0f, lineHeight));
    fVolumeLabel->SetExplicitAlignment(
        BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));

    fVolumeSlider = new BSlider("volume", "", new BMessage(MSG_SET_VOLUME),
        0, 100, B_HORIZONTAL, B_TRIANGLE_THUMB);
    fVolumeSlider->SetHashMarks(B_HASH_MARKS_NONE);
    fVolumeSlider->SetBarThickness(4.0f);
    fVolumeSlider->SetModificationMessage(new BMessage(MSG_SET_VOLUME));
    fVolumeSlider->SetExplicitMinSize(BSize(90.0f, lineHeight));
    fVolumeSlider->SetExplicitPreferredSize(BSize(110.0f, lineHeight));
    fVolumeSlider->SetExplicitMaxSize(BSize(120.0f, lineHeight));
    fVolumeSlider->SetExplicitAlignment(
        BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER));

    fDragger = new BDragger(this);
    fDragger->SetExplicitMinSize(BSize(8.0f, 8.0f));
    fDragger->SetExplicitPreferredSize(BSize(8.0f, 8.0f));
    fDragger->SetExplicitMaxSize(BSize(8.0f, 8.0f));
    fDragger->SetExplicitAlignment(
        BAlignment(B_ALIGN_RIGHT, B_ALIGN_BOTTOM));

    const float trackInfoInset = 2.0f;

    BLayoutBuilder::Grid<>(this, B_USE_SMALL_SPACING, 2.0f)
        .SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_INSETS,
            1.0f, 1.0f)
        .AddGroup(B_HORIZONTAL, 0, 0, 0, 5)
            .AddStrut(trackInfoInset)
            .Add(fTrackInfoView, 1.0f)
        .End()
        .AddGroup(B_HORIZONTAL, 0, 5, 0, 1)
            .AddGlue()
            .Add(fAddTrackButton)
            .AddStrut(6.0f)
        .End()
        .AddGroup(B_HORIZONTAL, 1.0f, 0, 1)
            .Add(fShuffleButton)
            .Add(fPrevButton)
            .Add(fPlayButton)
            .Add(fNextButton)
            .Add(fRepeatButton)
        .End()
        .Add(fPositionView, 1, 1)
        .Add(fSeekBar, 2, 1)
        .Add(fDurationView, 3, 1)
        .Add(fVolumeLabel, 4, 1)
        .Add(fVolumeSlider, 5, 1)
        .Add(fDragger, 6, 1)
        .SetColumnWeight(2, 1.0f)
    .End();

    SetExplicitMinSize(BSize(0.0f, kPlayerBarHeight));
    SetExplicitPreferredSize(BSize(B_SIZE_UNSET, kPlayerBarHeight));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, kPlayerBarHeight));

	_ApplySeekBarColors();
}

void PlayerBarView::_LoadButtonIcons() {
    const float iconSize = 18.0f;
    fIcoPlay = _LoadVectorIcon(2001, iconSize);
    fIcoPause = _LoadVectorIcon(2003, iconSize);
    fIcoPrev = _LoadVectorIcon(2005, iconSize);
    fIcoNext = _LoadVectorIcon(2006, iconSize);
    fIcoShuffle = _LoadVectorIcon(2007, iconSize);
    fIcoShuffleActive = _LoadVectorIcon(2008, iconSize);
    fIcoRepeat = _LoadVectorIcon(2010, iconSize);
    fIcoRepeatContext = _LoadVectorIcon(2011, iconSize);
    fIcoRepeatTrack = _LoadVectorIcon(2012, iconSize);
    fIcoVolumeMuted = _LoadVectorIcon(2013, iconSize);
    fIcoVolume = _LoadVectorIcon(2014, iconSize);

    _ApplyButtonIcon(fShuffleButton, fIcoShuffle);
    _ApplyButtonIcon(fPrevButton, fIcoPrev);
    _ApplyButtonIcon(fPlayButton, fIcoPlay);
    _ApplyButtonIcon(fNextButton, fIcoNext);
    _ApplyButtonIcon(fRepeatButton, fIcoRepeat);
    _UpdateVolumeIcon();
}

void PlayerBarView::_UpdateVolumeIcon()
{
    if (!fVolumeLabel)
        return;

    int32 volume = fVolumeSlider ? fVolumeSlider->Value() : 0;
    fVolumeLabel->SetIcon(volume <= 0 ? fIcoVolumeMuted : fIcoVolume);
}

void PlayerBarView::AttachedToWindow() {
    BView::AttachedToWindow();

    _LoadButtonIcons();
    if (fIsPlaying)
        _ApplyButtonIcon(fPlayButton, fIcoPause);

    app_info info;
    fIsReplicant = true;
    if (be_app && be_app->GetAppInfo(&info) == B_OK)
        fIsReplicant = strcmp(info.signature, HAIFY_MIME_SIG) != 0;

    if (fIsReplicant && _UpdateReplicantAvailability())
        _RegisterReplicant();

    _ApplyTarget();

    BMessage tick(kMsgTick);
    fTickTimer = new BMessageRunner(BMessenger(this), &tick, 500000LL);
}

void PlayerBarView::AllAttached() {
    BView::AllAttached();
    _ApplyBackgroundColors();
    InvalidateLayout();
    if (GetLayout())
        GetLayout()->LayoutItems(true);
}

void PlayerBarView::DetachedFromWindow() {
    delete fTickTimer;
    fTickTimer = nullptr;

    _UnregisterReplicant();
    BView::DetachedFromWindow();
}

void PlayerBarView::SetTarget(BMessenger target) {
    fTarget = target;
    if (Window())
        _ApplyTarget();
}

void PlayerBarView::_ApplyTarget() {
    BMessenger target(this);
    if (fShuffleButton) fShuffleButton->SetTarget(target);
    if (fPrevButton) fPrevButton->SetTarget(target);
    if (fPlayButton) fPlayButton->SetTarget(target);
    if (fNextButton) fNextButton->SetTarget(target);
    if (fRepeatButton) fRepeatButton->SetTarget(target);
    if (fAddTrackButton) fAddTrackButton->SetTarget(target);
    if (fVolumeSlider) fVolumeSlider->SetTarget(target);
    if (fSeekBar) fSeekBar->SetTarget(target);
}

bool PlayerBarView::_RegisterReplicant() {
    if (fRegistered && !fTarget.IsValid())
        fRegistered = false;
    if (!fIsReplicant || fRegistered)
        return fRegistered;
    if (!fTarget.IsValid())
        fTarget = BMessenger(HAIFY_MIME_SIG);
    if (!fTarget.IsValid())
        return false;

    BMessage reg(MSG_REGISTER_REPLICANT);
    reg.AddMessenger("messenger", BMessenger(this));
    reg.AddBool("external", true);
    if (fTarget.SendMessage(&reg) != B_OK)
        return false;

    fRegistered = true;
    _ApplyTarget();
    return true;
}

bool PlayerBarView::_UpdateReplicantAvailability() {
    if (!fIsReplicant)
        return true;

    BMessenger app(HAIFY_MIME_SIG);
    if (!app.IsValid()) {
        fTarget = BMessenger();
        fRegistered = false;
        _SetHaifyUnavailable();
        return false;
    }

    bool becameAvailable = !fHaifyAvailable;
    if (!(fTarget == app)) {
        fTarget = app;
        fRegistered = false;
    }

    fHaifyAvailable = true;
    if (becameAvailable)
        SetTrack(kNoTrackText, "");
    return true;
}

void PlayerBarView::_SetHaifyUnavailable() {
    if (!fHaifyAvailable)
        return;

    fHaifyAvailable = false;
    SetTrack(B_TRANSLATE("Haify is not running"), "");
    SetTrackUri("");
    SetTrackIds("", "");
    SetPlaying(false);
    SetShuffle(false);
    SetRepeat("off");
    SetPosition(0, 0);
}

void PlayerBarView::_UnregisterReplicant() {
    if (!fIsReplicant || !fRegistered) {
        fRegistered = false;
        return;
    }

    if (fTarget.IsValid()) {
        BMessage unreg(MSG_UNREGISTER_REPLICANT);
        unreg.AddMessenger("messenger", BMessenger(this));
        fTarget.SendMessage(&unreg);
    }
    fRegistered = false;
}

static void
_SetControlBackground(BView* view, rgb_color background, bool transparent,
    rgb_color textColor)
{
    if (!view)
        return;

    _SetTransparentBackground(view, transparent);
    view->SetViewColor(background);
    view->SetLowColor(_LowColorFor(transparent, background, textColor));
    view->SetHighColor(textColor);
    view->Invalidate();
}

static void
_SetSliderBackground(BSlider* slider, bool transparent, rgb_color textColor)
{
    if (!slider)
        return;

    rgb_color background = transparent
        ? B_TRANSPARENT_COLOR : ui_color(B_PANEL_BACKGROUND_COLOR);

    _SetTransparentBackground(slider, transparent);
    slider->SetViewColor(background);
    slider->SetLowColor(background);
    slider->SetHighColor(textColor);
    slider->Invalidate();
}

void PlayerBarView::_LoadReplicantAppearance()
{
    HaifySettings settings = SettingsController::Load();
    fUseAutomaticReplicantColor = settings.replicantUseAutomaticColor;
    fReplicantColor = (rgb_color) {
        (uint8)std::clamp(settings.replicantColorRed, 0, 255),
        (uint8)std::clamp(settings.replicantColorGreen, 0, 255),
        (uint8)std::clamp(settings.replicantColorBlue, 0, 255),
        (uint8)std::clamp(settings.replicantColorAlpha, 0, 255)
    };
}

bool PlayerBarView::_ApplyReplicantAppearance(const BMessage* message)
{
    bool automatic;
    if (!message || message->FindBool("appearance_automatic", &automatic)
            != B_OK) {
        if (!message || message->FindBool("automatic", &automatic) != B_OK)
            return false;
    }

    fUseAutomaticReplicantColor = automatic;
    fReplicantColor = (rgb_color) {
        (uint8)std::clamp(message->GetInt32("appearance_red",
            message->GetInt32("red", 255)), 0, 255),
        (uint8)std::clamp(message->GetInt32("appearance_green",
            message->GetInt32("green", 255)), 0, 255),
        (uint8)std::clamp(message->GetInt32("appearance_blue",
            message->GetInt32("blue", 255)), 0, 255),
        (uint8)std::clamp(message->GetInt32("appearance_alpha",
            message->GetInt32("alpha", 255)), 0, 255)
    };
    if (Window())
        _ApplyBackgroundColors();
    return true;
}

void PlayerBarView::_ApplyBackgroundColors() {
    fReplicantColorRefreshPending = false;
    bool transparent = fIsReplicant;
    rgb_color background = transparent
        ? B_TRANSPARENT_COLOR : ui_color(B_PANEL_BACKGROUND_COLOR);
    rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
    if (transparent) {
        textColor = fUseAutomaticReplicantColor
            ? _ReplicantTextColorFor(this) : fReplicantColor;
        fActiveReplicantColor = textColor;
    }

    _SetTransparentBackground(this, transparent);
    SetFlags(transparent
        ? Flags() | B_DRAW_ON_CHILDREN
        : Flags() & ~B_DRAW_ON_CHILDREN);
    SetViewColor(background);
    SetLowColor(_LowColorFor(transparent, background, textColor));
    SetHighColor(textColor);

    if (fTrackInfoView)
        fTrackInfoView->SetTransparentBackground(transparent, textColor);
    if (fPositionView)
        fPositionView->SetTransparentBackground(transparent, textColor);
    if (fDurationView)
        fDurationView->SetTransparentBackground(transparent, textColor);
    if (fVolumeLabel)
        fVolumeLabel->SetTransparentBackground(transparent, textColor);
    if (fSeekBar)
        fSeekBar->SetTransparentBackground(transparent);
    _ApplySeekBarColors();

    _SetControlBackground(fAddTrackButton, background, transparent, textColor);
    _SetControlBackground(fShuffleButton, background, transparent, textColor);
    _SetControlBackground(fPrevButton, background, transparent, textColor);
    _SetControlBackground(fPlayButton, background, transparent, textColor);
    _SetControlBackground(fNextButton, background, transparent, textColor);
    _SetControlBackground(fRepeatButton, background, transparent, textColor);
    _SetSliderBackground(fVolumeSlider, transparent, textColor);
    _SetControlBackground(fDragger, background, transparent, textColor);

    Invalidate();
}

void PlayerBarView::_ApplySeekBarColors()
{
    if (!fSeekBar)
        return;

    rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
    rgb_color background = panel;
    rgb_color fill = fUseSystemSeekBarColor
        ? ui_color(B_CONTROL_HIGHLIGHT_COLOR) : fSeekBarColor;
    fSeekBar->SetColors(background, fill);
	if (fVolumeSlider) {
		fVolumeSlider->UseFillColor(true, &fill);
		fVolumeSlider->Invalidate();
	}
}

void PlayerBarView::Draw(BRect) {
    BRect bounds = Bounds();
    rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);

    if (!fIsReplicant) {
        SetHighColor(panel);
        FillRect(bounds);
    }
}


void PlayerBarView::DrawAfterChildren(BRect)
{
    if (!fIsReplicant)
        return;

    rgb_color borderColor = fActiveReplicantColor;
    if (fUseAutomaticReplicantColor) {
        borderColor = _ReplicantTextColorFor(this);
        if (!_ColorsEqual(borderColor, fActiveReplicantColor)
                && !fReplicantColorRefreshPending && Window()) {
            fReplicantColorRefreshPending = true;
            Window()->PostMessage(kMsgRefreshReplicantColor, this);
        }
    }

    SetDrawingMode(B_OP_ALPHA);
    SetHighColor(borderColor);
    StrokeRect(Bounds());
    SetDrawingMode(B_OP_COPY);
}


void PlayerBarView::FrameMoved(BPoint newPosition)
{
    BView::FrameMoved(newPosition);
    if (fIsReplicant && fUseAutomaticReplicantColor)
        Invalidate();
}

void PlayerBarView::DoLayout() {
    BView::DoLayout();
    if (fDragger)
        fDragger->MoveBy(-kDraggerRightInset, -kDraggerBottomInset);
}

void PlayerBarView::MouseDown(BPoint where) {
    if (!fIsReplicant) {
        BView::MouseDown(where);
        return;
    }

    BMessage* current = Window() ? Window()->CurrentMessage() : nullptr;
    int32 buttons = 0;
    if (current)
        current->FindInt32("buttons", &buttons);
    if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
        BPoint screenWhere = where;
        ConvertToScreen(&screenWhere);
        _ShowContextMenu(screenWhere);
        return;
    }

    BView::MouseDown(where);
}

void PlayerBarView::_ShowContextMenu(BPoint screenWhere) {
    BPopUpMenu* menu = new BPopUpMenu("Haify Replicant", false, false);
    menu->SetAsyncAutoDestruct(true);
    bool haifyRunning = BMessenger(HAIFY_MIME_SIG).IsValid();

    menu->AddItem(new BMenuItem(B_TRANSLATE("Player"),
        new BMessage(MSG_SHOW_PLAYER_WINDOW)));
    menu->AddItem(new BMenuItem(B_TRANSLATE("Artwork"),
        new BMessage(MSG_OPEN_ARTWORK)));
    menu->AddItem(new BMenuItem(B_TRANSLATE("Search"),
        new BMessage(MSG_OPEN_SEARCH)));
    menu->AddItem(new BMenuItem(B_TRANSLATE("Discover"),
        new BMessage(MSG_OPEN_BROWSER)));
    menu->AddItem(new BMenuItem(B_TRANSLATE("Queue"),
        new BMessage(MSG_OPEN_QUEUE)));
    menu->AddSeparatorItem();
    menu->AddItem(new BMenuItem(B_TRANSLATE("Settings"),
        new BMessage(MSG_OPEN_SETTINGS)));
    menu->AddItem(new BMenuItem(
        haifyRunning ? B_TRANSLATE("Quit Haify") : B_TRANSLATE("Start Haify"),
        new BMessage(haifyRunning ? MSG_QUIT_APP : MSG_SHOW_PLAYER_WINDOW)));

    menu->SetTargetForItems(BMessenger(this));
    menu->Go(screenWhere, true, false, true);
}

void PlayerBarView::_ForwardMessage(BMessage* message) {
    if (fIsReplicant) {
        if (!fTarget.IsValid())
            fTarget = BMessenger(HAIFY_MIME_SIG);
        if (fTarget.IsValid())
            fTarget.SendMessage(message);
        else {
            fRegistered = false;
            be_roster->Launch(HAIFY_MIME_SIG);
        }
    } else {
        BMessenger(nullptr, Window()).SendMessage(message);
    }
}

void PlayerBarView::MessageReceived(BMessage* msg) {
    switch (msg->what) {
        case B_COLORS_UPDATED:
        {
            _ApplyBackgroundColors();
            break;
        }

        case kMsgRefreshReplicantColor:
            _ApplyBackgroundColors();
            break;

        case kMsgTick:
        {
            if (fIsReplicant) {
                if (!_UpdateReplicantAvailability())
                    break;
                if (!fRegistered || !fTarget.IsValid())
                    _RegisterReplicant();
            }
            if (fIsPlaying && fDurationUs > 0 && fLastSyncUs > 0) {
                bigtime_t elapsed = system_time() - fLastSyncUs;
                bigtime_t pos = fLastPosUs + elapsed;
                if (pos > fDurationUs) pos = fDurationUs;
                _UpdatePlaybackPosition(pos, fDurationUs);
            }
            break;
        }

        case MSG_REPLICANT_STATE:
        {
            if (fIsReplicant && !fRegistered)
                _RegisterReplicant();
            _ApplyReplicantAppearance(msg);
            SetTrack(msg->GetString("title", ""), msg->GetString("artist", ""));
            SetTrackUri(msg->GetString("track_uri", ""));
            SetTrackIds(msg->GetString("album_id", ""), msg->GetString("artist_id", ""));
            SetPlaying(msg->GetBool("is_playing", false));
            int32 pos = msg->GetInt32("progress_ms", 0);
            int32 dur = msg->GetInt32("duration_ms", 0);
            SetPosition((bigtime_t)pos * 1000LL, (bigtime_t)dur * 1000LL);
            int32 vol = msg->GetInt32("volume_percent", -1);
            if (vol >= 0) SetVolume(vol);
            SetShuffle(msg->GetBool("shuffle_state", false));
            SetRepeat(msg->GetString("repeat_state", "off"));

            break;
        }

        case MSG_REPLICANT_APPEARANCE_CHANGED:
            _ApplyReplicantAppearance(msg);
            break;

        case MSG_SEEKBAR_COLOR_CHANGED:
        {
            rgb_color color = {
                (uint8)msg->GetInt32("red", fSeekBarColor.red),
                (uint8)msg->GetInt32("green", fSeekBarColor.green),
                (uint8)msg->GetInt32("blue", fSeekBarColor.blue),
                (uint8)msg->GetInt32("alpha", fSeekBarColor.alpha)
            };
            SetSeekBarColor(msg->GetBool("use_system", false), color);
            break;
        }

        case MSG_SEEKBAR_COLOR_DROPPED:
            _ForwardMessage(msg);
            break;

        case MSG_SHOW_REPLICANT_MENU:
        {
            if (!fIsReplicant)
                break;
            BPoint screenWhere;
            if (msg->FindPoint("screen_where", &screenWhere) == B_OK)
                _ShowContextMenu(screenWhere);
            break;
        }

        case MSG_SHOW_PLAYER_WINDOW:
        case MSG_OPEN_ARTWORK:
        case MSG_OPEN_BROWSER:
        case MSG_OPEN_QUEUE:
        case MSG_OPEN_SEARCH:
        case MSG_OPEN_SETTINGS:
        case MSG_QUIT_APP:
            _ForwardMessage(msg);
            break;

        case MSG_SHOW_ADD_TRACK_MENU:
        {
            if (fCurrentTrackUri.empty())
                break;
            BMessage open(MSG_SHOW_ADD_TRACK_MENU);
            open.AddString("trackUri", fCurrentTrackUri.c_str());
            if (fAddTrackButton) {
                BPoint where(fAddTrackButton->Bounds().left,
                    fAddTrackButton->Bounds().bottom + 1.0f);
                fAddTrackButton->ConvertToScreen(&where);
                open.AddPoint("screen_where", where);
            }
            _ForwardMessage(&open);
            break;
        }

        case MSG_PLAY_PAUSE:
        {
            if (fIsReplicant && !_UpdateReplicantAvailability()) {
                _ForwardMessage(msg);
                break;
            }
            SetPlaying(!fIsPlaying);
            if (fIsReplicant) {
                if (!fTarget.IsValid())
                    fTarget = BMessenger(HAIFY_MIME_SIG);
                if (fTarget.IsValid())
                    fTarget.SendMessage(msg);
                else {
                    fRegistered = false;
                    be_roster->Launch(HAIFY_MIME_SIG);
                }
            } else {
                BMessenger(nullptr, Window()).SendMessage(msg);
            }
            break;
        }

        case MSG_TOGGLE_SHUFFLE:
        {
            if (fIsReplicant && !_UpdateReplicantAvailability()) {
                _ForwardMessage(msg);
                break;
            }
            SetShuffle(!fShuffleOn);
            if (fIsReplicant) {
                if (!fTarget.IsValid())
                    fTarget = BMessenger(HAIFY_MIME_SIG);
                if (fTarget.IsValid())
                    fTarget.SendMessage(msg);
                else {
                    fRegistered = false;
                    be_roster->Launch(HAIFY_MIME_SIG);
                }
            } else {
                BMessenger(nullptr, Window()).SendMessage(msg);
            }
            break;
        }

        case MSG_TOGGLE_REPEAT:
        {
            if (fIsReplicant && !_UpdateReplicantAvailability()) {
                _ForwardMessage(msg);
                break;
            }
            if (fRepeatState == "off")          SetRepeat("context");
            else if (fRepeatState == "context") SetRepeat("track");
            else                                SetRepeat("off");
            if (fIsReplicant) {
                if (!fTarget.IsValid())
                    fTarget = BMessenger(HAIFY_MIME_SIG);
                if (fTarget.IsValid())
                    fTarget.SendMessage(msg);
                else {
                    fRegistered = false;
                    be_roster->Launch(HAIFY_MIME_SIG);
                }
            } else {
                BMessenger(nullptr, Window()).SendMessage(msg);
            }
            break;
        }

        case MSG_TOGGLE_MUTE:
            _ForwardMessage(msg);
            break;

        case MSG_SHOW_ALBUM:
        {
            if (fCurrentAlbumId.empty())
                break;
            BMessage open(MSG_SHOW_ALBUM);
            open.AddString("id", fCurrentAlbumId.c_str());
            if (fIsReplicant) {
                if (!fTarget.IsValid())
                    fTarget = BMessenger(HAIFY_MIME_SIG);
                if (fTarget.IsValid())
                    fTarget.SendMessage(&open);
                else {
                    fRegistered = false;
                    be_roster->Launch(HAIFY_MIME_SIG);
                }
            } else {
                BMessenger(nullptr, Window()).SendMessage(&open);
            }
            break;
        }

        case MSG_SHOW_ARTIST:
        {
            if (fCurrentArtistId.empty())
                break;
            BMessage open(MSG_SHOW_ARTIST);
            open.AddString("id", fCurrentArtistId.c_str());
            if (fIsReplicant) {
                if (!fTarget.IsValid())
                    fTarget = BMessenger(HAIFY_MIME_SIG);
                if (fTarget.IsValid())
                    fTarget.SendMessage(&open);
                else {
                    fRegistered = false;
                    be_roster->Launch(HAIFY_MIME_SIG);
                }
            } else {
                BMessenger(nullptr, Window()).SendMessage(&open);
            }
            break;
        }

        case MSG_NEXT_TRACK:
        case MSG_PREV_TRACK:
        case MSG_SET_VOLUME:
        case MSG_SEEK_REQUEST:
        {
            if (msg->what == MSG_SET_VOLUME) {
                int32 volume = 0;
                if (msg->FindInt32("be:value", &volume) == B_OK)
                    SetVolume(volume);
                else
                    _UpdateVolumeIcon();
            }
            if (fIsReplicant) {
                if (!fTarget.IsValid())
                    fTarget = BMessenger(HAIFY_MIME_SIG);
                if (fTarget.IsValid())
                    fTarget.SendMessage(msg);
                else {
                    fRegistered = false;
                    be_roster->Launch(HAIFY_MIME_SIG);
                }
            } else {
                BMessenger(nullptr, Window()).SendMessage(msg);
            }
            break;
        }

        default:
            BView::MessageReceived(msg);
            break;
    }
}



void PlayerBarView::SetTrack(const char* title, const char* artist) {
    if (fTrackInfoView)
        fTrackInfoView->SetTrack(title, artist);
}

void PlayerBarView::SetTrackUri(const char* trackUri) {
    fCurrentTrackUri = trackUri ? trackUri : "";
    if (fAddTrackButton)
        fAddTrackButton->SetEnabled(
            fCurrentTrackUri.find("spotify:track:") == 0);
}

void PlayerBarView::SetTrackIds(const char* albumId, const char* artistId) {
    fCurrentAlbumId  = albumId  ? albumId  : "";
    fCurrentArtistId = artistId ? artistId : "";
    if (fTrackInfoView)
        fTrackInfoView->SetTrackIds(albumId, artistId);
}

void PlayerBarView::SetArtwork(BBitmap*) {
}

void PlayerBarView::SetPlaying(bool playing) {
    fIsPlaying = playing;
    fLastSyncUs = system_time();
    if (fPlayButton) {
        fPlayButton->SetValue(B_CONTROL_OFF);
        BBitmap* icon = fIsPlaying ? fIcoPause : fIcoPlay;
        if (icon)
            _ApplyButtonIcon(fPlayButton, icon);
        else
            fPlayButton->SetLabel(fIsPlaying ? "||" : ">");
    }
}

void PlayerBarView::SetPosition(bigtime_t pos, bigtime_t duration) {
    fLastPosUs  = pos;
    fLastSyncUs = system_time();
    fDurationUs = duration;
    _UpdatePlaybackPosition(pos, duration);
}

void PlayerBarView::SetVolume(int32 volume) {
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    if (fVolumeSlider && fVolumeSlider->Value() != volume)
        fVolumeSlider->SetValue(volume);
    _UpdateVolumeIcon();
}

void PlayerBarView::SetShuffle(bool on) {
    fShuffleOn = on;
    if (fShuffleButton) {
        fShuffleButton->SetValue(B_CONTROL_OFF);
        BBitmap* icon = on ? fIcoShuffleActive : fIcoShuffle;
        if (icon)
            _ApplyButtonIcon(fShuffleButton, icon);
    }
}

void PlayerBarView::SetRepeat(const char* mode) {
    fRepeatState = mode ? mode : "off";
    if (fRepeatButton) {
        fRepeatButton->SetValue(B_CONTROL_OFF);
        BBitmap* icon = fIcoRepeat;
        if (fRepeatState == "track")
            icon = fIcoRepeatTrack;
        else if (fRepeatState == "context")
            icon = fIcoRepeatContext;

        if (icon)
            _ApplyButtonIcon(fRepeatButton, icon);
        else
            fRepeatButton->SetLabel(fRepeatState == "track" ? "1" : "R");
    }
}

void PlayerBarView::SetSeekBarColor(bool useSystemColor, rgb_color color)
{
    fUseSystemSeekBarColor = useSystemColor;
    fSeekBarColor = color;
    _ApplySeekBarColors();
}

void PlayerBarView::_UpdatePlaybackPosition(bigtime_t pos, bigtime_t duration) {
    if (duration < 0)
        duration = 0;
    if (pos < 0)
        pos = 0;
    if (duration > 0 && pos > duration)
        pos = duration;

    if (fSeekBar) {
        fSeekBar->SetDuration(duration);
        fSeekBar->SetPosition(pos);
    }
    _UpdateTimeLabels(pos, duration);
}

void PlayerBarView::_UpdateTimeLabels(bigtime_t pos, bigtime_t duration) {
    BString position = _FormatTime(pos);
    BString total = _FormatTime(duration);

    if (fPositionView)
        fPositionView->SetText(position.String());
    if (fDurationView)
        fDurationView->SetText(total.String());
}
