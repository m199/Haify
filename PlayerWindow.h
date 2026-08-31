#ifndef PLAYERWINDOW_H
#define PLAYERWINDOW_H

#include <Message.h>
#include <Window.h>
#include <map>
#include <string>

class BMenu;
class BMenuBar;
class BMenuItem;
class BMessageRunner;
class PlayerBarView;
class SpotifyApi;

class PlayerWindow : public BWindow {
public:
	                        PlayerWindow();
	virtual					~PlayerWindow();
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage* message);
	virtual void			FrameResized(float width, float height);
	virtual void			MenusBeginning();

private:
	struct PlaybackMessageData {
		bool isPlaying = false;
		int32 progressMs = 0;
		int32 durationMs = 0;
		int32 volumePct = -1;
		bool optimistic = false;
		bool preserveCurrentArtwork = false;
		bool volumeAuthoritative = true;
		bool hasItem = true;
		bool knownItemState = false;
		std::string trackUri;
		std::string repeatState;
		bool shuffleState = false;
		std::string effectiveTitle;
		std::string effectiveArtist;
		std::string effectiveAlbumId;
		std::string effectiveArtistId;
		std::string effectiveItemKind;
		std::string effectiveOpenUri;
		std::string effectiveParentUri;
		std::string effectiveParentKind;
		std::string effectiveShowId;
		std::string effectiveAudiobookId;
		std::string effectiveArtworkUrl;
		std::string deviceId;
		std::string deviceName;
		std::string deviceType;
	};

	void					_InitMenu();
	void					_InitLayout();
	void					_PollPlayback();
	void					_SchedulePlaybackPoll(bigtime_t delay);
	void					_FetchQueuePrediction();
	void					_HandlePlaybackTick();
	void					_ScheduleVerifyPoll(bigtime_t delay);
	void					_ApplyPredictedNext();
	void					_ApplyPlaybackMessage(BMessage* message);
	PlaybackMessageData		_ReadPlaybackMessage(BMessage* message) const;
	bool					_ShouldDeferPlaybackUpdate(
								const PlaybackMessageData& update) const;
	void					_ApplyPlaybackSeekGuard(
								PlaybackMessageData& update,
								bool trackChanged);
	void					_ApplyPlaybackVolume(
								PlaybackMessageData& update);
	void					_ResolvePlaybackMetadata(
								PlaybackMessageData& update,
								bool trackChanged);
	void					_StorePlaybackMetadata(
								const PlaybackMessageData& update);
	void					_StorePlaybackState(
								const PlaybackMessageData& update);
	void					_ApplyPlayerBarState(
								const PlaybackMessageData& update);
	void					_ApplyTrackChangedState(
								const PlaybackMessageData& update);
	void					_PublishPlaybackReplicantState(int32 progressMs);
	bool					_HandlePlaybackMessage(BMessage* message);
	bool					_HandleTransportMessage(BMessage* message);
	bool					_HandleInterfaceMessage(BMessage* message);
	bool					_ForwardAppMessage(BMessage* message);
	bool					_HandleAccountDeviceMessage(BMessage* message);
	void					_ApplyPlaybackPollResult(BMessage* message);
	void					_ApplyAudiobookContextResult(BMessage* message);
	void					_PlayUri(BMessage* message);
	void					_ApplyQueuePrediction(BMessage* message);
	void					_ApplyVerifyPoll();
	void					_TogglePlayPause();
	void					_SkipNextTrack();
	void					_SkipPreviousTrack();
	void					_SetVolumeFromMessage(BMessage* message);
	void					_ToggleMute();
	bool					_RestoreMutedVolumeIfNeeded(SpotifyApi* api);
	void					_ApplyMuteToggle(BMessage* message);
	void					_ToggleShuffle();
	void					_ToggleRepeat();
	void					_ToggleLibrespotAutostart();
	void					_SeekFromMessage(BMessage* message);
	void					_ApplySeekBarColor(BMessage* message);
	void					_ApplyDroppedSeekBarColor(BMessage* message);
	void					_SaveCurrentTrack();
	void					_PrepareAddTrackMenu(BMessage* message);
	void					_ShowAddTrackMenuFromMessage(BMessage* message);
	void					_ApplyAuthStatus(BMessage* message);
	void					_ApplyDeviceList(BMessage* message);
	void					_TransferToDevice(BMessage* message);
	void					_ApplyOptimisticPlay(BMessage* message);
	void					_ResolveAudiobookContextForPlayback(
								const std::string& trackUri,
								const std::string& parentKind,
								const std::string& openUri);
	void					_FillReplicantStateMessage(BMessage& message,
								int32 progressMs) const;
	void					_ReadLibrespotEvent();
	void					_ApplyLibrespotEvent(
								const std::map<std::string, std::string>& fields);
	void					_ApplyLibrespotTrackChanged(
								const std::map<std::string, std::string>& fields);
	void					_ApplyLibrespotPositionEvent(
								const std::string& event,
								const std::map<std::string, std::string>& fields);
	void					_ApplyLibrespotShuffleChanged(
								const std::map<std::string, std::string>& fields);
	void					_ApplyLibrespotRepeatChanged(
								const std::map<std::string, std::string>& fields);
	void					_ApplyLibrespotVolumeChanged(
								const std::map<std::string, std::string>& fields);
	void					_SetVolumeOptimistically(int32 volume);
	bool					_AcceptReportedVolume(int32 volume);
	void					_PublishReplicantState();
	void					_ShowAddTrackMenu(const std::string& trackUri,
								BPoint screenWhere, bool liked);
	void					_RemoveCurrentTrackFromLikedSongs(
								const std::string& trackUri);
	void					_UpdateLibrespotMenuItems();
	void					_ApplySizeLimits();
	void					_ShowAboutWindow();

	BMenuBar*				fMenuBar       = nullptr;
	BMenuItem*				fAuthItem      = nullptr;
	BMenuItem*				fLibrespotToggleItem = nullptr;
	BMenuItem*				fAutostartItem = nullptr;
	BMenu*					fDeviceMenu    = nullptr;
	PlayerBarView*			fPlayerBar     = nullptr;
	BMessageRunner*			fPollTimer     = nullptr;
	BMessageRunner*			fPlaybackTimer = nullptr;
	BMessageRunner*			fVerifyTimer   = nullptr;

	bool					fIsPlaying     = false;
	bool					fHasPredictedNext = false;
	bool					fQueueRequestPending = false;
	bool					fPlaybackRequestPending = false;
	bool					fHasPlaybackState = false;
	bigtime_t				fStartupEmptyPlaybackRetryUntilUs = 0;
	int32					fPlaybackPollFailures = 0;
	bool					fShuffleOn     = false;
	int32					fProgressMs    = 0;
	int32					fDurationMs    = 0;
	int32					fVolumePct     = -1;
	int32					fLastNonZeroVolume = 50;
	bool					fHasLastNonZeroVolume = false;
	bool					fMutedByHaify = false;
	int32					fVolumeTargetPct = -1;
	bigtime_t				fVolumeGuardUntilUs = 0;
	bigtime_t				fLastPlaybackSyncUs = 0;
	int32					fSeekTargetMs = 0;
	bigtime_t				fSeekGuardUntilUs = 0;
	std::string				fRepeatState   = "off";
	std::string				fCurrentTitle;
	std::string				fCurrentArtist;
	std::string				fCurrentAlbumId;
	std::string				fCurrentArtistId;
	std::string				fCurrentItemKind;
	std::string				fCurrentPrimaryOpenUri;
	std::string				fCurrentParentUri;
	std::string				fCurrentParentKind;
	std::string				fCurrentShowId;
	std::string				fCurrentAudiobookId;
	std::string				fLastArtworkUrl;
	std::string				fVolumeDeviceId;
	std::string				fCurrentDeviceId;
	std::string				fCurrentDeviceName;
	std::string				fCurrentDeviceType;
	std::string				fCurrentTrackUri;
	std::string				fQueueTrackUri;
	std::string				fLastLibrespotTrackEventId;
	std::string				fLastLibrespotPlaybackEventId;
	std::string				fOptimisticSourceTrackUri;
	bigtime_t				fOptimisticUntilUs = 0;
	bool					fAudiobookContextRequestPending = false;
	std::string				fAudiobookContextRequestTrackUri;
	std::string				fLastAudiobookContextLookupTrackUri;
	bool					fHasPendingLibrespotTrack = false;
	BMessage				fPendingLibrespotTrack;
	BMessage				fPredictedNext;
};

#endif
