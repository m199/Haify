#include "App.h"
#include "Messages.h"
#include "PlayerWindow.h"
#include "ArtworkWindow.h"
#include "DiscoverWindow.h"
#include "HaifyDragState.h"
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
#include "spotify/SpotifyUri.h"
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
static const uint32 kMsgLibrespotPlaybackTransferred = 'lbpt';
static const uint32 kMsgRefreshAccessToken = 'rfrt';
static const bigtime_t kLibrespotPlaybackPollDelay = 1500000LL;

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
	fApi->Profile().GetCurrentUserProfile([app](bool ok,
			const nlohmann::json& profile) {
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
App::_ToggleDeskbarReplicant(BMessage* message)
{
	if (message->GetBool("enabled", true))
		_InstallDeskbarReplicant();
	else
		_RemoveDeskbarReplicant();
}


void
App::_ShowSettingsWindow()
{
	SettingsWindow* settings = FindOpenWindow<SettingsWindow>(this);
	if (settings) {
		settings->Activate();
		return;
	}
	SettingsWindow* window = new SettingsWindow();
	window->Show();
}


void
App::_ShowDiscoverWindow()
{
	DiscoverWindow* window = FindOpenWindow<DiscoverWindow>(this);
	if (window)
		window->Activate();
	else {
		window = new DiscoverWindow();
		window->Show();
	}
	_SendCurrentTrackTo(window);
}


void
App::_OpenPlaylistWindow(BMessage* message)
{
	const char* name = "Rock Classics";
	const char* uri = "";
	const char* coverUrl = "";
	message->FindString("name", &name);
	message->FindString("uri", &uri);
	message->FindString("coverUrl", &coverUrl);
	std::string uriString = uri ? uri : "";
	PlaylistWindow* window = FindOpenWindow<PlaylistWindow>(this,
		[&](PlaylistWindow* candidate) {
			return candidate->GetUri() == uriString;
		});
	if (window)
		window->Activate();
	else {
		window = new PlaylistWindow(name, uri, coverUrl);
		window->Show();
	}
	_SendCurrentTrackTo(window);
}


void
App::_BroadcastPlaylistsChanged(BMessage* message)
{
	for (int32 i = 0; i < CountWindows(); i++) {
		DiscoverWindow* window = dynamic_cast<DiscoverWindow*>(WindowAt(i));
		if (window)
			window->PostMessage(message);
	}
}


void
App::_BroadcastLibraryChanged(BMessage* message)
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
}


void
App::_BroadcastDragEnded()
{
	ClearHaifyActiveDragMessage();
	BMessage ended(MSG_HAIFY_DRAG_ENDED);
	for (int32 i = 0; i < CountWindows(); i++) {
		BWindow* window = WindowAt(i);
		if (window)
			window->PostMessage(&ended);
	}
}


void
App::_ApplySpotifyCapabilitiesMessage(BMessage* message)
{
	if (message->GetBool("probe_result", false))
		_BroadcastSpotifyCapabilities();
	else
		RefreshSpotifyCapabilities(message->GetBool("force", false));
}


void
App::_ApplySpotifyAccount(BMessage* message)
{
	std::string accountId = message->GetString("account_id", "");
	if (accountId.empty())
		return;
	std::string providerAccountId =
		message->GetString("provider_account_id", "");
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
			if (window)
				window->PostMessage('lddt');
		}
	} else {
		fApi->SetAccountId(accountId);
	}

	// GetPlaylists may have completed before the profile ID was known.
	// Rebuild its writable-playlist cache now with the resolved identity.
	BMessenger application(this);
	fApi->Playlists().GetPlaylists([application](bool ok,
			const nlohmann::json&) {
		if (!ok)
			return;
		BMessage refreshed(MSG_PLAYLISTS_CHANGED);
		application.SendMessage(&refreshed);
	});
}


void
App::_ShowArtistWindow(BMessage* message)
{
	const char* id = "";
	message->FindString("id", &id);
	if (!id || !id[0])
		return;
	std::string idString = id;
	ArtistWindow* window = FindOpenWindow<ArtistWindow>(this,
		[&](ArtistWindow* candidate) {
			return candidate->GetArtistId() == idString;
		});
	if (window)
		window->Activate();
	else {
		window = new ArtistWindow(idString);
		window->Show();
		window->PostMessage('lddt');
	}
	_SendCurrentTrackTo(window);
}


