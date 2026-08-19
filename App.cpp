#include "App.h"
#include "Messages.h"
#include "PlayerWindow.h"
#include "ArtworkWindow.h"
#include "DiscoverWindow.h"
#include "DeskbarReplicantView.h"
#include "PlaylistWindow.h"
#include "ArtistWindow.h"
#include "EpisodeWindow.h"
#include "AudiobookWindow.h"
#include "QueueWindow.h"
#include "SearchWindow.h"
#include "SettingsWindow.h"

#include "Config.h"
#include "SettingsController.h"
#include "spotify/auth/SpotifyAuth.h"
#include "spotify/api/SpotifyApi.h"
#include "network/ImageCache.h"
#include "network/OAuthCallbackServer.h"

#include <Catalog.h>
#include <Autolock.h>
#include <Deskbar.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <Url.h>
#include <Alert.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#include "HaifyDebug.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "App"

static const uint32 kMsgTransferLibrespotPlayback = 'tlbp';
static const uint32 kMsgLibrespotDevicePollResult = 'ldpr';
static const uint32 kMsgLibrespotPlaybackDecision = 'lpbd';
static const uint32 kMsgRefreshAccessToken = 'rfrt';

bool gIsDebug = false;

static status_t OpenUrl(const std::string& url) {
    BUrl target(url.c_str(), false);
    return target.OpenWithPreferredApplication(false);
}

static void AddTokenResult(BMessage& message, const TokenResult& result)
{
	message.AddBool("ok", result.success);
	message.AddInt32("http_status", result.httpStatus);
	message.AddInt32("expires_in", result.expiresIn);
	message.AddString("access_token", result.accessToken.c_str());
	message.AddString("refresh_token", result.refreshToken.c_str());
	message.AddString("scopes", result.scopes.c_str());
	message.AddString("error", result.error.c_str());
	message.AddString("error_description", result.errorDescription.c_str());
}

static bool HasAllScopes(const std::string& granted, const std::string& required)
{
	std::set<std::string> grantedScopes;
	std::istringstream grantedStream(granted);
	std::string scope;
	while (grantedStream >> scope)
		grantedScopes.insert(scope);

	std::istringstream requiredStream(required);
	while (requiredStream >> scope) {
		if (grantedScopes.find(scope) == grantedScopes.end())
			return false;
	}
	return true;
}

template<typename Window, typename Matcher>
static Window* FindOpenWindow(BApplication* application, Matcher matcher)
{
	for (int32 i = 0; i < application->CountWindows(); i++) {
		Window* window = dynamic_cast<Window*>(application->WindowAt(i));
		if (window && matcher(window))
			return window;
	}
	return nullptr;
}

template<typename Window>
static Window* FindOpenWindow(BApplication* application)
{
	return FindOpenWindow<Window>(application, [](Window*) { return true; });
}

App::App()
	: BApplication(HAIFY_MIME_SIG),
	  fPlayerWindow(NULL),
	  fArtworkWindow(NULL),
	  fOAuthSrv(NULL),
	  fApi(new SpotifyApi("")),
	  fCapabilities(fApi),
	  fAlive(std::make_shared<std::atomic_bool>(true)),
	  fTokenLock("Spotify token refresh"),
	  fIsAuthenticated(false)
{
	auto alive = fAlive;
	fApi->SetTokenRefreshHandler([this, alive](TokenRefreshCompletion completion) {
		if (!alive->load()) {
			if (completion)
				completion(false);
			return;
		}
		_RefreshAccessToken(completion, true);
	});
}


void
App::RefreshSpotifyCapabilities(bool force)
{
	HaifySettings settings = SettingsController::Load();
	fCapabilities.SetAudiobookMode((AudiobookMode)settings.audiobookMode);
	if (!fIsAuthenticated) {
		fCapabilities.Reset();
		_BroadcastSpotifyCapabilities();
		return;
	}

	BMessenger app(this);
	fCapabilities.ProbeAudiobooks([app](AudiobookCapabilityState state) {
		BMessage message(MSG_SPOTIFY_CAPABILITIES_CHANGED);
		message.AddInt32("audiobook_state", (int32)state);
		message.AddBool("probe_result", true);
		app.SendMessage(&message);
	}, force);
}


void
App::_BroadcastSpotifyCapabilities()
{
	BMessage message(MSG_SPOTIFY_CAPABILITIES_CHANGED);
	message.AddInt32("audiobook_state",
		(int32)fCapabilities.AudiobookState());
	message.AddInt32("audiobook_mode",
		(int32)fCapabilities.AudiobookModeSetting());
	message.AddBool("audiobooks_enabled",
		fCapabilities.AudiobooksEnabled());
	for (int32 i = 0; i < CountWindows(); i++) {
		BWindow* window = WindowAt(i);
		if (window)
			window->PostMessage(&message);
	}
}


void
App::_RefreshSpotifyAccount()
{
	if (!fIsAuthenticated || !fApi)
		return;
	BMessenger app(this);
	fApi->GetCurrentUserProfile([app](bool ok, const nlohmann::json& profile) {
		if (!ok || !profile.is_object()) return;
		BMessage message('spAc');
		message.AddString("account_id",
			profile.value("id", profile.value("account_id", "")).c_str());
		message.AddString("provider_account_id",
			profile.value("account_id", "").c_str());
		app.SendMessage(&message);
	});
}


void
App::ReadyToRun()
{
	_InitAuth(true);

	HaifySettings s = SettingsController::Load();
	fArtworkWindowOpen = s.artworkWindowOpen;
	ImageCache::SetMaxCacheBytes(
		(int64)s.imageCacheLimitMB * 1024LL * 1024LL);

	if (s.librespotAlwaysStart)
		_StartLibrespot(kLibrespotTransferIfIdle);

	if (s.deskbarReplicantEnabled)
		_InstallDeskbarReplicant();
	else
		_RemoveDeskbarReplicant();
	_ShowPlayerWindow();

	if (s.browserWindowOpen) {
		DiscoverWindow* browser = new DiscoverWindow();
		browser->Show();
	}

	if (s.queueWindowOpen) {
		QueueWindow* qw = new QueueWindow();
		qw->Show();
	}

	if (s.searchWindowOpen) {
		SearchWindow* sw = new SearchWindow();
		sw->Show();
	}

	if (s.artworkWindowOpen)
		_ShowArtworkWindow();
}


