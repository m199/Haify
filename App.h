#ifndef APP_H
#define APP_H

#include <Application.h>
#include <Locker.h>
#include <Messenger.h>
#include <Message.h>
#include <functional>
#include <atomic>
#include <memory>
#include <vector>
#include <sys/types.h>

#include "spotify/SpotifyCapabilities.h"
#include "spotify/SpotifyUri.h"

class PlayerWindow;
class ArtworkWindow;
class BMessageRunner;
class BWindow;
class SpotifyAuth;
class OAuthCallbackServer;
class SpotifyApi;

struct ReplicantRegistration {
	BMessenger	messenger;
	bool		external = false;
};

enum LibrespotTransferMode {
	kLibrespotTransferAlways,
	kLibrespotTransferIfIdle
};

class App : public BApplication {
public:
							App();
	virtual void			ReadyToRun();
	virtual void			ArgvReceived(int32 argc, char** argv);
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage* message);

	SpotifyApi*				GetApi() const { return fIsAuthenticated ? fApi : nullptr; }
	SpotifyCapabilities*	GetCapabilities() { return &fCapabilities; }
	void					RefreshSpotifyCapabilities(bool force = false);
	bool					IsQuitting() const { return fIsQuitting; }
	bool					IsLibrespotRunning();
	void					SetArtworkWindowOpen(bool open);