void
App::_RegisterReplicant(BMessage* message)
{
	BMessenger messenger;
	if (message->FindMessenger("messenger", &messenger) != B_OK
			|| !messenger.IsValid())
		return;
	bool external = message->GetBool("external", true);
	bool known = false;
	for (auto& registration : fReplicants) {
		if (registration.messenger == messenger) {
			registration.external = external;
			known = true;
			break;
		}
	}
	if (!known)
		fReplicants.push_back({messenger, external});
	if (!fLastReplicantState.IsEmpty())
		messenger.SendMessage(&fLastReplicantState);
	HaifySettings settings = SettingsController::Load();
	BMessage colorMessage(MSG_SEEKBAR_COLOR_CHANGED);
	colorMessage.AddBool("use_system", settings.seekBarUseSystemColor);
	colorMessage.AddInt32("red", settings.seekBarColorRed);
	colorMessage.AddInt32("green", settings.seekBarColorGreen);
	colorMessage.AddInt32("blue", settings.seekBarColorBlue);
	colorMessage.AddInt32("alpha", settings.seekBarColorAlpha);
	messenger.SendMessage(&colorMessage);
	BMessage appearance(MSG_REPLICANT_APPEARANCE_CHANGED);
	appearance.AddBool("appearance_automatic",
		settings.replicantUseAutomaticColor);
	appearance.AddInt32("appearance_red", settings.replicantColorRed);
	appearance.AddInt32("appearance_green", settings.replicantColorGreen);
	appearance.AddInt32("appearance_blue", settings.replicantColorBlue);
	appearance.AddInt32("appearance_alpha", settings.replicantColorAlpha);
	appearance.AddBool("automatic", settings.replicantUseAutomaticColor);
	appearance.AddInt32("red", settings.replicantColorRed);
	appearance.AddInt32("green", settings.replicantColorGreen);
	appearance.AddInt32("blue", settings.replicantColorBlue);
	appearance.AddInt32("alpha", settings.replicantColorAlpha);
	messenger.SendMessage(&appearance);
	if (fPlayerWindow)
		fPlayerWindow->PostMessage(MSG_SYNC_REPLICANT_STATE);
}


void
App::_BroadcastReplicantSettings(BMessage* message)
{
	if (fPlayerWindow)
		fPlayerWindow->PostMessage(message);
	for (auto& registration : fReplicants) {
		if (registration.messenger.IsValid())
			registration.messenger.SendMessage(message);
	}
}


void
App::_UnregisterReplicant(BMessage* message)
{
	BMessenger messenger;
	if (message->FindMessenger("messenger", &messenger) != B_OK)
		return;
	for (auto it = fReplicants.begin(); it != fReplicants.end();) {
		if (it->messenger == messenger)
			it = fReplicants.erase(it);
		else
			++it;
	}
}


void
App::_ApplyReplicantState(BMessage* message)
{
	std::string oldTrackUri = fLastReplicantState.GetString("track_uri", "");
	fLastReplicantState = *message;
	std::string newTrackUri = fLastReplicantState.GetString("track_uri", "");
	if (!newTrackUri.empty() && newTrackUri != oldTrackUri)
		_BroadcastPlayingTrack(newTrackUri.c_str());
	for (auto& registration : fReplicants) {
		if (registration.messenger.IsValid())
			registration.messenger.SendMessage(message);
		else
			registration.messenger = BMessenger();
	}
	for (int32 i = 0; i < CountWindows(); i++) {
		BWindow* window = WindowAt(i);
		if (window && window != fPlayerWindow)
			window->PostMessage(message);
	}
}


void
App::_ForwardPlayerCommand(BMessage* message)
{
	if (message->what == 'play') {
		const char* uri = message->GetString("uri", "");
		if ((!uri || !uri[0]))
			uri = message->GetString("trackUri", "");
		if (uri && SpotifyItemIsPlayable(SpotifyItemKindForUri(uri)))
			_BroadcastPlayingTrack(uri);
	}
	if (fPlayerWindow)
		fPlayerWindow->PostMessage(message);
}


void
App::_ForwardPlaybackPoll(BMessage* message)
{
	delete fLibrespotPlaybackPollTimer;
	fLibrespotPlaybackPollTimer = nullptr;
	if (fPlayerWindow)
		fPlayerWindow->PostMessage(message);
}