bool
App::IsLibrespotRunning()
{
	_ReapLibrespot(false);
	return fLibrespotPid > 0;
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--debug") == 0)
			gIsDebug = true;
	}
	_ShowPlayerWindow();
}


void
App::_ShowPlayerWindow()
{
	if (!fPlayerWindow)
		fPlayerWindow = new PlayerWindow();

	if (fPlayerWindow->IsHidden())
		fPlayerWindow->Show();
	fPlayerWindow->Activate();
}


void
App::_HidePlayerWindow()
{
	if (fPlayerWindow && !fPlayerWindow->IsHidden())
		fPlayerWindow->Hide();
}


void
App::_TogglePlayerWindow()
{
	if (!fPlayerWindow || fPlayerWindow->IsHidden())
		_ShowPlayerWindow();
	else
		_HidePlayerWindow();
}


void
App::_ShowArtworkWindow()
{
	if (!fArtworkWindow)
		fArtworkWindow = new ArtworkWindow();

	if (fArtworkWindow->IsHidden())
		fArtworkWindow->Show();
	fArtworkWindowOpen = true;
	fArtworkWindow->SaveOpenState(true);
	fArtworkWindow->Activate();
}


void
App::SetArtworkWindowOpen(bool open)
{
	fArtworkWindowOpen = open;
}


void
App::_SendCurrentTrackTo(BWindow* window)
{
	if (!window || fLastReplicantState.IsEmpty())
		return;

	const char* trackUri = fLastReplicantState.GetString("track_uri", "");
	if (!trackUri || !trackUri[0])
		return;

	BMessage msg('pStU');
	msg.AddString("trackUri", trackUri);
	window->PostMessage(&msg);
}


void
App::_BroadcastPlayingTrack(const char* trackUri)
{
	if (!trackUri || !trackUri[0])
		return;

	if (!fLastReplicantState.IsEmpty()) {
		if (fLastReplicantState.ReplaceString("track_uri", trackUri) != B_OK)
			fLastReplicantState.AddString("track_uri", trackUri);
	}

	BMessage msg('pStU');
	msg.AddString("trackUri", trackUri);
	for (int32 i = 0; i < CountWindows(); i++) {
		BWindow* win = WindowAt(i);
		if (win)
			win->PostMessage(&msg);
	}
}


void
App::_InstallDeskbarReplicant()
{
	BDeskbar deskbar;
	if (!deskbar.IsRunning())
		return;

	if (deskbar.HasItem(DeskbarReplicantView::ItemName()))
		deskbar.RemoveItem(DeskbarReplicantView::ItemName());

	DeskbarReplicantView* view = new DeskbarReplicantView();
	if (deskbar.AddItem(view) != B_OK)
		delete view;
}


void
App::_RemoveDeskbarReplicant()
{
	BDeskbar deskbar;
	if (deskbar.IsRunning() && deskbar.HasItem(DeskbarReplicantView::ItemName()))
		deskbar.RemoveItem(DeskbarReplicantView::ItemName());
}


