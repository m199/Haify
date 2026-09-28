#pragma once

#include "ui/dialogs/SetupAssistantSteps.h"

#include <Messenger.h>
#include <Window.h>

#include <string>

class BButton;
class BCardLayout;
class BStringView;
class BTextControl;
class BView;

// Walks a user through registering an own Spotify app and collects its
// Client ID. It owns no settings or authentication: "Finish" sends
// MSG_SPOTIFY_CLIENT_ID_CHOSEN to the target (the App), which stores the ID
// and starts the sign-in. Page rules live in SetupAssistantSteps.
class SpotifySetupAssistant : public BWindow {
public:
							SpotifySetupAssistant(BMessenger target,
								const std::string& currentClientId);

	void					MessageReceived(BMessage* message) override;
	void					WindowActivated(bool active) override;

private:
	BView*					_MakePage(const char* title, const char* text,
								BView* extra = nullptr);
	BView*					_MakeDashboardControls();
	BView*					_MakeRedirectControls();
	BView*					_MakeClientIdControls(
								const std::string& currentClientId);
	std::string				_ClientId() const;
	bool					_ClientIdValid() const;
	void					_UpdateControls();
	void					_UpdateClientIdStatus();
	void					_ApplyClientIdEdit();
	bool					_TakeClientIdFromClipboard();
	void					_SetClientIdText(const std::string& clientId);
	void					_OpenDashboard();
	void					_CopyRedirectUri();
	void					_Finish();

	BMessenger				fTarget;
	SetupAssistantSteps		fSteps;
	BCardLayout*			fPages = nullptr;
	BButton*				fBack = nullptr;
	BButton*				fNext = nullptr;
	BButton*				fCopy = nullptr;
	BTextControl*			fClientId = nullptr;
	BStringView*			fClientIdStatus = nullptr;
	BStringView*			fCopyStatus = nullptr;
	// Last ID Haify wrote into the field itself (from a URL or clipboard).
	std::string				fAppliedClientId;
	bool					fFromClipboard = false;
};
