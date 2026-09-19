#include "app/App.h"
#include "spotify/session/SpotifySessionMessages.h"
#include "spotify/session/SpotifySessionPolicy.h"
#include "spotify/session/SpotifyCredentialStore.h"
#include "navigation/SpotifyNavigationMessages.h"
#include "playback/LibrespotArguments.h"
#include "playback/LibrespotEventState.h"
#include "playback/LibrespotTransferMessages.h"
#include "messages/Messages.h"
#include "messages/MessageContracts.h"
#include "ui/windows/PlayerWindow.h"
#include "ui/windows/ArtworkWindow.h"
#include "ui/windows/DiscoverWindow.h"
#include "ui/drag/HaifyDragState.h"
#include "ui/replicants/DeskbarReplicantView.h"
#include "ui/windows/PlaylistWindow.h"
#include "ui/windows/ArtistWindow.h"
#include "ui/windows/EpisodeWindow.h"
#include "ui/windows/AudiobookWindow.h"
#include "ui/windows/QueueWindow.h"
#include "ui/windows/SearchWindow.h"
#include "ui/windows/SettingsWindow.h"

#include "app/Config.h"
#include "settings/SettingsController.h"
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
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#include "app/HaifyDebug.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "App"

static const uint32 kMsgRefreshAccessToken = 'rfrt';
static const bigtime_t kLibrespotPlaybackPollDelay = 1500000LL;

bool gIsDebug = false;

static status_t OpenUrl(const std::string& url) {
    BUrl target(url.c_str(), false);
    return target.OpenWithPreferredApplication(false);
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
	BMessenger app(this);
	fCapabilities.RefreshForSession(fIsAuthenticated, (AudiobookMode)settings.audiobookMode,
		[app](AudiobookCapabilityState) {
		BMessage message = MakeSpotifyCapabilityProbeResult();
		app.SendMessage(&message);
	}, force);
}


