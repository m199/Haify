#include "ArtworkReplicantView.h"
#include "ArtworkWindow.h"
#include "Config.h"

#include "Messages.h"
#include "NowPlayingFields.h"
#include "SettingsController.h"

#include <algorithm>
#include <Application.h>
#include <Archivable.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <Dragger.h>
#include <InterfaceDefs.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Window.h>

#include <string>
#include <string.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ArtworkReplicantView"

static const uint32 kMsgReloadArtwork = 'rArt';
static const uint32 kMsgRegisterRetry = 'rTry';
static const float kDefaultArtworkSize = 220.0f;
static const float kMinArtworkSize = 96.0f;
static const float kDraggerSize = 8.0f;
static const rgb_color kBlack = { 0, 0, 0, 255 };
static const rgb_color kWhite = { 255, 255, 255, 255 };


static rgb_color
ReplicantBorderColorFor(BView* view)
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


ArtworkReplicantView::ArtworkReplicantView()
    :
    ArtworkView("ArtworkReplicantView")
{
    _Init(true);
}


ArtworkReplicantView::ArtworkReplicantView(BMessage* archive)
    :
    ArtworkView(archive),
    fIsReplicant(true)
{
    _Init(false);

    const char* url = "";
    if (archive->FindString("artwork_url", &url) == B_OK)
        fArtworkUrl = url;

    _ApplyAppearance(archive);

    float width = 0.0f;
    float height = 0.0f;
    if (archive->FindFloat("artwork_width", &width) == B_OK
            && archive->FindFloat("artwork_height", &height) == B_OK
            && width > 0.0f && height > 0.0f) {
        ResizeTo(width - 1.0f, height - 1.0f);
        SetExplicitPreferredSize(BSize(width, height));
    }
}


ArtworkReplicantView::~ArtworkReplicantView()
{
}


void
ArtworkReplicantView::_Init(bool useDefaultSize)
{
	_LoadAppearanceSettings();
    SetFlags(Flags() | B_WILL_DRAW | B_FRAME_EVENTS
        | B_FULL_UPDATE_ON_RESIZE);
    SetExplicitMinSize(BSize(kMinArtworkSize, kMinArtworkSize));

    if (useDefaultSize) {
        SetExplicitPreferredSize(BSize(kDefaultArtworkSize,
            kDefaultArtworkSize));
        ResizeTo(kDefaultArtworkSize - 1.0f, kDefaultArtworkSize - 1.0f);
    } else {
        BSize preferred(Bounds().Width() + 1.0f, Bounds().Height() + 1.0f);
        if (preferred.width <= 1.0f || preferred.height <= 1.0f)
            preferred = BSize(kDefaultArtworkSize, kDefaultArtworkSize);
        SetExplicitPreferredSize(preferred);
    }

    fDragger = new BDragger(this);
    fDragger->ResizeTo(kDraggerSize - 1.0f, kDraggerSize - 1.0f);
    AddChild(fDragger);
    _LayoutDragger();
    _ApplyBackground();
}


void
ArtworkReplicantView::_LoadAppearanceSettings()
{
    HaifySettings settings = SettingsController::Load();
    fUseAutomaticColor = settings.replicantUseAutomaticColor;
    fCustomColor = (rgb_color) {
        (uint8)std::clamp(settings.replicantColorRed, 0, 255),
        (uint8)std::clamp(settings.replicantColorGreen, 0, 255),
        (uint8)std::clamp(settings.replicantColorBlue, 0, 255),
        (uint8)std::clamp(settings.replicantColorAlpha, 0, 255)
    };
}


