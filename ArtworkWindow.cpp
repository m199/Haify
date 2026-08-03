#include "ArtworkWindow.h"

#include "App.h"
#include "ArtworkReplicantView.h"
#include "SettingsController.h"

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Screen.h>
#include <algorithm>
#include <cmath>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ArtworkWindow"

static const float kMinArtworkWindowSize = 120.0f;


ArtworkWindow::ArtworkWindow()
    :
    BWindow(BRect(260, 140,
        260 + kDefaultArtworkWindowSize,
        140 + kDefaultArtworkWindowSize), B_TRANSLATE("Artwork"),
        B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS)
{
    fArtworkView = new ArtworkReplicantView();
    fArtworkView->SetRegisterForUpdates(true);

    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(0, 0, 0, 0)
        .Add(fArtworkView)
    .End();

    SetSizeLimits(kMinArtworkWindowSize, 100000.0f,
        kMinArtworkWindowSize, 100000.0f);

    HaifySettings s = SettingsController::Load();
    if (s.artworkWindowX >= 0)
        MoveTo(s.artworkWindowX, s.artworkWindowY);
    if (s.artworkWindowW > 0 && s.artworkWindowH > 0) {
        float size = std::max(s.artworkWindowW, s.artworkWindowH);
        ResizeTo(size, size);
    }
    _EnsureOnScreen();

    fLastWidth = Bounds().Width();
    fLastHeight = Bounds().Height();
}


ArtworkWindow::~ArtworkWindow()
{
    App* app = (App*)be_app;
    if (app && app->IsQuitting())
        return;
    _SaveFrame(!IsHidden());
}


bool
ArtworkWindow::QuitRequested()
{
    App* app = (App*)be_app;
    if (app && app->IsQuitting())
        return true;

    if (app)
        app->SetArtworkWindowOpen(false);
    _SaveFrame(false);
    Hide();
    return false;
}


void
ArtworkWindow::FrameResized(float width, float height)
{
    BWindow::FrameResized(width, height);
    _EnforceSquareSize(width, height);
}


void
ArtworkWindow::SaveOpenState(bool open)
{
    App* app = (App*)be_app;
    if (app)
        app->SetArtworkWindowOpen(open);
    _SaveFrame(open);
}


void
ArtworkWindow::_EnforceSquareSize(float width, float height)
{
    if (fAdjustingSize)
        return;

    if (std::fabs(width - height) < 1.0f) {
        fLastWidth = width;
        fLastHeight = height;
        return;
    }

    float widthDelta = std::fabs(width - fLastWidth);
    float heightDelta = std::fabs(height - fLastHeight);
    float size = widthDelta >= heightDelta ? width : height;
    if (size < kMinArtworkWindowSize)
        size = kMinArtworkWindowSize;

    fAdjustingSize = true;
    ResizeTo(size, size);
    fAdjustingSize = false;
    fLastWidth = size;
    fLastHeight = size;
}


void
ArtworkWindow::_EnsureOnScreen()
{
    BScreen screen(this);
    if (!screen.IsValid())
        return;

    BRect screenFrame = screen.Frame();
    BRect frame = Frame();
    float x = frame.left;
    float y = frame.top;

    if (frame.right > screenFrame.right)
        x = screenFrame.right - frame.Width();
    if (frame.bottom > screenFrame.bottom)
        y = screenFrame.bottom - frame.Height();
    if (x < screenFrame.left)
        x = screenFrame.left;
    if (y < screenFrame.top)
        y = screenFrame.top;

    if (x != frame.left || y != frame.top)
        MoveTo(x, y);
}


void
ArtworkWindow::_SaveFrame(bool open)
{
	BRect frame = Frame();
	SettingsController::Update([&](HaifySettings& s) {
		s.artworkWindowOpen = open;
		s.artworkWindowX = frame.left;
		s.artworkWindowY = frame.top;
		s.artworkWindowW = frame.Width();
		s.artworkWindowH = frame.Height();
	});
}