void
App::_BroadcastSpotifyCapabilities()
{
	BMessage message = MakeSpotifyCapabilitiesSnapshot(fCapabilities);
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
	int64 requestId = fAccountRequests.Begin();
	BMessenger app(this);
	RequestSpotifyAccount(fApi->Profile(), requestId, [app](const SpotifyAccountResult& result) {
		BMessage message = MakeSpotifyAccountResultMessage(result);
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
	// Window threads read a snapshot; only the App looper reaps the process
	// and resets transfer state. The next menu refresh observes that result.
	PostMessage(MSG_LIBRESPOT_REAP);
	return fLibrespotPid.load() > 0;
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

	BMessage msg = MessageContracts::MakeCurrentTrackUpdate({trackUri});
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

	BMessage msg = MessageContracts::MakeCurrentTrackUpdate({trackUri});
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
	if (!MessageContracts::MatchesAccount(*message, fApi ? fApi->AccountId() : ""))
		return;
	for (int32 i = 0; i < CountWindows(); i++) {
		DiscoverWindow* window = dynamic_cast<DiscoverWindow*>(WindowAt(i));
		if (window)
			window->PostMessage(message);
	}
}


void
App::_BroadcastLibraryChanged(BMessage* message)
{
	if (!MessageContracts::MatchesAccount(*message, fApi ? fApi->AccountId() : ""))
		return;
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
	SpotifyAccountResult result;
	if (!ReadSpotifyAccountResultMessage(*message, result)
			|| !fAccountRequests.Accept(result.requestId) || !fIsAuthenticated)
		return;
	DEBUG_PRINT("Account profile: ok=%d valid=%d status=%d retry_after=%d\n",
		result.ok, result.valid, (int)result.status, (int)result.retryAfter);
	SpotifyAccountUpdate update = ApplySpotifyAccount(*fApi, fCapabilities, result);
	if (!update.applied) {
		DEBUG_PRINT("Account not applied: settings status=%ld\n", (long)update.storageStatus);
		return;
	}
	if (update.changed) {
		RefreshSpotifyCapabilities(true);
		_ReloadAllWindows();
	}
	BMessenger application(this);
	RefreshSpotifyAccountPlaylists(*fApi, [application](const SpotifyAccountPlaylistsResult& refreshed) {
		if (!refreshed.ok) {
			DEBUG_PRINT("Account playlists failed: status=%d retry_after=%d\n",
				(int)refreshed.status, (int)refreshed.retryAfter);
			return;
		}
		BMessage changed = MakeSpotifyAccountPlaylistsMessage(refreshed);
		application.SendMessage(&changed);
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
	if (message->what == MSG_PLAY_URI) {
		MessageContracts::PlayCommand command;
		if (!MessageContracts::ReadPlayCommand(*message, command)) {
			DEBUG_PRINT("Playback command rejected before forwarding: uri=%s\n",
				message->GetString(MessageFields::Uri, ""));
			return;
		}
		if (SpotifyItemIsPlayable(SpotifyItemKindForUri(command.uri)))
			_BroadcastPlayingTrack(command.uri.c_str());
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
	SpotifyOpenRequest request;
	if (ReadSpotifyOpenMessage(*message, request))
		_NavigateSpotify(request);
}


void
App::_NavigateSpotify(const SpotifyOpenRequest& request)
{
	const std::string id = SpotifyItemIdForUri(request.uri);
	switch (SelectSpotifyNavigation(request, fCapabilities.AudiobooksEnabled(), fApi != nullptr)) {
		case SpotifyNavigationAction::Artist:
			_OpenArtistUri(id);
			break;
		case SpotifyNavigationAction::Episode:
			_OpenEpisodeUri(id);
			break;
		case SpotifyNavigationAction::Audiobook:
			_OpenAudiobookUri(id);
			break;
		case SpotifyNavigationAction::PlayTrack: {
			BMessage play = MessageContracts::MakePlayCommand({request.uri.c_str()});
			PostMessage(&play);
			break;
		}
		case SpotifyNavigationAction::ResolveShow:
			_ResolveShowOrAudiobook(request);
			break;
		case SpotifyNavigationAction::Collection:
			_OpenCollectionWindow(request.uri, request.title, request.coverUrl);
			break;
		case SpotifyNavigationAction::AudiobooksUnavailable:
			_ShowAudiobooksUnavailableAlert();
			break;
		case SpotifyNavigationAction::Unsupported:
			_ShowUnsupportedSpotifyItemAlert();
			break;
		case SpotifyNavigationAction::None:
			break;
	}
}


void
App::_ShowAudiobooksUnavailableAlert()
{
	BAlert* alert = new BAlert("", B_TRANSLATE(
		"Audiobooks are not available for this account or market."),
		B_TRANSLATE("OK"));
	alert->Go();
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
App::_ResolveShowOrAudiobook(const SpotifyOpenRequest& request)
{
	if (!fApi)
		return;
	BMessenger app(this);
	ResolveSpotifyShow(fApi->Content(), request, [app](const SpotifyShowResolution& result) {
		BMessage resolved = MakeSpotifyShowResolutionMessage(result);
		app.SendMessage(&resolved);
	});
}


void
App::_ApplySpotifyShowResolution(BMessage* message)
{
	SpotifyShowResolution result;
	if (!ReadSpotifyShowResolutionMessage(*message, result))
		return;
	DEBUG_PRINT("Show navigation: resolution=%d status=%d retry_after=%d\n",
		static_cast<int>(result.kind), static_cast<int>(result.status),
		static_cast<int>(result.retryAfter));
	_NavigateSpotify(ResolvedSpotifyOpenRequest(result));
}


void
App::_OpenCollectionWindow(const std::string& uri, const std::string& title,
	const std::string& coverUrl)
{
	PlaylistWindow* window = FindOpenWindow<PlaylistWindow>(this,
		[&](PlaylistWindow* candidate) {
			return candidate->GetUri() == uri;
		});
	if (window) {
		if (!coverUrl.empty())
			window->SetCoverUrl(coverUrl);
		window->Activate();
	} else {
		window = new PlaylistWindow(title.c_str(), uri.c_str(),
			coverUrl.c_str());
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
	if (!_AcceptAuthCompletionGeneration(message))
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
App::_AcceptAuthCompletionGeneration(BMessage* message)
{
	int32 messageGeneration;
	if (message->FindInt32("token_generation", &messageGeneration) != B_OK)
		return true;
	BAutolock lock(&fTokenLock);
	if (messageGeneration == fTokenGeneration)
		return true;
	return false;
}


bool
App::_StoreAuthTokens(BMessage* message, std::string& error,
	std::string& errorDescription)
{
	TokenResult token;
	if (!ReadSpotifyTokenResult(*message, token)) {
		error = "invalid_token_result";
		return false;
	}
	SpotifyCredentialResult result = StoreSpotifyCredentials(*fApi, token);
	if (!result.stored) {
		error = result.error;
		errorDescription = result.description;
		return false;
	}
	fIsAuthenticated = true;
	_ScheduleTokenRefresh(result.expiresIn);
	return true;
}


void
App::_FinishSuccessfulAuth(bool silent)
{
	_SendAuthStateToPlayer(true);
	_ReloadAllWindows();

	if (fLibrespotPid > 0) {
		fLibrespotTransfer.ResetAttempts();
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
	SpotifyAuthFailureAction action = SelectSpotifyAuthFailure(error, refreshRequest);
	DEBUG_PRINT("Spotify %s failed (HTTP %ld): %s (%s)\n",
		operation.c_str(), (long)message->GetInt32("http_status", -1),
		error.c_str(), errorDescription.c_str());
	if (action.clearSession)
		_ClearAuthSession();
	if (action.retryRefresh)
		_ScheduleTokenRefresh(90);
	if (!silent)
		_ShowAuthFailureAlert(error, errorDescription);
}


status_t
App::_ClearAuthSession()
{
	fAccountRequests.Cancel();
	status_t status = ClearSpotifyCredentials(*fApi);
	if (status != B_OK)
		DEBUG_PRINT("Could not clear stored credentials: %ld\n", (long)status);
	fIsAuthenticated = false;
	fCapabilities.Reset();
	_BroadcastSpotifyCapabilities();
	_SendAuthStateToPlayer(false);
	return status;
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
	status_t status = _ClearAuthSession();
	delete fTokenRefreshTimer;
	fTokenRefreshTimer = nullptr;

	const char* text = status == B_OK ? "Successfully signed out."
		: "Signed out for this session, but saved credentials could not be removed."
		  " Please try signing out again.";
	BAlert* alert = new BAlert("Auth", text, "OK");
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
App::_ApplyLibrespotTransferResult(BMessage* message)
{
	LibrespotTransferResult result;
	if (!ReadLibrespotTransferResultMessage(*message, result))
		return;
	_ReapLibrespot(false);
	LibrespotTransferUpdate update = fLibrespotTransfer.Apply(result);
	if (!update.accepted)
		return;
	DEBUG_PRINT("Librespot transfer step=%d ok=%d valid=%d status=%d retry_after=%d\n",
		static_cast<int>(result.request.step), result.ok, result.responseValid,
		static_cast<int>(result.status), static_cast<int>(result.retryAfter));
	if (update.finishOAuth)
		_FinishLibrespotOAuthRegistration();
	if (update.retryDelayUs > 0)
		_ScheduleLibrespotTransfer(update.retryDelayUs);
	if (ValidLibrespotTransferRequest(update.next))
		_DispatchLibrespotTransfer(update.next);
	if (update.ready)
		_SchedulePlaybackPollAfterLibrespotTransfer(kLibrespotPlaybackPollDelay);
}


void
App::_FinishLibrespotOAuthRegistration()
{
	if (!fLibrespotOAuthRegistration)
		return;
	SettingsController::FinishLibrespotOAuthRegistration();
	fLibrespotOAuthRegistration = false;
	for (int32 i = 0; i < CountWindows(); i++) {
		SettingsWindow* settings = dynamic_cast<SettingsWindow*>(WindowAt(i));
		if (settings)
			settings->PostMessage('lbOk');
	}
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
		case MSG_SHOW_ALBUM:
			_ShowAlbumWindow(message);
			return true;
		default:
			return _HandleSpotifyNavigationMessage(message);
	}
}


bool
App::_HandleSpotifyNavigationMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_OPEN_SPOTIFY_URI:
			_OpenSpotifyUri(message);
			return true;
		case MSG_SPOTIFY_SHOW_RESOLVED:
			_ApplySpotifyShowResolution(message);
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
		case MSG_SPOTIFY_ACCOUNT_RESULT:
			_ApplySpotifyAccount(message);
			return true;
		case MSG_CURRENT_TRACK_UPDATE:
		{
			MessageContracts::CurrentTrackUpdate update;
			if (MessageContracts::ReadCurrentTrackUpdate(*message, update))
				_BroadcastPlayingTrack(update.uri.c_str());
			return true;
		}
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
		case MSG_PLAY_URI:
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
		case MSG_START_LIBRESPOT:
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
		case MSG_LIBRESPOT_REAP:
			_ReapLibrespot(false);
			return true;
		case MSG_LIBRESPOT_TRANSFER_POLL:
			if (!fLibrespotTransfer.Readiness().Accepts(
					ReadLibrespotTransferPoll(*message)))
				return true;
			_TryTransferPlaybackToLibrespot();
			return true;
		case MSG_LIBRESPOT_TRANSFER_RESULT:
			_ApplyLibrespotTransferResult(message);
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
	time_t now = time(nullptr);
	switch (SelectSpotifyAuthStartup(settings, HAIFY_AUTH_SCOPE_VERSION, now)) {
		case SpotifyAuthStartup::ClearObsoleteScopes: {
			status_t status = ClearStoredSpotifyCredentials();
			if (status != B_OK)
				DEBUG_PRINT("Could not clear obsolete credentials: %ld\n", (long)status);
			return false;
		}
		case SpotifyAuthStartup::UseAccessToken:
			fApi->SetAccessToken(settings.accessToken);
			fIsAuthenticated = true;
			_ScheduleTokenRefresh(SpotifyTokenLifetime(settings.accessTokenExpiresAt, now));
			RefreshSpotifyCapabilities(false);
			_RefreshSpotifyAccount();
			return true;
		case SpotifyAuthStartup::RefreshToken:
			_RefreshAccessToken(nullptr, true);
			return false;
		default:
			return false;
	}
}


int32
App::_BeginAuthGeneration()
{
	fAccountRequests.Cancel();
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
					AddSpotifyTokenResult(msg, result);
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
			AddSpotifyTokenResult(message, result);
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
	int delaySeconds = SpotifyTokenRefreshDelay(expiresIn);
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
	fLibrespotTransfer.Begin(system_time(), mode,
		registerOAuth || fLibrespotOAuthRegistration);
	if (fLibrespotPid > 0) {
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
	AppendLibrespotPlaybackArguments(args, s, hasEnableOAuthArgument);
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
	fLibrespotEventSession = 0;
	if (!_WriteLibrespotEventScript())
		return;

	unlink(SettingsController::LibrespotEventStatePath().c_str());
	unlink((SettingsController::LibrespotEventStatePath()
		+ ".playback").c_str());
	int64 session = system_time();
	// Pass the token as an argument so callbacks from an older process keep
	// their old identity even after the shared script has been rewritten.
	args.push_back("--onevent=" + SettingsController::LibrespotEventScriptPath()
		+ " " + std::to_string(session));
	fLibrespotEventSession = session;
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
		_ScheduleLibrespotTransfer(3000000LL);
	} else {
		fLibrespotEventSession = 0;
		if (fLibrespotOAuthRegistration) {
			SettingsController::FinishLibrespotOAuthRegistration();
			fLibrespotOAuthRegistration = false;
		}
	}
}

void
App::_ScheduleLibrespotTransfer(bigtime_t delay)
{
	delete fLibrespotTransferTimer;
	fLibrespotTransferTimer = nullptr;

	BMessage message = MakeLibrespotTransferPoll(
		fLibrespotTransfer.Readiness().Generation());
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
	_DispatchLibrespotTransfer(fLibrespotTransfer.Poll(deviceName));
}


void
App::_DispatchLibrespotTransfer(const LibrespotTransferRequest& request)
{
	if (!ValidLibrespotTransferRequest(request))
		return;
	SpotifyApi* api = GetApi();
	if (!api || fLibrespotPid <= 0) {
		// Release the pending step on the owner thread even when authentication
		// disappeared before dispatch; later authentication may retry discovery.
		LibrespotTransferResult failure;
		failure.request = request;
		BMessage message = MakeLibrespotTransferResultMessage(failure);
		_ApplyLibrespotTransferResult(&message);
		return;
	}
	BMessenger messenger(this);
	DispatchLibrespotTransfer(api->Playback(), request,
		[messenger](const LibrespotTransferResult& result) {
		BMessage message = MakeLibrespotTransferResultMessage(result);
		messenger.SendMessage(&message);
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
		<< "    printf '" << LibrespotEventFields::SessionId << "=%s\\n' \"$1\"\n"
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
	fLibrespotTransfer.Begin(0);
	fLibrespotEventSession = 0;
	delete fLibrespotTransferTimer;
	fLibrespotTransferTimer = nullptr;
	delete fLibrespotPlaybackPollTimer;
	fLibrespotPlaybackPollTimer = nullptr;

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
		fLibrespotTransfer.Begin(0);
		fLibrespotEventSession = 0;
		if (fLibrespotOAuthRegistration) {
			SettingsController::FinishLibrespotOAuthRegistration();
			fLibrespotOAuthRegistration = false;
		}
		return true;
	}

	if (result < 0 && errno == ECHILD) {
		fLibrespotPid = -1;
		fLibrespotTransfer.Begin(0);
		fLibrespotEventSession = 0;
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