bool
ArtworkReplicantView::_ApplyAppearance(const BMessage* message)
{
    bool automatic;
    if (!message || message->FindBool("appearance_automatic", &automatic)
            != B_OK) {
        if (!message || message->FindBool("automatic", &automatic) != B_OK)
            return false;
    }

    fUseAutomaticColor = automatic;
    fCustomColor = (rgb_color) {
        (uint8)std::clamp(message->GetInt32("appearance_red",
            message->GetInt32("red", 255)), 0, 255),
        (uint8)std::clamp(message->GetInt32("appearance_green",
            message->GetInt32("green", 255)), 0, 255),
        (uint8)std::clamp(message->GetInt32("appearance_blue",
            message->GetInt32("blue", 255)), 0, 255),
        (uint8)std::clamp(message->GetInt32("appearance_alpha",
            message->GetInt32("alpha", 255)), 0, 255)
    };
    Invalidate();
    return true;
}


rgb_color
ArtworkReplicantView::_AppearanceColor()
{
    return fUseAutomaticColor ? ReplicantBorderColorFor(this) : fCustomColor;
}


BArchivable*
ArtworkReplicantView::Instantiate(BMessage* archive)
{
    if (!validate_instantiation(archive, "ArtworkReplicantView"))
        return nullptr;
    return new ArtworkReplicantView(archive);
}


status_t
ArtworkReplicantView::Archive(BMessage* archive, bool) const
{
    status_t status = BView::Archive(archive, false);
    if (status != B_OK)
        return status;

    // Tracker otherwise keeps the first loaded image for this signature and
    // newly dropped replicants can continue running stale code after a rebuild.
    archive->AddString("class", "ArtworkReplicantView");
    archive->AddString("add_on", HAIFY_MIME_SIG);
    archive->AddInt32("be:add_on_version", 5);
    archive->AddBool("be:load_each_time", true);
    archive->AddBool("be:unload_on_delete", true);
    archive->AddInt32("version", 5);
    archive->AddString("artwork_url", fArtworkUrl.String());
    archive->AddBool("appearance_automatic", fUseAutomaticColor);
    archive->AddInt32("appearance_red", fCustomColor.red);
    archive->AddInt32("appearance_green", fCustomColor.green);
    archive->AddInt32("appearance_blue", fCustomColor.blue);
    archive->AddInt32("appearance_alpha", fCustomColor.alpha);
    archive->AddFloat("artwork_width", Bounds().Width() + 1.0f);
    archive->AddFloat("artwork_height", Bounds().Height() + 1.0f);
    return B_OK;
}


void
ArtworkReplicantView::SetRegisterForUpdates(bool enabled)
{
    fRegisterForUpdates = enabled;
    if (!Window())
        return;

    if (enabled || fIsReplicant)
        _Register();
    else
        _Unregister();
}


void
ArtworkReplicantView::SetArtworkUrl(const char* url)
{
    BString nextUrl(url ? url : "");
    fArtworkUrl = nextUrl;
    LoadUrl(fArtworkUrl.String());
}


void
ArtworkReplicantView::_RequestArtwork(bool reload)
{
    if (fArtworkUrl.IsEmpty())
        return;
    if (reload)
        ReloadUrl();
    else
        LoadUrl(fArtworkUrl.String());
}


void
ArtworkReplicantView::AttachedToWindow()
{
    ArtworkView::AttachedToWindow();

    // The normal in-app view always lives in ArtworkWindow. Tracker hosts the
    // archived desktop replicant in a different BWindow, which is a more
    // reliable distinction than be_app's signature during add-on loading.
    fIsReplicant = dynamic_cast<ArtworkWindow*>(Window()) == nullptr;

    _ApplyBackground();

    fTarget = BMessenger(HAIFY_MIME_SIG);
    if (fIsReplicant || fRegisterForUpdates) {
        _Register();
        if (!fRegisterTimer) {
            BMessage retry(kMsgRegisterRetry);
            fRegisterTimer = new BMessageRunner(BMessenger(this), &retry,
                500000LL);
        }
    }

    _KeepInsideParent();

    if (!fArtworkUrl.IsEmpty()) {
        LoadUrl(fArtworkUrl.String());
    }
}


void
ArtworkReplicantView::DetachedFromWindow()
{
    delete fRegisterTimer;
    fRegisterTimer = nullptr;
    _Unregister();
    ArtworkView::DetachedFromWindow();
}


