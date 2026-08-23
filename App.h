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
	kLibrespotTransferIfIdle,
	kLibrespotTransferNever
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
	void					_ShowArtworkWindow();
	void					_SendCurrentTrackTo(BWindow* window);
	void					_BroadcastPlayingTrack(const char* trackUri);
	void					_InstallDeskbarReplicant();
	void					_RemoveDeskbarReplicant();
	void					_InitAuth(bool silent = false);
	void					_RefreshAccessToken(
								std::function<void(bool)> completion = nullptr,
								bool silent = true);
	void					_CompleteTokenRefresh(bool ok);
	void					_ScheduleTokenRefresh(int expiresIn);
	void					_StartLibrespot(LibrespotTransferMode mode,
								bool registerOAuth = false);
	void					_StopLibrespot();
	void					_ScheduleLibrespotTransfer(bigtime_t delay);
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
