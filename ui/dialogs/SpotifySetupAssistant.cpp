#include "ui/dialogs/SpotifySetupAssistant.h"

#include "app/Config.h"
#include "messages/Messages.h"
#include "spotify/auth/SpotifyClientId.h"

#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <Font.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Url.h>

#include <algorithm>
#include <cstring>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpotifySetupAssistant"

static const uint32 kMsgBack = 'saBk';
static const uint32 kMsgNext = 'saNx';
static const uint32 kMsgOpenDashboard = 'saDb';
static const uint32 kMsgCopyRedirect = 'saCp';
static const uint32 kMsgClientIdChanged = 'saId';

static const char* kDashboardUrl = "https://developer.spotify.com/dashboard";


static BTextView*
MakeInstructionText(const char* text)
{
	BTextView* view = new BTextView("instructions");
	view->SetText(text);
	view->MakeEditable(false);
	view->MakeSelectable(false);
	view->SetWordWrap(true);
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	view->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
	view->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);
	view->SetExplicitMinSize(BSize(440, 140));
	return view;
}


SpotifySetupAssistant::SpotifySetupAssistant(BMessenger target,
	const std::string& currentClientId)
	:
	BWindow(BRect(0, 0, 500, 330), B_TRANSLATE("Spotify Setup"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target)
{
	fPages = new BCardLayout();
	BView* pages = new BView("pages", 0, fPages);

	fPages->AddView(_MakePage(B_TRANSLATE("Welcome to Haify"),
		B_TRANSLATE("Haify talks to Spotify through an app registration "
			"called a Client ID. Spotify allows each registration only a "
			"handful of users, so every Haify user creates a free one of "
			"their own.\n\n"
			"This takes about two minutes. You need your Spotify account; "
			"Spotify requires Premium for the account that owns the "
			"registration.\n\n"
			"Click Next to begin.")));
	fPages->AddView(_MakePage(
		B_TRANSLATE("Step 1: Open the Spotify Developer Dashboard"),
		B_TRANSLATE("Click the button below. Your web browser opens the "
			"Spotify Developer Dashboard.\n\n"
			"Log in with your Spotify account. If Spotify asks you to accept "
			"the Developer Terms, accept them.\n\n"
			"Then click \"Create app\" and come back here."),
		_MakeDashboardControls()));
	fPages->AddView(_MakePage(B_TRANSLATE("Step 2: Fill in the form"),
		B_TRANSLATE("App name: Haify (any name works)\n"
			"App description: Haify on Haiku (any text works)\n"
			"Redirect URI: copy the address below, paste it and click "
			"\"Add\"\n"
			"APIs used: tick \"Web API\"\n\n"
			"Accept the terms at the bottom of the form and click \"Save\"."),
		_MakeRedirectControls()));
	fPages->AddView(_MakePage(B_TRANSLATE("Step 3: Copy your Client ID"),
		B_TRANSLATE("Spotify now shows the page of your new app. Copy either "
			"the \"Client ID\" or simply the address from your browser's "
			"address bar, then come back here: Haify fills in the Client ID "
			"on its own. You can also paste it below.\n\n"
			"Haify does not need the client secret. Keep it to yourself."),
		_MakeClientIdControls(currentClientId)));
	fPages->AddView(_MakePage(B_TRANSLATE("Step 4: Sign in"),
		B_TRANSLATE("Click Finish. Your web browser opens the Spotify sign-in "
			"page. Allow Haify access, then return to Haify.\n\n"
			"If Spotify reports \"Invalid redirect URI\", go back to step 2 "
			"and compare the Redirect URI in your app's settings.")));

	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	fBack = new BButton("back", B_TRANSLATE("Back"), new BMessage(kMsgBack));
	fNext = new BButton("next", B_TRANSLATE("Next"), new BMessage(kMsgNext));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(pages, 1.0f)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(cancel)
			.AddGlue()
			.Add(fBack)
			.Add(fNext)
		.End()
	.End();

	// Same width as Next (which also reads "Finish" on the last page), so
	// Copy sits exactly above it. _UpdateControls() sets the real label.
	float buttonWidth = fCopy->PreferredSize().width;
	for (const char* label : {B_TRANSLATE("Next"), B_TRANSLATE("Finish")}) {
		fNext->SetLabel(label);
		buttonWidth = std::max(buttonWidth, fNext->PreferredSize().width);
	}
	fCopy->SetExplicitSize(BSize(buttonWidth, B_SIZE_UNSET));
	fNext->SetExplicitSize(BSize(buttonWidth, B_SIZE_UNSET));

	SetDefaultButton(fNext);
	_UpdateClientIdStatus();
	_UpdateControls();
	CenterOnScreen();
}


BView*
SpotifySetupAssistant::_MakePage(const char* title, const char* text,
	BView* extra)
{
	BStringView* heading = new BStringView("title", title);
	heading->SetFont(be_bold_font);

	BGroupView* page = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(page)
		.Add(heading)
		.Add(MakeInstructionText(text), 1.0f)
		.End();
	if (extra)
		page->GroupLayout()->AddView(extra);
	return page;
}


BView*
SpotifySetupAssistant::_MakeDashboardControls()
{
	BGroupView* controls = new BGroupView(B_HORIZONTAL);
	BLayoutBuilder::Group<>(controls)
		.Add(new BButton("dashboard",
			B_TRANSLATE("Open Spotify Dashboard"),
			new BMessage(kMsgOpenDashboard)))
		.AddGlue()
		.End();
	return controls;
}


BView*
SpotifySetupAssistant::_MakeRedirectControls()
{
	BTextControl* redirect = new BTextControl("redirect",
		B_TRANSLATE("Redirect URI:"), SPOTIFY_REDIRECT, nullptr);
	redirect->TextView()->MakeEditable(false);
	fCopyStatus = new BStringView("copyStatus", "");

	fCopy = new BButton("copy", B_TRANSLATE("Copy"),
		new BMessage(kMsgCopyRedirect));

	// Copy is the last item of its row, so it ends at the same right edge as
	// the Next button below; the status goes underneath instead of beside it.
	BGroupView* controls = new BGroupView(B_VERTICAL, B_USE_HALF_ITEM_SPACING);
	BLayoutBuilder::Group<>(controls)
		.AddGroup(B_HORIZONTAL)
			.Add(redirect, 1.0f)
			.Add(fCopy)
		.End()
		.Add(fCopyStatus)
		.End();
	return controls;
}


BView*
SpotifySetupAssistant::_MakeClientIdControls(
	const std::string& currentClientId)
{
	fClientId = new BTextControl("clientId", B_TRANSLATE("Client ID:"),
		currentClientId.c_str(), nullptr);
	fClientId->SetModificationMessage(new BMessage(kMsgClientIdChanged));
	fClientIdStatus = new BStringView("clientIdStatus", "");

	BGroupView* controls = new BGroupView(B_VERTICAL);
	BLayoutBuilder::Group<>(controls)
		.Add(fClientId)
		.Add(fClientIdStatus)
		.End();
	return controls;
}


// Accepts the ID itself or a pasted dashboard address that contains it.
std::string
SpotifySetupAssistant::_ClientId() const
{
	std::string text = fClientId ? fClientId->Text() : "";
	std::string extracted = ExtractSpotifyClientId(text);
	return extracted.empty() ? NormalizeSpotifyClientId(text) : extracted;
}


bool
SpotifySetupAssistant::_ClientIdValid() const
{
	return IsValidSpotifyClientId(_ClientId());
}


void
SpotifySetupAssistant::_UpdateControls()
{
	fPages->SetVisibleItem(fSteps.Index());
	bool valid = _ClientIdValid();
	fBack->SetEnabled(fSteps.CanGoBack());
	fNext->SetLabel(fSteps.IsLast() ? B_TRANSLATE("Finish")
		: B_TRANSLATE("Next"));
	fNext->SetEnabled(fSteps.IsLast() ? fSteps.CanFinish(valid)
		: fSteps.CanGoNext(valid));
	if (fSteps.Page() == SetupAssistantPage::EnterClientId)
		fClientId->MakeFocus(true);
}


void
SpotifySetupAssistant::_UpdateClientIdStatus()
{
	std::string clientId = _ClientId();
	if (clientId.empty()) {
		fClientIdStatus->SetText(B_TRANSLATE("Paste the Client ID here."));
	} else if (IsValidSpotifyClientId(clientId) && fFromClipboard) {
		fClientIdStatus->SetText(
			B_TRANSLATE("Client ID taken from the clipboard."));
	} else if (IsValidSpotifyClientId(clientId)) {
		fClientIdStatus->SetText(B_TRANSLATE("This Client ID looks right."));
	} else {
		fClientIdStatus->SetText(B_TRANSLATE("A Client ID has 32 characters: "
			"digits 0-9 and letters a-f."));
	}
}


void
SpotifySetupAssistant::_SetClientIdText(const std::string& clientId)
{
	fAppliedClientId = clientId;
	fClientId->SetText(clientId.c_str());
}


// A pasted dashboard address is replaced by the Client ID it contains.
void
SpotifySetupAssistant::_ApplyClientIdEdit()
{
	std::string text = NormalizeSpotifyClientId(fClientId->Text());
	if (text != fAppliedClientId) {
		fFromClipboard = false;
		std::string extracted = ExtractSpotifyClientId(text);
		if (!extracted.empty() && extracted != text)
			_SetClientIdText(extracted);
	}
	_UpdateClientIdStatus();
	_UpdateControls();
}


// The user copies the Client ID or the dashboard address in the browser and
// comes back; the ID is filled in without pasting. Only an empty or invalid
// field is replaced, never an ID the user typed.
bool
SpotifySetupAssistant::_TakeClientIdFromClipboard()
{
	if (_ClientIdValid())
		return false;

	std::string text;
	if (be_clipboard->Lock()) {
		BMessage* clip = be_clipboard->Data();
		const void* data = nullptr;
		ssize_t size = 0;
		if (clip && clip->FindData("text/plain", B_MIME_TYPE, &data, &size)
				== B_OK && data && size > 0 && size <= 4096) {
			text.assign(static_cast<const char*>(data), (size_t)size);
		}
		be_clipboard->Unlock();
	}

	std::string clientId = ExtractSpotifyClientId(text);
	if (clientId.empty())
		return false;
	_SetClientIdText(clientId);
	fFromClipboard = true;
	_UpdateClientIdStatus();
	_UpdateControls();
	return true;
}


void
SpotifySetupAssistant::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	if (active && fSteps.Page() == SetupAssistantPage::EnterClientId)
		_TakeClientIdFromClipboard();
}