void
App::_ShowQueueWindow()
{
	QueueWindow* window = FindOpenWindow<QueueWindow>(this);
	if (window)
		window->Activate();
	else {
		window = new QueueWindow();
		window->Show();
	}
	_SendCurrentTrackTo(window);
}


void
App::_ShowSearchWindow()
{
	SearchWindow* window = FindOpenWindow<SearchWindow>(this);
	if (window)
		window->Activate();
	else {
		window = new SearchWindow();
		window->Show();
	}
	_SendCurrentTrackTo(window);
}


void
App::_OpenSpotifyUri(BMessage* message)
{
	const char* uri = nullptr;
	const char* title = nullptr;
	message->FindString("uri", &uri);
	message->FindString("title", &title);
	if (!uri || !uri[0])
		return;
	std::string uriString = uri;
	std::string titleString = title ? title : "";
	SpotifyItemKind kind = SpotifyItemKindForUri(uriString);
	if (kind == kSpotifyItemArtist)
		_OpenArtistUri(SpotifyItemIdForUri(uriString));
	else if (kind == kSpotifyItemEpisode)
		_OpenEpisodeUri(SpotifyItemIdForUri(uriString));
	else if (kind == kSpotifyItemAudiobook)
		_OpenAudiobookUri(SpotifyItemIdForUri(uriString));
	else if (kind == kSpotifyItemTrack) {
		BMessage play('play');
		play.AddString("uri", uriString.c_str());
		PostMessage(&play);
	} else if (_ShouldResolveShowAsAudiobook(message, kind)) {
		_ResolveShowOrAudiobook(uriString, titleString);
	} else if (_CanOpenPlaylistStyleUri(uriString, kind)) {
		_OpenCollectionWindow(uriString, titleString);
	} else {
		_ShowUnsupportedSpotifyItemAlert();
	}
}


bool
App::_ShouldResolveShowAsAudiobook(BMessage* message, SpotifyItemKind kind) const
{
	return kind == kSpotifyItemShow
		&& !message->GetBool("skip_audiobook_resolution", false)
		&& fCapabilities.AudiobooksEnabled() && fApi;
}


bool
App::_CanOpenPlaylistStyleUri(const std::string& uri, SpotifyItemKind kind) const
{
	return uri == "spotify:collection" || kind == kSpotifyItemAlbum
		|| kind == kSpotifyItemPlaylist || kind == kSpotifyItemShow;
}


void
App::_OpenArtistUri(const std::string& id)
{
	if (id.empty())
		return;
	ArtistWindow* window = FindOpenWindow<ArtistWindow>(this,
		[&](ArtistWindow* candidate) {
			return candidate->GetArtistId() == id;
		});
	if (window)
		window->Activate();
	else {
		window = new ArtistWindow(id);
		window->Show();
		window->PostMessage('lddt');
	}
	_SendCurrentTrackTo(window);
}


void
App::_OpenEpisodeUri(const std::string& id)
{
	if (id.empty())
		return;
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
}


void
App::_OpenAudiobookUri(const std::string& id)
{
	if (!fCapabilities.AudiobooksEnabled()) {
		BAlert* alert = new BAlert("", B_TRANSLATE(
			"Audiobooks are not available for this account or market."),
			B_TRANSLATE("OK"));
		alert->Go();
		return;
	}
	if (id.empty())
		return;
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
}


void
App::_ResolveShowOrAudiobook(const std::string& uri, const std::string& title)
{
	std::string id = SpotifyItemIdForUri(uri);
	if (id.empty()) {
		BMessage retry('open');
		retry.AddString("uri", uri.c_str());
		retry.AddString("title", title.c_str());
		retry.AddBool("skip_audiobook_resolution", true);
		PostMessage(&retry);
		return;
	}
	BMessenger app(this);
	fApi->Content().GetAudiobook(id, [app, uri, id, title](bool ok,
			const nlohmann::json& book) {
		BMessage resolved('open');
		if (ok && book.is_object()) {
			std::string audiobookUri;
			if (book.contains("uri") && book["uri"].is_string())
				audiobookUri = book["uri"].get<std::string>();
			if (SpotifyItemKindForUri(audiobookUri) != kSpotifyItemAudiobook)
				audiobookUri = SpotifyUriForItemKind(kSpotifyItemAudiobook, id);
			resolved.AddString("uri", audiobookUri.c_str());
		} else {
			resolved.AddString("uri", uri.c_str());
			resolved.AddBool("skip_audiobook_resolution", true);
		}
		resolved.AddString("title", title.c_str());
		app.SendMessage(&resolved);
	});
}


