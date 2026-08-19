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
	void					_ScheduleVerifyPoll(bigtime_t delay);
	void					_ApplyPredictedNext();
	void					_ApplyPlaybackMessage(BMessage* message);
	void					_PlayUri(BMessage* message);
	void					_ApplyOptimisticPlay(BMessage* message);
	void					_ReadLibrespotEvent();
	void					_ApplyLibrespotEvent(
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
	std::string				fLastArtworkUrl;
	std::string				fVolumeDeviceId;
	std::string				fCurrentTrackUri;
	std::string				fQueueTrackUri;
	std::string				fLastLibrespotTrackEventId;
	std::string				fLastLibrespotPlaybackEventId;
	std::string				fOptimisticSourceTrackUri;
	bigtime_t				fOptimisticUntilUs = 0;
	bool					fHasPendingLibrespotTrack = false;
	BMessage				fPendingLibrespotTrack;
	BMessage				fPredictedNext;
};

#endif