void
ArtworkReplicantView::_Register()
{
    if (fRegistered && !fTarget.IsValid())
        fRegistered = false;
    if (fRegistered)
        return;
    if (!fTarget.IsValid())
        fTarget = BMessenger(HAIFY_MIME_SIG);
    if (!fTarget.IsValid())
        return;

    BMessage message(MSG_REGISTER_REPLICANT);
    message.AddMessenger("messenger", BMessenger(this));
    message.AddBool("external", fIsReplicant);
    if (fTarget.SendMessage(&message) == B_OK)
        fRegistered = true;
}


void
ArtworkReplicantView::_Unregister()
{
    if (!fRegistered) {
        fRegistered = false;
        return;
    }

    if (fTarget.IsValid()) {
        BMessage message(MSG_UNREGISTER_REPLICANT);
        message.AddMessenger("messenger", BMessenger(this));
        fTarget.SendMessage(&message);
    }
    fRegistered = false;
}


void
ArtworkReplicantView::Draw(BRect updateRect)
{
    bool hasArtwork = HasBitmap();
    if (!fIsReplicant || hasArtwork)
        ArtworkView::Draw(updateRect);
}


void
ArtworkReplicantView::DrawAfterChildren(BRect)
{
    bool hasArtwork = HasBitmap();

    rgb_color border;
    if (fIsReplicant)
        border = _AppearanceColor();
    else
        border = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
            B_DARKEN_3_TINT);

    SetDrawingMode(B_OP_ALPHA);
    if (fIsReplicant && !hasArtwork) {
        const char* text = B_TRANSLATE("No Cover");
        if (State() == kLoading)
            text = B_TRANSLATE("Loading...");
        else if (State() == kLoadFailed)
            text = B_TRANSLATE("Load failed");
        font_height height;
        GetFontHeight(&height);
        BRect bounds = Bounds();
        BPoint position(
            bounds.left + (bounds.Width() - StringWidth(text)) / 2.0f,
            bounds.top + (bounds.Height()
                + height.ascent - height.descent) / 2.0f);
        SetHighColor(border);
        DrawString(text, position);
    }

    SetHighColor(border);
    StrokeRect(Bounds());
    SetDrawingMode(B_OP_COPY);
}


void
ArtworkReplicantView::FrameResized(float width, float height)
{
    ArtworkView::FrameResized(width, height);
    _LayoutDragger();
    _KeepInsideParent();
}


void
ArtworkReplicantView::_LayoutDragger()
{
    if (!fDragger)
        return;

    BRect bounds = Bounds();
    fDragger->MoveTo(bounds.right - kDraggerSize + 1.0f,
        bounds.bottom - kDraggerSize + 1.0f);
}


void
ArtworkReplicantView::_KeepInsideParent()
{
    if (!fIsReplicant || !Parent())
        return;

    BRect parent = Parent()->Bounds();
    BRect frame = Frame();
    float x = frame.left;
    float y = frame.top;

    if (frame.right > parent.right)
        x = parent.right - frame.Width();
    if (frame.bottom > parent.bottom)
        y = parent.bottom - frame.Height();
    if (x < parent.left)
        x = parent.left;
    if (y < parent.top)
        y = parent.top;

    if (x != frame.left || y != frame.top)
        MoveTo(x, y);
}


void
ArtworkReplicantView::_ApplyBackground()
{
    bool transparent = fIsReplicant && !HasBitmap();
    uint32 flags = Flags();
    flags = transparent
        ? flags | B_TRANSPARENT_BACKGROUND
        : flags & ~B_TRANSPARENT_BACKGROUND;
    flags = fIsReplicant
        ? flags | B_DRAW_ON_CHILDREN
        : flags & ~B_DRAW_ON_CHILDREN;
    SetFlags(flags);

    rgb_color background = transparent
        ? B_TRANSPARENT_COLOR : ui_color(B_PANEL_BACKGROUND_COLOR);
    SetViewColor(background);
    SetLowColor(background);

    if (fDragger) {
        fDragger->SetFlags(transparent
            ? fDragger->Flags() | B_TRANSPARENT_BACKGROUND
            : fDragger->Flags() & ~B_TRANSPARENT_BACKGROUND);
        fDragger->SetViewColor(background);
        fDragger->SetLowColor(background);
        fDragger->Invalidate();
    }

    // A transparent child does not erase pixels that were previously drawn
    // by the opaque artwork/placeholder. Redraw the parent first so Tracker's
    // desktop background becomes visible instead of leaving a grey remnant.
    if (transparent && Parent())
        Parent()->Invalidate(Frame());
    Invalidate();
}