void
App::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_DESKBAR_REPLICANT_CHANGED:
			if (message->GetBool("enabled", true))
				_InstallDeskbarReplicant();
			else
				_RemoveDeskbarReplicant();
			break;

		case MSG_SHOW_PLAYER_WINDOW:
			_ShowPlayerWindow();
			break;

		case MSG_HIDE_PLAYER_WINDOW:
			_HidePlayerWindow();
			break;

		case MSG_TOGGLE_PLAYER_WINDOW:
			_TogglePlayerWindow();
			break;

		case MSG_OPEN_ARTWORK:
			_ShowArtworkWindow();
			break;

		case MSG_OPEN_SETTINGS:
		{
			SettingsWindow* settings = FindOpenWindow<SettingsWindow>(this);
			if (settings) {
				settings->Activate();
				break;
			}
			SettingsWindow* sw = new SettingsWindow();
			sw->Show();
			break;
		}

		case MSG_QUIT_APP:
			PostMessage(B_QUIT_REQUESTED);
			break;

		case MSG_OPEN_BROWSER:
		{
			DiscoverWindow* window = FindOpenWindow<DiscoverWindow>(this);
			if (window)
				window->Activate();
			else {
				window = new DiscoverWindow();
				window->Show();
			}
			_SendCurrentTrackTo(window);
			break;
		}

		case MSG_OPEN_PLAYLIST:
		{
			const char* name = "Rock Classics";
			const char* uri = "";
			const char* coverUrl = "";
			message->FindString("name", &name);
			message->FindString("uri", &uri);
			message->FindString("coverUrl", &coverUrl);
			std::string uriStr = uri ? uri : "";
			PlaylistWindow* window = FindOpenWindow<PlaylistWindow>(this,
				[&](PlaylistWindow* candidate) {
					return candidate->GetUri() == uriStr;
				});
			if (window)
				window->Activate();
			else {
				window = new PlaylistWindow(name, uri, coverUrl);
				window->Show();
			}
			_SendCurrentTrackTo(window);
			break;
		}

		case MSG_PLAYLISTS_CHANGED:
		{
			for (int32 i = 0; i < CountWindows(); i++) {
				DiscoverWindow* win =
					dynamic_cast<DiscoverWindow*>(WindowAt(i));
				if (win)
					win->PostMessage(message);
			}
			break;
		}

		case MSG_LIBRARY_CHANGED:
		{
			const char* operation = nullptr;
			const char* uri = nullptr;
			bool isDelta = message->FindString("operation", &operation) == B_OK
				&& message->FindString("uri", &uri) == B_OK;
			for (int32 i = 0; i < CountWindows(); i++) {
				BWindow* window = WindowAt(i);
				if (DiscoverWindow* discover =
						dynamic_cast<DiscoverWindow*>(window)) {
					if (isDelta)
						discover->PostMessage(message);
					else
						discover->PostMessage('lddt');
				} else if (isDelta
						&& (dynamic_cast<ArtistWindow*>(window)
							|| dynamic_cast<PlaylistWindow*>(window)
							|| dynamic_cast<AudiobookWindow*>(window)
							|| dynamic_cast<EpisodeWindow*>(window))) {
					window->PostMessage(message);
				}
			}
			break;
		}

		case MSG_SPOTIFY_CAPABILITIES_CHANGED:
			if (message->GetBool("probe_result", false))
				_BroadcastSpotifyCapabilities();
			else
				RefreshSpotifyCapabilities(
					message->GetBool("force", false));
			break;

		case 'spAc':
		{
			std::string accountId = message->GetString("account_id", "");
			if (accountId.empty()) break;
			std::string providerAccountId = message->GetString(
				"provider_account_id", "");
			HaifySettings settings = SettingsController::Load();
			bool changed = !settings.spotifyAccountId.empty()
				&& settings.spotifyAccountId != accountId
				&& (providerAccountId.empty()
					|| settings.spotifyAccountId != providerAccountId);
			SettingsController::Update([&](HaifySettings& value) {
				value.spotifyAccountId = accountId;
			});
			if (changed) {
				fApi->ClearSession();
				fApi->SetAccountId(accountId);
				fApi->SetAccessToken(settings.accessToken);
				fCapabilities.Reset();
				RefreshSpotifyCapabilities(true);
				for (int32 i = 0; i < CountWindows(); i++) {
					BWindow* window = WindowAt(i);
					if (window) window->PostMessage('lddt');
				}
			} else {
				fApi->SetAccountId(accountId);
			}

			// GetPlaylists may have completed before the profile ID was known.
			// Rebuild its writable-playlist cache now with the resolved identity.
			BMessenger application(this);
			fApi->GetPlaylists([application](bool ok,
					const nlohmann::json&) {
				if (!ok) return;
				BMessage refreshed(MSG_PLAYLISTS_CHANGED);
				application.SendMessage(&refreshed);
			});
			break;
		}

		case MSG_SHOW_ARTIST:
		{
			const char* id = "";
			message->FindString("id", &id);
			if (id && id[0]) {
				std::string idStr = id;
				ArtistWindow* window = FindOpenWindow<ArtistWindow>(this,
					[&](ArtistWindow* candidate) {
						return candidate->GetArtistId() == idStr;
					});
				if (window)
					window->Activate();
				else {
					window = new ArtistWindow(idStr);
					window->Show();
				}
				_SendCurrentTrackTo(window);
			}
			break;
		}

		case 'pStU':
		{
			_BroadcastPlayingTrack(message->GetString("trackUri", ""));
			break;
		}

		case MSG_REGISTER_REPLICANT:
		{
			BMessenger m;
			if (message->FindMessenger("messenger", &m) == B_OK && m.IsValid()) {
				bool external = message->GetBool("external", true);
				bool known = false;
				for (auto& r : fReplicants) {
					if (r.messenger == m) {
						r.external = external;
						known = true;
						break;
					}
				}
				if (!known)
					fReplicants.push_back({m, external});
				if (!fLastReplicantState.IsEmpty())
					m.SendMessage(&fLastReplicantState);
				HaifySettings settings = SettingsController::Load();
				BMessage colorMessage(MSG_SEEKBAR_COLOR_CHANGED);
				colorMessage.AddBool("use_system",
					settings.seekBarUseSystemColor);
				colorMessage.AddInt32("red", settings.seekBarColorRed);
				colorMessage.AddInt32("green", settings.seekBarColorGreen);
				colorMessage.AddInt32("blue", settings.seekBarColorBlue);
				colorMessage.AddInt32("alpha", settings.seekBarColorAlpha);
				m.SendMessage(&colorMessage);
				BMessage appearance(MSG_REPLICANT_APPEARANCE_CHANGED);
				appearance.AddBool("appearance_automatic",
					settings.replicantUseAutomaticColor);
				appearance.AddInt32("appearance_red", settings.replicantColorRed);
				appearance.AddInt32("appearance_green", settings.replicantColorGreen);
				appearance.AddInt32("appearance_blue", settings.replicantColorBlue);
				appearance.AddInt32("appearance_alpha", settings.replicantColorAlpha);
				appearance.AddBool("automatic",
					settings.replicantUseAutomaticColor);
				appearance.AddInt32("red", settings.replicantColorRed);
				appearance.AddInt32("green", settings.replicantColorGreen);
				appearance.AddInt32("blue", settings.replicantColorBlue);
				appearance.AddInt32("alpha", settings.replicantColorAlpha);
				m.SendMessage(&appearance);
				if (fPlayerWindow)
					fPlayerWindow->PostMessage(MSG_SYNC_REPLICANT_STATE);
			}
			break;
		}

		case MSG_SEEKBAR_COLOR_CHANGED:
		{
			if (fPlayerWindow)
				fPlayerWindow->PostMessage(message);
			for (auto& r : fReplicants) {
				if (r.messenger.IsValid())
					r.messenger.SendMessage(message);
			}
			break;
		}

		case MSG_REPLICANT_APPEARANCE_CHANGED:
		{
			if (fPlayerWindow)
				fPlayerWindow->PostMessage(message);
			for (auto& r : fReplicants) {
				if (r.messenger.IsValid())
					r.messenger.SendMessage(message);
			}
			break;
		}

		case MSG_UNREGISTER_REPLICANT:
		{
			BMessenger m;
			if (message->FindMessenger("messenger", &m) == B_OK) {
				for (auto it = fReplicants.begin(); it != fReplicants.end(); ) {
					if (it->messenger == m) it = fReplicants.erase(it);
					else ++it;
				}
			}
			break;
		}

		case MSG_REPLICANT_STATE:
		{
			std::string oldTrackUri =
				fLastReplicantState.GetString("track_uri", "");
			fLastReplicantState = *message;
			std::string newTrackUri =
				fLastReplicantState.GetString("track_uri", "");
			if (!newTrackUri.empty() && newTrackUri != oldTrackUri)
				_BroadcastPlayingTrack(newTrackUri.c_str());
			for (auto& r : fReplicants) {
				if (r.messenger.IsValid()) r.messenger.SendMessage(message);
				else r.messenger = BMessenger();
			}
			for (int32 i = 0; i < CountWindows(); i++) {
				BWindow* window = WindowAt(i);
				if (window && window != fPlayerWindow)
					window->PostMessage(message);
			}
			break;
		}

		case MSG_PLAY_PAUSE:
		case MSG_NEXT_TRACK:
		case MSG_PREV_TRACK:
		case MSG_SET_VOLUME:
		case MSG_TOGGLE_MUTE:
		case MSG_TOGGLE_SHUFFLE:
		case MSG_TOGGLE_REPEAT:
		case MSG_SAVE_CURRENT_TRACK:
		case MSG_SHOW_ADD_TRACK_MENU:
		case MSG_SEEK_REQUEST:
		case MSG_SEEKBAR_COLOR_DROPPED:
		case 'play':
		{
			if (message->what == 'play') {
				const char* uri = message->GetString("uri", "");
				if ((!uri || !uri[0]))
					uri = message->GetString("trackUri", "");
				if (uri && (strncmp(uri, "spotify:track:", 14) == 0
						|| strncmp(uri, "spotify:episode:", 16) == 0))
					_BroadcastPlayingTrack(uri);
			}
			if (fPlayerWindow)
				fPlayerWindow->PostMessage(message);
			break;
		}

		case 'poll':
			if (fPlayerWindow)
				fPlayerWindow->PostMessage(message);
			break;

		case MSG_OPEN_QUEUE:
		{
			QueueWindow* window = FindOpenWindow<QueueWindow>(this);
			if (window)
				window->Activate();
			else {
				window = new QueueWindow();
				window->Show();
			}
			_SendCurrentTrackTo(window);
			break;
		}

		case MSG_OPEN_SEARCH:
		{
			SearchWindow* window = FindOpenWindow<SearchWindow>(this);
			if (window)
				window->Activate();
			else {
				window = new SearchWindow();
				window->Show();
			}
			_SendCurrentTrackTo(window);
			break;
		}

		case 'open':
		{
			const char* uri   = nullptr;
			const char* title = nullptr;
			message->FindString("uri",   &uri);
			message->FindString("title", &title);
			if (!uri || !uri[0]) break;
			std::string uriStr = uri;
			if (uriStr.find("spotify:artist:") == 0) {
				std::string id = uriStr.substr(15);
				ArtistWindow* window = FindOpenWindow<ArtistWindow>(this,
					[&](ArtistWindow* candidate) {
						return candidate->GetArtistId() == id;
					});
				if (window)
					window->Activate();
				else {
					window = new ArtistWindow(id);
					window->Show();
				}
				_SendCurrentTrackTo(window);
			} else if (uriStr.find("spotify:episode:") == 0) {
				std::string id = uriStr.substr(16);
				EpisodeWindow* window = FindOpenWindow<EpisodeWindow>(this,
					[&](EpisodeWindow* candidate) {
						return candidate->GetEpisodeId() == id;
					});
				if (window)
					window->Activate();
				else {
					window = new EpisodeWindow(id);
					window->Show();
				}
			} else if (uriStr.find("spotify:audiobook:") == 0) {
				if (!fCapabilities.AudiobooksEnabled()) {
					BAlert* alert = new BAlert("", B_TRANSLATE(
						"Audiobooks are not available for this account or market."),
						B_TRANSLATE("OK"));
					alert->Go();
					break;
				}
				std::string id = uriStr.substr(18);
				AudiobookWindow* window = FindOpenWindow<AudiobookWindow>(this,
					[&](AudiobookWindow* candidate) {
						return candidate->GetAudiobookId() == id;
					});
				if (window)
					window->Activate();
				else {
					window = new AudiobookWindow(id);
					window->Show();
				}
			} else if (uriStr.find("spotify:track:") == 0) {
				BMessage play('play');
				play.AddString("uri", uriStr.c_str());
				PostMessage(&play);
			} else if (uriStr == "spotify:collection"
					|| uriStr.find("spotify:album:") == 0
					|| uriStr.find("spotify:playlist:") == 0
					|| uriStr.find("spotify:show:") == 0) {
				std::string t = title ? title : "";
				PlaylistWindow* window = FindOpenWindow<PlaylistWindow>(this,
					[&](PlaylistWindow* candidate) {
						return candidate->GetUri() == uriStr;
					});
				if (window)
					window->Activate();
				else {
					window = new PlaylistWindow(t.c_str(), uriStr.c_str(), "");
					window->Show();
				}
				_SendCurrentTrackTo(window);
			} else {
				BAlert* alert = new BAlert("", B_TRANSLATE(
					"This Spotify item type is not supported."),
					B_TRANSLATE("OK"));
				alert->Go();
			}
			break;
		}

		case MSG_SHOW_ALBUM:
		{
			const char* id = "";
			message->FindString("id", &id);
			if (id && id[0]) {
				std::string uri = std::string("spotify:album:") + id;
				PlaylistWindow* window = FindOpenWindow<PlaylistWindow>(this,
					[&](PlaylistWindow* candidate) {
						return candidate->GetUri() == uri;
					});
				if (window)
					window->Activate();
				else {
					window = new PlaylistWindow("Album", uri.c_str(), "");
					window->Show();
				}
				_SendCurrentTrackTo(window);
			}
			break;
		}

		case MSG_INIT_AUTH:
			_InitAuth(false);
			break;

		case kMsgRefreshAccessToken:
			delete fTokenRefreshTimer;
			fTokenRefreshTimer = nullptr;
			_RefreshAccessToken(nullptr, true);
			break;

		case MSG_AUTH_COMPLETE:
		{
			bool ok = message->GetBool("ok", false);
			bool silent = message->GetBool("silent", false);
			bool refreshRequest = message->GetBool("refresh_request", false);
			int32 messageGeneration;
			if (message->FindInt32("token_generation", &messageGeneration) == B_OK) {
				BAutolock lock(&fTokenLock);
				if (messageGeneration != fTokenGeneration) {
					lock.Unlock();
					if (refreshRequest)
						_CompleteTokenRefresh(false);
					break;
				}
			}
			std::string error = message->GetString("error", "");
			std::string errorDescription =
				message->GetString("error_description", "");
			std::string operation = message->GetString("operation",
				refreshRequest ? "token_refresh" : "authorization");

			if (ok) {
				const char* access = message->GetString("access_token", "");
				const char* refresh = message->GetString("refresh_token", "");
				const char* scopes = message->GetString("scopes", "");
				int32 expiresIn = message->GetInt32("expires_in", 3600);
				HaifySettings previousSettings = SettingsController::Load();
				std::string effectiveScopes = scopes[0]
					? scopes : previousSettings.grantedScopes;
				if (!HasAllScopes(effectiveScopes, SPOTIFY_REQUIRED_SCOPES)) {
					ok = false;
					error = "insufficient_scope";
					errorDescription = "Spotify did not grant all required permissions.";
				}
				status_t saveStatus = ok ? SettingsController::Update(
					[&](HaifySettings& s) {
						if (access[0]) s.accessToken = access;
						if (refresh[0]) s.refreshToken = refresh;
						s.grantedScopes = effectiveScopes;
						s.accessTokenExpiresAt = time(nullptr) + expiresIn;
						s.authScopeVersion = HAIFY_AUTH_SCOPE_VERSION;
					}) : B_NOT_ALLOWED;
				if (ok && saveStatus != B_OK) {
					ok = false;
					error = "settings_write_failed";
					errorDescription = "Could not save Spotify credentials.";
				} else if (ok && !access[0]) {
					ok = false;
					error = "missing_access_token";
				}
				if (ok) {
					fApi->SetAccessToken(access);
					fIsAuthenticated = true;
					_ScheduleTokenRefresh(expiresIn);
				}
			}

			if (ok) {

				{
					BMessage authMsg('aust');
					authMsg.AddBool("ok", true);
					if (fPlayerWindow) fPlayerWindow->PostMessage(&authMsg);
				}

				for (int32 i = 0; i < CountWindows(); i++) {
					BWindow* win = WindowAt(i);
					if (win) win->PostMessage('lddt');
				}

				if (fLibrespotPid > 0) {
					fLibrespotTransferAttempts = 0;
					_ScheduleLibrespotTransfer(0);
				}

				RefreshSpotifyCapabilities(false);
				_RefreshSpotifyAccount();

				if (!silent) {
					BAlert* alert = new BAlert("Auth", "Successfully connected to Spotify!", "OK");
					alert->Go();
				}
			} else {
				bool invalidGrant = error == "invalid_grant";
				DEBUG_PRINT("Spotify %s failed (HTTP %ld): %s (%s)\n",
					operation.c_str(),
					(long)message->GetInt32("http_status", -1),
					error.c_str(), errorDescription.c_str());
				if (invalidGrant) {
					SettingsController::Update([](HaifySettings& s) {
						s.accessToken.clear();
						s.refreshToken.clear();
						s.grantedScopes.clear();
						s.accessTokenExpiresAt = 0;
					});
					fApi->ClearSession();
					fIsAuthenticated = false;
					fCapabilities.Reset();
					_BroadcastSpotifyCapabilities();
					BMessage authMsg('aust');
					authMsg.AddBool("ok", false);
					if (fPlayerWindow)
						fPlayerWindow->PostMessage(&authMsg);
				}
				if (refreshRequest && !invalidGrant)
					_ScheduleTokenRefresh(90);
				if (!silent) {
					std::string text = "Error connecting to Spotify.";
					if (!error.empty())
						text += std::string("\n\n") + error;
					if (!errorDescription.empty())
						text += std::string(": ") + errorDescription;
					BAlert* alert = new BAlert("Auth", text.c_str(), "OK", NULL,
						NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
					alert->Go();
				}
			}
			if (refreshRequest)
				_CompleteTokenRefresh(ok);
			break;
		}

		case 'sout':
		{
			{
				BAutolock lock(&fTokenLock);
				fTokenGeneration++;
			}
			_CompleteTokenRefresh(false);
			SettingsController::Update([](HaifySettings& s) {
				s.accessToken.clear();
				s.refreshToken.clear();
				s.grantedScopes.clear();
				s.accessTokenExpiresAt = 0;
			});

			fIsAuthenticated = false;
			fApi->ClearSession();
			fCapabilities.Reset();
			_BroadcastSpotifyCapabilities();
			delete fTokenRefreshTimer;
			fTokenRefreshTimer = nullptr;

			{
				BMessage authMsg('aust');
				authMsg.AddBool("ok", false);
				if (fPlayerWindow) fPlayerWindow->PostMessage(&authMsg);
			}

			BAlert* alert = new BAlert("Auth", "Successfully signed out.", "OK");
			alert->Go();
			break;
		}

		case 'lbSt':
			if (message->GetBool("restart", false))
				_StopLibrespot();
			_StartLibrespot(kLibrespotTransferAlways);
			break;

		case 'lbRg':
			_StopLibrespot();
			_StartLibrespot(kLibrespotTransferAlways, true);
			break;

		case 'lbSp':
			_StopLibrespot();
			break;

		case MSG_TOGGLE_LIBRESPOT_RUNNING:
			_ReapLibrespot(false);
			if (fLibrespotPid > 0)
				_StopLibrespot();
			else
				_StartLibrespot(kLibrespotTransferAlways);
			break;

		case kMsgTransferLibrespotPlayback:
			_TryTransferPlaybackToLibrespot();
			break;

		case kMsgLibrespotDevicePollResult:
		{
			if (message->GetBool("found", false)) {
				if (fLibrespotOAuthRegistration) {
					SettingsController::FinishLibrespotOAuthRegistration();
					fLibrespotOAuthRegistration = false;
					for (int32 i = 0; i < CountWindows(); i++) {
						SettingsWindow* settings
							= dynamic_cast<SettingsWindow*>(WindowAt(i));
						if (settings)
							settings->PostMessage('lbOk');
					}
				}
				const char* deviceId = message->GetString("device_id", "");
				if (GetApi() && fLibrespotPid > 0 && deviceId && deviceId[0])
					_TransferPlaybackToLibrespotDevice(deviceId);
			} else if (fLibrespotPid > 0
					&& fLibrespotTransferAttempts
						< (fLibrespotOAuthRegistration ? 150 : 5)) {
				_ScheduleLibrespotTransfer(
					fLibrespotOAuthRegistration ? 2000000LL : 1000000LL);
			}
			break;
		}

		case kMsgLibrespotPlaybackDecision:
		{
			const char* deviceId = message->GetString("device_id", "");
			if (message->GetBool("transfer", false) && GetApi()
					&& fLibrespotPid > 0 && deviceId && deviceId[0]) {
				fApi->TransferPlayback(deviceId, nullptr);
			}
			break;
		}

		default:
			BApplication::MessageReceived(message);
			break;
	}
}