void
App::_OpenCollectionWindow(const std::string& uri, const std::string& title)
{
	PlaylistWindow* window = FindOpenWindow<PlaylistWindow>(this,
		[&](PlaylistWindow* candidate) {
			return candidate->GetUri() == uri;
		});
	if (window)
		window->Activate();
	else {
		window = new PlaylistWindow(title.c_str(), uri.c_str(), "");
		window->Show();
	}
	_SendCurrentTrackTo(window);
}


void
App::_ShowUnsupportedSpotifyItemAlert()
{
	BAlert* alert = new BAlert("", B_TRANSLATE(
		"This Spotify item type is not supported."), B_TRANSLATE("OK"));
	alert->Go();
}


void
App::_ShowAlbumWindow(BMessage* message)
{
	const char* id = "";
	message->FindString("id", &id);
	if (!id || !id[0])
		return;
	std::string uri = SpotifyUriForItemKind(kSpotifyItemAlbum, id);
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


void
App::_ApplyAuthComplete(BMessage* message)
{
	bool ok = message->GetBool("ok", false);
	bool silent = message->GetBool("silent", false);
	bool refreshRequest = message->GetBool("refresh_request", false);
	if (!_AcceptAuthCompletionGeneration(message, refreshRequest))
		return;

	std::string error = message->GetString("error", "");
	std::string errorDescription =
		message->GetString("error_description", "");
	std::string operation = message->GetString("operation",
		refreshRequest ? "token_refresh" : "authorization");

	if (ok)
		ok = _StoreAuthTokens(message, error, errorDescription);
	if (ok)
		_FinishSuccessfulAuth(silent);
	else
		_FinishFailedAuth(message, silent, refreshRequest, error,
			errorDescription, operation);
	if (refreshRequest)
		_CompleteTokenRefresh(ok);
}


bool
App::_AcceptAuthCompletionGeneration(BMessage* message, bool refreshRequest)
{
	int32 messageGeneration;
	if (message->FindInt32("token_generation", &messageGeneration) != B_OK)
		return true;
	BAutolock lock(&fTokenLock);
	if (messageGeneration == fTokenGeneration)
		return true;
	lock.Unlock();
	if (refreshRequest)
		_CompleteTokenRefresh(false);
	return false;
}


bool
App::_StoreAuthTokens(BMessage* message, std::string& error,
	std::string& errorDescription)
{
	const char* access = message->GetString("access_token", "");
	const char* refresh = message->GetString("refresh_token", "");
	const char* scopes = message->GetString("scopes", "");
	int32 expiresIn = message->GetInt32("expires_in", 3600);
	HaifySettings previousSettings = SettingsController::Load();
	std::string effectiveScopes = scopes[0]
		? scopes : previousSettings.grantedScopes;
	if (!HasAllScopes(effectiveScopes, SPOTIFY_REQUIRED_SCOPES)) {
		error = "insufficient_scope";
		errorDescription = "Spotify did not grant all required permissions.";
		return false;
	}
	status_t saveStatus = SettingsController::Update([&](HaifySettings& s) {
		if (access[0]) s.accessToken = access;
		if (refresh[0]) s.refreshToken = refresh;
		s.grantedScopes = effectiveScopes;
		s.accessTokenExpiresAt = time(nullptr) + expiresIn;
		s.authScopeVersion = HAIFY_AUTH_SCOPE_VERSION;
	});
	if (saveStatus != B_OK) {
		error = "settings_write_failed";
		errorDescription = "Could not save Spotify credentials.";
		return false;
	}
	if (!access[0]) {
		error = "missing_access_token";
		return false;
	}
	fApi->SetAccessToken(access);
	fIsAuthenticated = true;
	_ScheduleTokenRefresh(expiresIn);
	return true;
}


void
App::_FinishSuccessfulAuth(bool silent)
{
	_SendAuthStateToPlayer(true);
	_ReloadAllWindows();

	if (fLibrespotPid > 0) {
		fLibrespotTransferAttempts = 0;
		_ScheduleLibrespotTransfer(0);
	}

	RefreshSpotifyCapabilities(false);
	_RefreshSpotifyAccount();

	if (!silent) {
		BAlert* alert = new BAlert("Auth",
			"Successfully connected to Spotify!", "OK");
		alert->Go();
	}
}


void
App::_FinishFailedAuth(BMessage* message, bool silent, bool refreshRequest,
	const std::string& error, const std::string& errorDescription,
	const std::string& operation)
{
	bool invalidGrant = error == "invalid_grant";
	DEBUG_PRINT("Spotify %s failed (HTTP %ld): %s (%s)\n",
		operation.c_str(), (long)message->GetInt32("http_status", -1),
		error.c_str(), errorDescription.c_str());
	if (invalidGrant)
		_ClearAuthSession();
	if (refreshRequest && !invalidGrant)
		_ScheduleTokenRefresh(90);
	if (!silent)
		_ShowAuthFailureAlert(error, errorDescription);
}


void
App::_ClearAuthSession()
{
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
	_SendAuthStateToPlayer(false);
}


void
App::_SendAuthStateToPlayer(bool ok)
{
	BMessage authMsg('aust');
	authMsg.AddBool("ok", ok);
	if (fPlayerWindow)
		fPlayerWindow->PostMessage(&authMsg);
}


void
App::_ReloadAllWindows()
{
	for (int32 i = 0; i < CountWindows(); i++) {
		BWindow* window = WindowAt(i);
		if (window)
			window->PostMessage('lddt');
	}
}


void
App::_ShowAuthFailureAlert(const std::string& error,
	const std::string& errorDescription)
{
	std::string text = "Error connecting to Spotify.";
	if (!error.empty())
		text += std::string("\n\n") + error;
	if (!errorDescription.empty())
		text += std::string(": ") + errorDescription;
	BAlert* alert = new BAlert("Auth", text.c_str(), "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
}


void
App::_SignOut()
{
	{
		BAutolock lock(&fTokenLock);
		fTokenGeneration++;
	}
	_CompleteTokenRefresh(false);
	_ClearAuthSession();
	delete fTokenRefreshTimer;
	fTokenRefreshTimer = nullptr;

	BAlert* alert = new BAlert("Auth", "Successfully signed out.", "OK");
	alert->Go();
}


void
App::_StartLibrespotFromMessage(BMessage* message)
{
	if (message->GetBool("restart", false))
		_StopLibrespot();
	_StartLibrespot(kLibrespotTransferAlways);
}


void
App::_RegisterLibrespotOAuth()
{
	_StopLibrespot();
	_StartLibrespot(kLibrespotTransferAlways, true);
}


void
App::_StopLibrespotFromMessage()
{
	_StopLibrespot();
}


void
App::_ToggleLibrespotRunning()
{
	_ReapLibrespot(false);
	if (fLibrespotPid > 0)
		_StopLibrespot();
	else
		_StartLibrespot(kLibrespotTransferAlways);
}


void
App::_ApplyLibrespotDevicePollResult(BMessage* message)
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
		return;
	}

	int maxAttempts = fLibrespotOAuthRegistration ? 150 : 5;
	if (fLibrespotPid > 0 && fLibrespotTransferAttempts < maxAttempts) {
		_ScheduleLibrespotTransfer(
			fLibrespotOAuthRegistration ? 2000000LL : 1000000LL);
	}
}