void
SpotifySetupAssistant::_OpenDashboard()
{
	BUrl url(kDashboardUrl, false);
	url.OpenWithPreferredApplication(false);
}


void
SpotifySetupAssistant::_CopyRedirectUri()
{
	bool copied = false;
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		if (BMessage* clip = be_clipboard->Data()) {
			clip->AddData("text/plain", B_MIME_TYPE, SPOTIFY_REDIRECT,
				strlen(SPOTIFY_REDIRECT));
			copied = be_clipboard->Commit() == B_OK;
		}
		be_clipboard->Unlock();
	}
	fCopyStatus->SetText(copied ? B_TRANSLATE("Copied.")
		: B_TRANSLATE("Copy failed."));
}


void
SpotifySetupAssistant::_Finish()
{
	if (!fSteps.CanFinish(_ClientIdValid()))
		return;
	BMessage chosen(MSG_SPOTIFY_CLIENT_ID_CHOSEN);
	chosen.AddString(MessageFields::SpotifyClientId, _ClientId().c_str());
	fTarget.SendMessage(&chosen);
	PostMessage(B_QUIT_REQUESTED);
}


void
SpotifySetupAssistant::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgBack:
			fSteps.Back();
			_UpdateControls();
			return;
		case kMsgNext:
			if (fSteps.IsLast())
				_Finish();
			else if (fSteps.Next(_ClientIdValid())) {
				_UpdateControls();
				if (fSteps.Page() == SetupAssistantPage::EnterClientId)
					_TakeClientIdFromClipboard();
			}
			return;
		case kMsgOpenDashboard:
			_OpenDashboard();
			return;
		case kMsgCopyRedirect:
			_CopyRedirectUri();
			return;
		case kMsgClientIdChanged:
			_ApplyClientIdEdit();
			return;
		default:
			BWindow::MessageReceived(message);
			return;
	}
}