void
App::_InitAuth(bool silent)
{
	HaifySettings s = SettingsController::Load();
	fApi->SetAccountId(s.spotifyAccountId);

	if (strlen(HAIFY_CLIENT_ID) == 0) {
		if (!silent) {
			BAlert* alert = new BAlert("Error", "HAIFY_CLIENT_ID missing in Config.h!", "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go();
		}
		return;
	}

	if (silent) {
		if ((!s.accessToken.empty() || !s.refreshToken.empty())
				&& s.authScopeVersion != HAIFY_AUTH_SCOPE_VERSION) {
			SettingsController::Update([](HaifySettings& settings) {
				settings.accessToken.clear();
				settings.refreshToken.clear();
				settings.grantedScopes.clear();
				settings.accessTokenExpiresAt = 0;
			});
			return;
		}
		if (!s.accessToken.empty() && s.accessTokenExpiresAt > time(nullptr) + 60) {
			fApi->SetAccessToken(s.accessToken);
			fIsAuthenticated = true;
			_ScheduleTokenRefresh((int)(s.accessTokenExpiresAt - time(nullptr)));
			RefreshSpotifyCapabilities(false);
			_RefreshSpotifyAccount();
			return;
		}
		if (!s.refreshToken.empty()) {
			_RefreshAccessToken(nullptr, true);
		}
		return;
	}

	{
		int32 generation;
		{
			BAutolock lock(&fTokenLock);
			fTokenGeneration++;
			generation = fTokenGeneration;
		}
		_CompleteTokenRefresh(false);
		delete fOAuthSrv;
		fOAuthSrv = nullptr;

		auto auth = std::make_shared<SpotifyAuth>(HAIFY_CLIENT_ID);
		std::string authUrl = auth->BuildAuthUrl();
		if (authUrl.empty()) {
			BAlert* alert = new BAlert("Error",
				"Could not generate secure OAuth parameters.", "OK", nullptr,
				nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go();
			return;
		}
		std::string expectedState = auth->State();
		BMessenger messenger(this);
		fOAuthSrv = new OAuthCallbackServer(8765,
			[auth, expectedState, messenger, generation](const std::string& code,
					const std::string& state, const std::string& callbackError) {
				if (!callbackError.empty() || state != expectedState) {
					BMessage msg(MSG_AUTH_COMPLETE);
					msg.AddBool("ok", false);
					msg.AddBool("silent", false);
					msg.AddInt32("token_generation", generation);
					msg.AddString("operation", "oauth_callback");
					msg.AddString("error", callbackError.empty()
						? "state_mismatch" : callbackError.c_str());
					messenger.SendMessage(&msg);
					return;
				}
				auth->ExchangeCode(code,
					[messenger, generation](const TokenResult& result) {
						BMessage msg(MSG_AUTH_COMPLETE);
						AddTokenResult(msg, result);
						msg.AddBool("silent", false);
						msg.AddInt32("token_generation", generation);
						msg.AddString("operation", "authorization_code");
						messenger.SendMessage(&msg);
					});
			});

		if (!fOAuthSrv->Start()) {
			DEBUG_PRINT("Spotify OAuth callback server failed to start on port 8765\n");
			if (!silent) {
				BAlert* alert = new BAlert("Error", "Could not start local OAuth server. Port 8765 in use?", "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
				alert->Go();
			}
			delete fOAuthSrv;
			fOAuthSrv = nullptr;
			return;
		}

		status_t openStatus = OpenUrl(authUrl);
		if (openStatus != B_OK) {
			DEBUG_PRINT("Spotify authorization page returned status %ld; "
				"keeping callback server active\n", (long)openStatus);
		}
	}
}

void
App::_RefreshAccessToken(std::function<void(bool)> completion, bool silent)
{
	int32 generation;
	{
		BAutolock lock(&fTokenLock);
		if (completion)
			fTokenRefreshWaiters.push_back(completion);
		if (fTokenRefreshInFlight)
			return;
		fTokenRefreshInFlight = true;
		generation = fTokenGeneration;
	}

	HaifySettings settings = SettingsController::Load();
	if (settings.refreshToken.empty()) {
		BMessage message(MSG_AUTH_COMPLETE);
		message.AddBool("ok", false);
		message.AddBool("silent", silent);
		message.AddBool("refresh_request", true);
		message.AddInt32("token_generation", generation);
		message.AddString("operation", "token_refresh");
		message.AddString("error", "missing_refresh_token");
		PostMessage(&message);
		return;
	}

	auto auth = std::make_shared<SpotifyAuth>(HAIFY_CLIENT_ID);
	BMessenger messenger(this);
	auth->RefreshToken(settings.refreshToken,
		[auth, messenger, silent, generation](const TokenResult& result) {
			BMessage message(MSG_AUTH_COMPLETE);
			AddTokenResult(message, result);
			message.AddBool("silent", silent);
			message.AddBool("refresh_request", true);
			message.AddInt32("token_generation", generation);
			message.AddString("operation", "token_refresh");
			messenger.SendMessage(&message);
		});
}

void
App::_CompleteTokenRefresh(bool ok)
{
	std::vector<std::function<void(bool)>> waiters;
	{
		BAutolock lock(&fTokenLock);
		fTokenRefreshInFlight = false;
		waiters.swap(fTokenRefreshWaiters);
	}
	for (auto& waiter : waiters)
		waiter(ok);
}

void
App::_ScheduleTokenRefresh(int expiresIn)
{
	delete fTokenRefreshTimer;
	fTokenRefreshTimer = nullptr;
	int delaySeconds = expiresIn > 120 ? expiresIn - 60 : 30;
	BMessage message(kMsgRefreshAccessToken);
	fTokenRefreshTimer = new BMessageRunner(BMessenger(this), &message,
		(bigtime_t)delaySeconds * 1000000LL, 1);
}

bool
App::QuitRequested()
{
	fIsQuitting = true;
	fAlive->store(false);
	fApi->SetTokenRefreshHandler(nullptr);
	_RemoveDeskbarReplicant();

	SettingsController::Update([&](HaifySettings& s) {
		s.browserWindowOpen = false;
		s.queueWindowOpen   = false;
		s.searchWindowOpen  = false;
		s.artworkWindowOpen = fArtworkWindowOpen
			|| (fArtworkWindow && !fArtworkWindow->IsHidden());

		for (int32 i = 0; i < CountWindows(); i++) {
			BWindow* win = WindowAt(i);
			if (!win) continue;
			BRect f = win->Frame();

			if (dynamic_cast<DiscoverWindow*>(win)) {
				s.browserWindowOpen = true;
				s.browserWindowX = f.left;  s.browserWindowY = f.top;
				s.browserWindowW = f.Width(); s.browserWindowH = f.Height();
			} else if (dynamic_cast<QueueWindow*>(win)) {
				s.queueWindowOpen = true;
				s.queueWindowX = f.left;  s.queueWindowY = f.top;
				s.queueWindowW = f.Width(); s.queueWindowH = f.Height();
			} else if (dynamic_cast<SearchWindow*>(win)) {
				s.searchWindowOpen = true;
				s.searchWindowX = f.left;  s.searchWindowY = f.top;
				s.searchWindowW = f.Width(); s.searchWindowH = f.Height();
			}
		}

		if (fArtworkWindow) {
			BRect f = fArtworkWindow->Frame();
			s.artworkWindowX = f.left;  s.artworkWindowY = f.top;
			s.artworkWindowW = f.Width(); s.artworkWindowH = f.Height();
		}
	});
	delete fTokenRefreshTimer;
	fTokenRefreshTimer = nullptr;
	delete fOAuthSrv;
	fOAuthSrv = nullptr;
	_StopLibrespot();
	return true;
}


void
App::_StartLibrespot(LibrespotTransferMode mode, bool registerOAuth)
{
	_ReapLibrespot(false);
	fLibrespotTransferMode = mode;
	if (fLibrespotPid > 0) {
		fLibrespotTransferAttempts = 0;
		_ScheduleLibrespotTransfer(3000000LL);
		return;
	}

	HaifySettings s = SettingsController::Load();
	std::string librespotPath = s.librespotPath.empty()
		? SettingsController::FindLibrespotPath() : s.librespotPath;
	if (librespotPath.empty()) {
		BAlert* a = new BAlert("Haify",
			"librespot not found. Please install librespot or set its path in File → Settings.",
			"OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		a->Go();
		return;
	}

	std::vector<std::string> args;
	args.push_back(librespotPath);

	std::string cachePath = s.librespotCachePath.empty()
		? SettingsController::DefaultCachePath() : s.librespotCachePath;
	args.push_back("--cache");
	args.push_back(cachePath);

	std::string systemCachePath
		= SettingsController::LibrespotSystemCachePath(s);
	if (registerOAuth
			&& !SettingsController::PrepareLibrespotOAuthRegistration(s)) {
		BAlert* alert = new BAlert("Haify",
			"Could not prepare the librespot OAuth registration.", "OK",
			nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->Go();
		return;
	}
	fLibrespotOAuthRegistration = registerOAuth;
	if (!systemCachePath.empty()) {
		args.push_back("--system-cache");
		args.push_back(systemCachePath);
	}
	bool hasEnableOAuthArgument = registerOAuth;
	if (hasEnableOAuthArgument)
		args.push_back("--enable-oauth");

	if (_WriteLibrespotEventScript()) {
		unlink(SettingsController::LibrespotEventStatePath().c_str());
		unlink((SettingsController::LibrespotEventStatePath()
			+ ".playback").c_str());
		args.push_back("--onevent=" + SettingsController::LibrespotEventScriptPath());
	}

	args.push_back("--backend");
	args.push_back(s.librespotBackend.empty() ? "sdl" : s.librespotBackend);
	args.push_back("--bitrate");
	args.push_back(std::to_string(s.librespotBitrate));

	args.push_back("--initial-volume");
	args.push_back(std::to_string(s.librespotVolume));

	if (s.librespotAutoplay) {
		args.push_back("--autoplay");
		args.push_back("on");
	}
	if (s.librespotNormalization) args.push_back("--enable-volume-normalisation");

	args.push_back("--name");
	args.push_back(s.librespotDeviceName.empty()
		? LIBRESPOT_DEVICE_NAME : s.librespotDeviceName);
	if (!s.librespotDeviceType.empty()) {
		args.push_back("--device-type");
		args.push_back(s.librespotDeviceType);
	}
	if (s.librespotDisableDiscovery) args.push_back("--disable-discovery");

	if (!s.librespotAdditionalArgs.empty()) {
		std::istringstream iss(s.librespotAdditionalArgs);
		std::string token;
		while (iss >> token) {
			if (token == "-j" || token == "--enable-oauth") {
				if (hasEnableOAuthArgument)
					continue;
				hasEnableOAuthArgument = true;
			}
			args.push_back(token);
		}
	}

	std::vector<char*> argv;
	for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
	argv.push_back(nullptr);

	pid_t pid = fork();
	if (pid == 0) {
		execv(argv[0], argv.data());
		_exit(1);
	} else if (pid > 0) {
		fLibrespotPid = pid;
		fLibrespotTransferAttempts = 0;
		_ScheduleLibrespotTransfer(3000000LL);
	} else if (fLibrespotOAuthRegistration) {
		SettingsController::FinishLibrespotOAuthRegistration();
		fLibrespotOAuthRegistration = false;
	}
}


void
App::_ScheduleLibrespotTransfer(bigtime_t delay)
{
	delete fLibrespotTransferTimer;
	fLibrespotTransferTimer = nullptr;

	BMessage message(kMsgTransferLibrespotPlayback);
	if (delay <= 0) {
		PostMessage(&message);
		return;
	}
	fLibrespotTransferTimer = new BMessageRunner(BMessenger(this), &message,
		delay, 1);
}


void
App::_TryTransferPlaybackToLibrespot()
{
	delete fLibrespotTransferTimer;
	fLibrespotTransferTimer = nullptr;

	_ReapLibrespot(false);
	SpotifyApi* api = GetApi();
	if (fLibrespotPid <= 0 || !api)
		return;

	HaifySettings s = SettingsController::Load();
	std::string deviceName = s.librespotDeviceName.empty()
		? LIBRESPOT_DEVICE_NAME : s.librespotDeviceName;
	BMessenger messenger(this);
	fLibrespotTransferAttempts++;

	api->GetDevices([messenger, deviceName](bool ok,
			const nlohmann::json& data) {
		std::string deviceId;
		if (ok && data.contains("devices") && data["devices"].is_array()) {
			for (const auto& device : data["devices"]) {
				if (!device.is_object())
					continue;
				if (device.value("name", std::string()) == deviceName) {
					deviceId = device.value("id", std::string());
					break;
				}
			}
		}

		BMessage result(kMsgLibrespotDevicePollResult);
		result.AddBool("found", !deviceId.empty());
		if (!deviceId.empty())
			result.AddString("device_id", deviceId.c_str());
		messenger.SendMessage(&result);
	});
}


void
App::_TransferPlaybackToLibrespotDevice(const char* deviceId)
{
	SpotifyApi* api = GetApi();
	if (!api || fLibrespotPid <= 0 || !deviceId || !deviceId[0])
		return;

	if (fLibrespotTransferMode == kLibrespotTransferAlways) {
		api->TransferPlayback(deviceId, nullptr);
		return;
	}

	std::string targetDeviceId = deviceId;
	BMessenger messenger(this);
	api->GetPlaybackState([messenger, targetDeviceId](bool ok,
			const nlohmann::json& data) {
		bool shouldTransfer = ok;
		if (ok && data.is_object()) {
			bool isPlaying = data.value("is_playing", false);
			std::string activeDeviceId;
			if (data.contains("device") && data["device"].is_object())
				activeDeviceId = data["device"].value("id", std::string());
			if (isPlaying && activeDeviceId != targetDeviceId)
				shouldTransfer = false;
		}

		BMessage decision(kMsgLibrespotPlaybackDecision);
		decision.AddBool("transfer", shouldTransfer);
		decision.AddString("device_id", targetDeviceId.c_str());
		messenger.SendMessage(&decision);
	});
}


static std::string
ShellQuote(const std::string& value)
{
	std::string result = "'";
	for (char c : value) {
		if (c == '\'')
			result += "'\\''";
		else
			result += c;
	}
	result += "'";
	return result;
}


bool
App::_WriteLibrespotEventScript()
{
	std::string statePath = SettingsController::LibrespotEventStatePath();
	std::string scriptPath = SettingsController::LibrespotEventScriptPath();

	std::ofstream script(scriptPath);
	if (!script.is_open())
		return false;

	script
		<< "#!/bin/sh\n"
		<< "STATE_FILE=" << ShellQuote(statePath) << "\n"
		<< "PLAYBACK_FILE=\"${STATE_FILE}.playback\"\n"
		<< "case \"$PLAYER_EVENT\" in\n"
		<< "    track_changed) TARGET_FILE=\"$STATE_FILE\" ;;\n"
		<< "    *) TARGET_FILE=\"$PLAYBACK_FILE\" ;;\n"
		<< "esac\n"
		<< "tmp=\"${TARGET_FILE}.tmp.$$\"\n"
		<< "first_line() {\n"
		<< "    printf '%s\\n' \"$1\" | sed -n '1p'\n"
		<< "}\n"
		<< "ARTIST=\"$(first_line \"$ARTISTS\")\"\n"
		<< "{\n"
		<< "    printf 'event_id=%s-%s-%s-%s\\n' \"$(date +%s)\" \"$$\" \"$PLAYER_EVENT\" \"$TRACK_ID\"\n"
		<< "    printf 'event=%s\\n' \"$PLAYER_EVENT\"\n"
		<< "    printf 'item_type=%s\\n' \"$ITEM_TYPE\"\n"
		<< "    printf 'track_id=%s\\n' \"$TRACK_ID\"\n"
		<< "    printf 'uri=%s\\n' \"$URI\"\n"
		<< "    printf 'name=%s\\n' \"$NAME\"\n"
		<< "    printf 'artist=%s\\n' \"$ARTIST\"\n"
		<< "    printf 'album=%s\\n' \"$ALBUM\"\n"
		<< "    printf 'duration_ms=%s\\n' \"$DURATION_MS\"\n"
		<< "    printf 'position_ms=%s\\n' \"$POSITION_MS\"\n"
		<< "    printf 'shuffle=%s\\n' \"$SHUFFLE\"\n"
		<< "    printf 'repeat=%s\\n' \"$REPEAT\"\n"
		<< "    printf 'volume=%s\\n' \"$VOLUME\"\n"
		<< "} > \"$tmp\"\n"
		<< "mv \"$tmp\" \"$TARGET_FILE\"\n";
	script.close();

	chmod(scriptPath.c_str(), 0755);
	return true;
}


void
App::_StopLibrespot()
{
	delete fLibrespotTransferTimer;
	fLibrespotTransferTimer = nullptr;
	fLibrespotTransferAttempts = 0;

	_ReapLibrespot(false);
	if (fLibrespotPid <= 0)
		return;

	pid_t pid = fLibrespotPid;
	kill(pid, SIGINT);
	for (int i = 0; i < 20; i++) {
		if (_ReapLibrespot(false))
			return;
		usleep(100000);
	}

	kill(pid, SIGTERM);
	for (int i = 0; i < 20; i++) {
		if (_ReapLibrespot(false))
			return;
		usleep(100000);
	}

	kill(pid, SIGKILL);
	_ReapLibrespot(true);
	fLibrespotPid = -1;
}


bool
App::_ReapLibrespot(bool wait)
{
	if (fLibrespotPid <= 0)
		return true;

	int status = 0;
	pid_t result = waitpid(fLibrespotPid, &status, wait ? 0 : WNOHANG);
	if (result == fLibrespotPid) {
		fLibrespotPid = -1;
		if (fLibrespotOAuthRegistration) {
			SettingsController::FinishLibrespotOAuthRegistration();
			fLibrespotOAuthRegistration = false;
		}
		return true;
	}

	if (result < 0 && errno == ECHILD) {
		fLibrespotPid = -1;
		if (fLibrespotOAuthRegistration) {
			SettingsController::FinishLibrespotOAuthRegistration();
			fLibrespotOAuthRegistration = false;
		}
		return true;
	}

	return false;
}


int
main()
{
	App app;
	app.Run();
	return 0;
}