void
App::_ApplyLibrespotPlaybackDecision(BMessage* message)
{
	const char* deviceId = message->GetString("device_id", "");
	if (!message->GetBool("transfer", false) || !GetApi()
			|| fLibrespotPid <= 0 || !deviceId || !deviceId[0]) {
		return;
	}

	BMessenger app(this);
	fApi->Playback().TransferPlayback(deviceId,
		[app](bool ok, const nlohmann::json&) {
		if (!ok)
			return;
		BMessage transferred(kMsgLibrespotPlaybackTransferred);
		app.SendMessage(&transferred);
	});
}


bool
App::_HandleWindowMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_DESKBAR_REPLICANT_CHANGED:
			_ToggleDeskbarReplicant(message);
			return true;
		case MSG_SHOW_PLAYER_WINDOW:
			_ShowPlayerWindow();
			return true;
		case MSG_HIDE_PLAYER_WINDOW:
			_HidePlayerWindow();
			return true;
		case MSG_TOGGLE_PLAYER_WINDOW:
			_TogglePlayerWindow();
			return true;
		case MSG_OPEN_ARTWORK:
			_ShowArtworkWindow();
			return true;
		case MSG_OPEN_SETTINGS:
			_ShowSettingsWindow();
			return true;
		case MSG_QUIT_APP:
			PostMessage(B_QUIT_REQUESTED);
			return true;
		case MSG_OPEN_BROWSER:
			_ShowDiscoverWindow();
			return true;
		case MSG_OPEN_PLAYLIST:
			_OpenPlaylistWindow(message);
			return true;
		case MSG_SHOW_ARTIST:
			_ShowArtistWindow(message);
			return true;
		case MSG_OPEN_QUEUE:
			_ShowQueueWindow();
			return true;
		case MSG_OPEN_SEARCH:
			_ShowSearchWindow();
			return true;
		case 'open':
			_OpenSpotifyUri(message);
			return true;
		case MSG_SHOW_ALBUM:
			_ShowAlbumWindow(message);
			return true;
		default:
			return false;
	}
}