private:
	void					_ShowPlayerWindow();
	void					_HidePlayerWindow();
	void					_TogglePlayerWindow();
	bool					_HandleWindowMessage(BMessage* message);
	bool					_HandleStateMessage(BMessage* message);
	bool					_HandleReplicantMessage(BMessage* message);
	bool					_HandlePlayerMessage(BMessage* message);
	bool					_HandleAuthLibrespotMessage(BMessage* message);
	bool					_HandleAuthMessage(BMessage* message);
	bool					_HandleLibrespotMessage(BMessage* message);
	void					_ShowArtworkWindow();
	void					_ToggleDeskbarReplicant(BMessage* message);
	void					_ShowSettingsWindow();
	void					_ShowDiscoverWindow();
	void					_OpenPlaylistWindow(BMessage* message);
	void					_BroadcastPlaylistsChanged(BMessage* message);
	void					_BroadcastLibraryChanged(BMessage* message);
	void					_ApplySpotifyCapabilitiesMessage(BMessage* message);
	void					_ApplySpotifyAccount(BMessage* message);
	void					_ShowArtistWindow(BMessage* message);
	void					_RegisterReplicant(BMessage* message);
	void					_BroadcastReplicantSettings(BMessage* message);
	void					_UnregisterReplicant(BMessage* message);
	void					_ApplyReplicantState(BMessage* message);
	void					_ForwardPlayerCommand(BMessage* message);
	void					_ForwardPlaybackPoll(BMessage* message);
	void					_ShowQueueWindow();
	void					_ShowSearchWindow();
	void					_OpenSpotifyUri(BMessage* message);
	bool					_ShouldResolveShowAsAudiobook(
								BMessage* message, SpotifyItemKind kind) const;
	bool					_CanOpenPlaylistStyleUri(
								const std::string& uri,
								SpotifyItemKind kind) const;
	void					_OpenArtistUri(const std::string& id);
	void					_OpenEpisodeUri(const std::string& id);
	void					_OpenAudiobookUri(const std::string& id);
	void					_ResolveShowOrAudiobook(const std::string& uri,
								const std::string& title);
	void					_OpenCollectionWindow(const std::string& uri,
								const std::string& title);
	void					_ShowUnsupportedSpotifyItemAlert();
	void					_ShowAlbumWindow(BMessage* message);
	void					_ApplyAuthComplete(BMessage* message);
	bool					_AcceptAuthCompletionGeneration(
								BMessage* message, bool refreshRequest);
	bool					_StoreAuthTokens(BMessage* message,
								std::string& error,
								std::string& errorDescription);
	void					_FinishSuccessfulAuth(bool silent);
	void					_FinishFailedAuth(BMessage* message, bool silent,
								bool refreshRequest,
								const std::string& error,
								const std::string& errorDescription,
								const std::string& operation);
	void					_ClearAuthSession();
	void					_SendAuthStateToPlayer(bool ok);
	void					_ReloadAllWindows();
	void					_ShowAuthFailureAlert(const std::string& error,
								const std::string& errorDescription);
	void					_SignOut();
	void					_StartLibrespotFromMessage(BMessage* message);
	void					_RegisterLibrespotOAuth();
	void					_StopLibrespotFromMessage();
	void					_ToggleLibrespotRunning();
	void					_ApplyLibrespotDevicePollResult(BMessage* message);
	void					_ApplyLibrespotPlaybackDecision(BMessage* message);
	void					_SendCurrentTrackTo(BWindow* window);
	void					_BroadcastPlayingTrack(const char* trackUri);
	void					_InstallDeskbarReplicant();
	void					_RemoveDeskbarReplicant();
	void					_InitAuth(bool silent = false);
	void					_ShowMissingClientIdAlert();
	bool					_InitSilentAuth(const HaifySettings& settings);
	int32					_BeginAuthGeneration();
	void					_StartInteractiveOAuth(int32 generation);
	void					_RefreshAccessToken(
								std::function<void(bool)> completion = nullptr,
								bool silent = true);
	void					_CompleteTokenRefresh(bool ok);
	void					_ScheduleTokenRefresh(int expiresIn);
	void					_StartLibrespot(LibrespotTransferMode mode,
								bool registerOAuth = false);
	bool					_ResolveLibrespotPath(
								const HaifySettings& settings,
								std::string& librespotPath);
	bool					_PrepareLibrespotOAuth(
								const HaifySettings& settings,
								bool registerOAuth);
	void					_AddLibrespotEventArgs(
								std::vector<std::string>& args);
	void					_AddLibrespotPlaybackArgs(
								std::vector<std::string>& args,
								const HaifySettings& settings,
								bool& hasEnableOAuthArgument);
	void					_AddLibrespotAdditionalArgs(
								std::vector<std::string>& args,
								const std::string& additionalArgs,
								bool& hasEnableOAuthArgument);
	void					_SpawnLibrespot(
								const std::vector<std::string>& args);
	void					_StopLibrespot();
	void					_ScheduleLibrespotTransfer(bigtime_t delay);
	void					_SchedulePlaybackPollAfterLibrespotTransfer(
								bigtime_t delay);
	void					_TryTransferPlaybackToLibrespot();
	void					_TransferPlaybackToLibrespotDevice(
								const char* deviceId);
	bool					_ReapLibrespot(bool wait);
	bool					_WriteLibrespotEventScript();
	void					_BroadcastSpotifyCapabilities();
	void					_RefreshSpotifyAccount();

	PlayerWindow*			fPlayerWindow;
	ArtworkWindow*			fArtworkWindow;
	BMessageRunner*			fLibrespotTransferTimer = nullptr;
	BMessageRunner*			fLibrespotPlaybackPollTimer = nullptr;
	BMessageRunner*			fTokenRefreshTimer = nullptr;
	OAuthCallbackServer*	fOAuthSrv;
	SpotifyApi*				fApi;
	SpotifyCapabilities	fCapabilities;
	std::shared_ptr<std::atomic_bool> fAlive;
	BLocker					fTokenLock;
	bool					fTokenRefreshInFlight = false;
	int32					fTokenGeneration = 0;
	std::vector<std::function<void(bool)>> fTokenRefreshWaiters;

	bool					fIsAuthenticated;
	bool					fIsQuitting = false;
	bool					fArtworkWindowOpen = false;
	bool					fLibrespotOAuthRegistration = false;
	int32					fLibrespotTransferAttempts = 0;
	LibrespotTransferMode	fLibrespotTransferMode = kLibrespotTransferAlways;
	pid_t					fLibrespotPid = -1;
	std::vector<ReplicantRegistration> fReplicants;
	BMessage				fLastReplicantState;
};

#endif