void
ArtworkReplicantView::ArtworkStateChanged()
{
    _ApplyBackground();
}


void
ArtworkReplicantView::MouseDown(BPoint where)
{
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

    if (!fOpenUri.IsEmpty()) {
        BMessage open('open');
        open.AddString("uri", fOpenUri.String());
        open.AddString("title", fTitle.String());
        _ForwardMessage(&open);
        return;
    }

    ArtworkView::MouseDown(where);
}


void
ArtworkReplicantView::_ShowContextMenu(BPoint screenWhere)
{
    BPopUpMenu* menu = new BPopUpMenu("Haify Artwork", false, false);
    menu->SetAsyncAutoDestruct(true);
    bool haifyRunning = BMessenger(HAIFY_MIME_SIG).IsValid();

    BMenuItem* reloadItem = new BMenuItem(B_TRANSLATE("Reload Artwork"),
        new BMessage(kMsgReloadArtwork));
    reloadItem->SetEnabled(!fArtworkUrl.IsEmpty());
    menu->AddItem(reloadItem);
    menu->AddSeparatorItem();
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


void
ArtworkReplicantView::_ForwardMessage(BMessage* message)
{
    if (!fTarget.IsValid())
        fTarget = BMessenger(HAIFY_MIME_SIG);
    if (fTarget.IsValid())
        fTarget.SendMessage(message);
    else {
        fRegistered = false;
        be_roster->Launch(HAIFY_MIME_SIG);
    }
}


void
ArtworkReplicantView::_ApplyReplicantStateMessage(BMessage* message)
{
    if (!fRegistered)
        _Register();
    fTitle = message->GetString("title", "");
    fArtist = message->GetString("artist", "");
    const char* openUri = message->GetString(
        kNowPlayingPrimaryOpenUriField, "");
    if (!openUri || !openUri[0])
        openUri = message->GetString("track_uri", "");
    fOpenUri = openUri;
    _ApplyAppearance(message);
    SetArtworkUrl(message->GetString("artwork_url", ""));
}


void
ArtworkReplicantView::_RetryRegister()
{
    if ((fIsReplicant || fRegisterForUpdates)
            && (!fRegistered || !fTarget.IsValid()))
        _Register();
}


void
ArtworkReplicantView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_REPLICANT_STATE:
            _ApplyReplicantStateMessage(message);
            break;

        case kMsgRegisterRetry:
            _RetryRegister();
            break;

        case kMsgReloadArtwork:
            _RequestArtwork(true);
            break;

        case MSG_REPLICANT_APPEARANCE_CHANGED:
            _ApplyAppearance(message);
            break;

        case B_COLORS_UPDATED:
            _ApplyBackground();
            break;

        case MSG_SHOW_PLAYER_WINDOW:
        case MSG_OPEN_ARTWORK:
        case MSG_OPEN_BROWSER:
        case MSG_OPEN_QUEUE:
        case MSG_OPEN_SEARCH:
        case MSG_OPEN_SETTINGS:
        case MSG_QUIT_APP:
            _ForwardMessage(message);
            break;

        default:
            ArtworkView::MessageReceived(message);
            break;
    }
}


BSize
ArtworkReplicantView::MinSize()
{
    return BSize(kMinArtworkSize, kMinArtworkSize);
}


BSize
ArtworkReplicantView::MaxSize()
{
    return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


BSize
ArtworkReplicantView::PreferredSize()
{
    return BSize(kDefaultArtworkSize, kDefaultArtworkSize);
}