bool
App::_HandleStateMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_PLAYLISTS_CHANGED:
			_BroadcastPlaylistsChanged(message);
			return true;
		case MSG_LIBRARY_CHANGED:
			_BroadcastLibraryChanged(message);
			return true;
		case MSG_SPOTIFY_CAPABILITIES_CHANGED:
			_ApplySpotifyCapabilitiesMessage(message);
			return true;
		case MSG_HAIFY_DRAG_ENDED:
			_BroadcastDragEnded();
			return true;
		case 'spAc':
			_ApplySpotifyAccount(message);
			return true;
		case 'pStU':
			_BroadcastPlayingTrack(message->GetString("trackUri", ""));
			return true;
		default:
			return false;
	}
}


bool
App::_HandleReplicantMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_REGISTER_REPLICANT:
			_RegisterReplicant(message);
			return true;
		case MSG_SEEKBAR_COLOR_CHANGED:
		case MSG_REPLICANT_APPEARANCE_CHANGED:
			_BroadcastReplicantSettings(message);
			return true;
		case MSG_UNREGISTER_REPLICANT:
			_UnregisterReplicant(message);
			return true;
		case MSG_REPLICANT_STATE:
			_ApplyReplicantState(message);
			return true;
		default:
			return false;
	}
}


bool
App::_HandlePlayerMessage(BMessage* message)
{
	switch (message->what) {
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
			_ForwardPlayerCommand(message);
			return true;
		case 'poll':
			_ForwardPlaybackPoll(message);
			return true;
		default:
			return false;
	}
}


bool
App::_HandleAuthLibrespotMessage(BMessage* message)
{
	return _HandleAuthMessage(message) || _HandleLibrespotMessage(message);
}


bool
App::_HandleAuthMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_INIT_AUTH:
			_InitAuth(false);
			return true;
		case MSG_AUTH_COMPLETE:
			_ApplyAuthComplete(message);
			return true;
		case kMsgRefreshAccessToken:
			_RefreshAccessToken(nullptr, true);
			return true;
		case 'sgno':
			_SignOut();
			return true;
		default:
			return false;
	}
}


bool
App::_HandleLibrespotMessage(BMessage* message)
{
	switch (message->what) {
		case 'stLb':
		case 'lbSt':
			_StartLibrespotFromMessage(message);
			return true;
		case 'rgLb':
		case 'lbRg':
			_RegisterLibrespotOAuth();
			return true;
		case 'spLb':
		case 'lbSp':
			_StopLibrespotFromMessage();
			return true;
		case MSG_TOGGLE_LIBRESPOT_RUNNING:
			_ToggleLibrespotRunning();
			return true;
		case kMsgTransferLibrespotPlayback:
			_TryTransferPlaybackToLibrespot();
			return true;
		case kMsgLibrespotDevicePollResult:
			_ApplyLibrespotDevicePollResult(message);
			return true;
		case kMsgLibrespotPlaybackDecision:
			_ApplyLibrespotPlaybackDecision(message);
			return true;
		case kMsgLibrespotPlaybackTransferred:
			_SchedulePlaybackPollAfterLibrespotTransfer(
				kLibrespotPlaybackPollDelay);
			return true;
		default:
			return false;
	}
}


void
App::MessageReceived(BMessage* message)
{
	if (_HandleWindowMessage(message) || _HandleStateMessage(message)
			|| _HandleReplicantMessage(message)
			|| _HandlePlayerMessage(message)
			|| _HandleAuthLibrespotMessage(message)) {
		return;
	}

	BApplication::MessageReceived(message);
}


