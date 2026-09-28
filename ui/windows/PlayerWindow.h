#ifndef PLAYERWINDOW_H
#define PLAYERWINDOW_H

#include "playback/LibrespotEventState.h"
#include "playback/LocalPlaybackPresentation.h"
#include "playback/PlaybackVolumeState.h"
#include "playback/PlaybackTimeline.h"
#include "playback/PlaybackPollState.h"
#include "playback/AudiobookEndState.h"
#include "playback/AudiobookPlaybackContext.h"
#include "messages/PlaybackStateMessages.h"

#include <Message.h>
#include <Window.h>
#include <map>
#include <string>
#include <vector>

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
	void					_InitMenu();
	void					_InitLayout();
	void					_PollPlayback();
	void					_SchedulePlaybackPoll(bigtime_t delay);
	void					_FetchQueuePrediction();
	void					_HandlePlaybackTick();
	bool					_ShouldFetchQueuePrediction(
								int32 remainingMs) const;
	void					_ApplyFinishedTrackTransition();
	bool					_FinishAudiobookChapter();
	bool					_ConsumePlaybackTransition(const PlaybackMessageData& update);
	void					_ApplyLibrespotEndEvent(
								const std::map<std::string, std::string>& fields);
	void					_ScheduleVerifyPoll(bigtime_t delay);
	void					_ApplyPredictedNext();
	void					_ApplyPlaybackMessage(BMessage* message);
	PlaybackMessageData		_ReadPlaybackMessage(BMessage* message) const;
	bool					_ShouldDeferPlaybackUpdate(
								const PlaybackMessageData& update);
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
	bool					_HandlePlaybackDeviceMessage(BMessage* message);
	bool					_HandleTransportMessage(BMessage* message);
	bool					_HandleInterfaceMessage(BMessage* message);
	bool					_ForwardAppMessage(BMessage* message);
	bool					_HandleAccountDeviceMessage(BMessage* message);
	void					_ApplyPlaybackPollResult(BMessage* message);
	void					_ApplyAudiobookContextResult(BMessage* message);
	void					_PlayUri(BMessage* message);
	bool					_EnsurePlaybackDeviceThen(BMessage* message);
	void					_FetchPlaybackDevicesForPrompt();
	void					_ApplyPlaybackDeviceChoices(BMessage* message);
	void					_ShowPlaybackDevicePrompt(BMessage* message);
	void					_ApplyPlaybackDeviceSelection(BMessage* message);
	void					_StartLocalPlaybackDevice();
	void					_RetryLocalPlaybackDevice();
	void					_ExecutePendingPlaybackCommand(
								const std::string& deviceId);
	void					_ExecutePlaybackCommand(BMessage* message);
	void					_PlayUriNow(BMessage* message);
	void					_ResumePlayback(const std::string& deviceId);
	void					_ApplyQueuePrediction(BMessage* message);
	void					_ApplyVerifyPoll();
	void					_TogglePlayPause();
	bool					_PlayNextAudiobookChapter();
	void					_SkipNextTrack();
	void					_SkipPreviousTrack();
	void					_SetVolumeFromMessage(BMessage* message);
	void					_ToggleMute();
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
	bool					_ApplyOptimisticPlay(BMessage* message);
	void					_ResolveAudiobookContextForPlayback(
								const std::string& trackUri,
								const std::string& parentKind,
								const std::string& openUri,
								bool metadataIncomplete);
	void					_FillReplicantStateMessage(BMessage& message,
								int32 progressMs) const;
	void					_ReadLibrespotEvent();
	void					_ClearPendingLibrespotTrack();
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
	void					_ApplyVolumeCommand(const PlaybackVolumeCommand& command,
								SpotifyApi* api);
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
	BMessageRunner*			fLocalPlaybackDeviceTimer = nullptr;

	bool					fHasPredictedNext = false;
	bool					fQueueRequestPending = false;
	bool					fHasPendingPlaybackCommand = false;
	bool					fPlaybackDevicePromptOpen = false;
	int32					fLocalPlaybackDeviceAttempts = 0;
	int64					fPreviousLocalPlaybackGeneration = 0;
	bool					fShuffleOn     = false;
	PlaybackVolumeState		fVolume;
	PlaybackMetadata		fMetadata;
	PlaybackTimeline		fTimeline;
	PlaybackPollState		fPlaybackPoll;
	AudiobookEndState		fAudiobookEnd;
	std::string				fRepeatState   = "off";
	std::string				fCurrentDeviceId;
	std::string				fCurrentDeviceName;
	std::string				fCurrentDeviceType;
	std::string				fCurrentTrackUri;
	std::string				fQueueTrackUri;
	LibrespotEventState		fLibrespotEvents;
	LocalPlaybackPresentation fLocalPlaybackPresentation;
	std::string				fOptimisticSourceTrackUri;
	bigtime_t				fOptimisticUntilUs = 0;
	bool					fAudiobookContextRequestPending = false;
	std::string				fAudiobookContextRequestTrackUri;
	std::string				fLastAudiobookContextLookupTrackUri;
	AudiobookPlaybackContext fAudiobookPlayback;
	bool					fHasPendingLibrespotTrack = false;
	BMessage				fPendingLibrespotTrack;
	BMessage				fPredictedNext;
	BMessage				fPendingPlaybackCommand;
};

#endif