void
App::_InitAuth(bool silent)
{
	HaifySettings settings = SettingsController::Load();
	fApi->SetAccountId(settings.spotifyAccountId);

	if (strlen(HAIFY_CLIENT_ID) == 0) {
		if (!silent)
			_ShowMissingClientIdAlert();
		return;
	}

	if (silent) {
		_InitSilentAuth(settings);
		return;
	}

	_StartInteractiveOAuth(_BeginAuthGeneration());
}


void
App::_ShowMissingClientIdAlert()
{
	BAlert* alert = new BAlert("Error",
		"HAIFY_CLIENT_ID missing in Config.h!", "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
}


bool
App::_InitSilentAuth(const HaifySettings& settings)
{
	if ((!settings.accessToken.empty() || !settings.refreshToken.empty())
			&& settings.authScopeVersion != HAIFY_AUTH_SCOPE_VERSION) {
		SettingsController::Update([](HaifySettings& value) {
			value.accessToken.clear();
			value.refreshToken.clear();
			value.grantedScopes.clear();
			value.accessTokenExpiresAt = 0;
		});
		return false;
	}
	if (!settings.accessToken.empty()
			&& settings.accessTokenExpiresAt > time(nullptr) + 60) {
		fApi->SetAccessToken(settings.accessToken);
		fIsAuthenticated = true;
		_ScheduleTokenRefresh((int)(settings.accessTokenExpiresAt
			- time(nullptr)));
		RefreshSpotifyCapabilities(false);
		_RefreshSpotifyAccount();
		return true;
	}
	if (!settings.refreshToken.empty())
		_RefreshAccessToken(nullptr, true);
	return false;
}


int32
App::_BeginAuthGeneration()
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
	return generation;
}


void
App::_StartInteractiveOAuth(int32 generation)
{
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
		BAlert* alert = new BAlert("Error",
			"Could not start local OAuth server. Port 8765 in use?", "OK",
			NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
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

	SettingsController::Update([&](HaifySettings& settings) {
		settings.browserWindowOpen = false;
		settings.queueWindowOpen = false;
		settings.searchWindowOpen = false;
		settings.artworkWindowOpen = fArtworkWindowOpen
			|| (fArtworkWindow && !fArtworkWindow->IsHidden());

		for (int32 i = 0; i < CountWindows(); i++) {
			BWindow* window = WindowAt(i);
			if (!window)
				continue;
			BRect frame = window->Frame();

			if (dynamic_cast<DiscoverWindow*>(window)) {
				settings.browserWindowOpen = true;
				settings.browserWindowX = frame.left;
				settings.browserWindowY = frame.top;
				settings.browserWindowW = frame.Width();
				settings.browserWindowH = frame.Height();
			} else if (dynamic_cast<QueueWindow*>(window)) {
				settings.queueWindowOpen = true;
				settings.queueWindowX = frame.left;
				settings.queueWindowY = frame.top;
				settings.queueWindowW = frame.Width();
				settings.queueWindowH = frame.Height();
			} else if (dynamic_cast<SearchWindow*>(window)) {
				settings.searchWindowOpen = true;
				settings.searchWindowX = frame.left;
				settings.searchWindowY = frame.top;
				settings.searchWindowW = frame.Width();
				settings.searchWindowH = frame.Height();
			}
		}

		if (fArtworkWindow) {
			BRect frame = fArtworkWindow->Frame();
			settings.artworkWindowX = frame.left;
			settings.artworkWindowY = frame.top;
			settings.artworkWindowW = frame.Width();
			settings.artworkWindowH = frame.Height();
		}
	});
	delete fTokenRefreshTimer;
	fTokenRefreshTimer = nullptr;
	delete fLibrespotPlaybackPollTimer;
	fLibrespotPlaybackPollTimer = nullptr;
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
	std::string librespotPath;
	if (!_ResolveLibrespotPath(s, librespotPath))
		return;

	std::vector<std::string> args;
	args.push_back(librespotPath);

	std::string cachePath = s.librespotCachePath.empty()
		? SettingsController::DefaultCachePath() : s.librespotCachePath;
	args.push_back("--cache");
	args.push_back(cachePath);

	std::string systemCachePath
		= SettingsController::LibrespotSystemCachePath(s);
	if (!_PrepareLibrespotOAuth(s, registerOAuth))
		return;
	fLibrespotOAuthRegistration = registerOAuth;
	if (!systemCachePath.empty()) {
		args.push_back("--system-cache");
		args.push_back(systemCachePath);
	}
	bool hasEnableOAuthArgument = registerOAuth;
	if (hasEnableOAuthArgument)
		args.push_back("--enable-oauth");

	_AddLibrespotEventArgs(args);
	_AddLibrespotPlaybackArgs(args, s, hasEnableOAuthArgument);
	_AddLibrespotAdditionalArgs(args, s.librespotAdditionalArgs,
		hasEnableOAuthArgument);
	_SpawnLibrespot(args);
}


bool
App::_ResolveLibrespotPath(const HaifySettings& settings,
	std::string& librespotPath)
{
	librespotPath = settings.librespotPath.empty()
		? SettingsController::FindLibrespotPath() : settings.librespotPath;
	if (!librespotPath.empty())
		return true;

	BAlert* alert = new BAlert("Haify",
		"librespot not found. Please install librespot or set its path in File → Settings.",
		"OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
	return false;
}


bool
App::_PrepareLibrespotOAuth(const HaifySettings& settings, bool registerOAuth)
{
	if (!registerOAuth
			|| SettingsController::PrepareLibrespotOAuthRegistration(settings)) {
		return true;
	}

	BAlert* alert = new BAlert("Haify",
		"Could not prepare the librespot OAuth registration.", "OK",
		nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
	return false;
}


void
App::_AddLibrespotEventArgs(std::vector<std::string>& args)
{
	if (!_WriteLibrespotEventScript())
		return;

	unlink(SettingsController::LibrespotEventStatePath().c_str());
	unlink((SettingsController::LibrespotEventStatePath()
		+ ".playback").c_str());
	args.push_back("--onevent="
		+ SettingsController::LibrespotEventScriptPath());
}


void
App::_AddLibrespotPlaybackArgs(std::vector<std::string>& args,
	const HaifySettings& settings, bool& hasEnableOAuthArgument)
{
	args.push_back("--backend");
	args.push_back(settings.librespotBackend.empty()
		? "sdl" : settings.librespotBackend);
	args.push_back("--bitrate");
	args.push_back(std::to_string(settings.librespotBitrate));

	args.push_back("--initial-volume");
	args.push_back(std::to_string(settings.librespotVolume));

	if (settings.librespotAutoplay) {
		args.push_back("--autoplay");
		args.push_back("on");
	}
	if (settings.librespotNormalization)
		args.push_back("--enable-volume-normalisation");

	args.push_back("--name");
	args.push_back(settings.librespotDeviceName.empty()
		? LIBRESPOT_DEVICE_NAME : settings.librespotDeviceName);
	if (!settings.librespotDeviceType.empty()) {
		args.push_back("--device-type");
		args.push_back(settings.librespotDeviceType);
	}
	if (settings.librespotDisableDiscovery)
		args.push_back("--disable-discovery");
}


void
App::_AddLibrespotAdditionalArgs(std::vector<std::string>& args,
	const std::string& additionalArgs, bool& hasEnableOAuthArgument)
{
	std::istringstream iss(additionalArgs);
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


void
App::_SpawnLibrespot(const std::vector<std::string>& args)
{
	std::vector<char*> argv;
	for (const auto& arg : args)
		argv.push_back(const_cast<char*>(arg.c_str()));
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
App::_SchedulePlaybackPollAfterLibrespotTransfer(bigtime_t delay)
{
	delete fLibrespotPlaybackPollTimer;
	fLibrespotPlaybackPollTimer = nullptr;

	BMessage message('poll');
	if (delay <= 0) {
		PostMessage(&message);
		return;
	}
	fLibrespotPlaybackPollTimer = new BMessageRunner(BMessenger(this),
		&message, delay, 1);
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

	api->Playback().GetDevices([messenger, deviceName](bool ok,
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
		BMessenger app(this);
		api->Playback().TransferPlayback(deviceId,
			[app](bool ok, const nlohmann::json&) {
			if (!ok)
				return;
			BMessage transferred(kMsgLibrespotPlaybackTransferred);
			app.SendMessage(&transferred);
		});
		return;
	}

	std::string targetDeviceId = deviceId;
	BMessenger messenger(this);
	api->Playback().GetPlaybackState([messenger, targetDeviceId](bool ok,
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
	delete fLibrespotPlaybackPollTimer;
	fLibrespotPlaybackPollTimer = nullptr;
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
