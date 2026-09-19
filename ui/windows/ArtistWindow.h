#pragma once

#include <Window.h>
#include <string>

class BColumnListView;
class BMessageRunner;
class BStringView;
class BButton;
class ArtworkView;

class ArtistWindow : public BWindow {
public:
	explicit				ArtistWindow(const std::string& artistId);
	virtual					~ArtistWindow();
	const std::string&		GetArtistId() const { return fArtistId; }
	virtual void			MessageReceived(BMessage* message);
	void					ShowTrackContextMenu(BPoint local, BPoint screen);

private:
	void					_LoadData();
	void					_LoadTopTracks(bool resetRetryCount);
	void					_LoadArtwork(const std::string& url);
	void					_ScheduleTopTracksRetry(int32 generation);
	void					_SetPlayingTrack(const char* uri);
	bool					_HandleArtistStateMessage(BMessage* message);
	bool					_HandleTrackActionMessage(BMessage* message);
	bool					_HandleAlbumActionMessage(BMessage* message);
	void					_ApplyLibraryChanged(BMessage* message);
	void					_UpdateFollowing(bool following);
	void					_ToggleFollowing();
	void					_ApplyArtistMetadata(BMessage* message);
	void					_ApplyTopTracks(BMessage* message);
	void					_ApplyAlbums(BMessage* message);
	void					_RetryTopTracks(BMessage* message);
	void					_PlayTrack(BMessage* message);
	void					_OpenSelectedAlbum();
	void					_LikeTrack(BMessage* message);
	void					_AddTrackToPlaylist(BMessage* message);
	void					_RemoveTrackFromLibrary(BMessage* message);
	void					_ShowTrackContextMenuResult(BMessage* message);
	void					_SaveAlbum(BMessage* message);
	std::string				_PlayTrackUri(BMessage* message) const;
	void					_AddPlayingTrackMetadata(BMessage& play,
								const std::string& uri) const;

	std::string				fArtistId;
	std::string				fCurrentPlayingTrackUri;
	int32					fTopTracksGeneration = 0;
	int32					fTopTracksRetryCount = 0;

	ArtworkView*			fArtworkView	= nullptr;
	BStringView*			fNameView		= nullptr;
	BStringView*			fFollowersView	= nullptr;
	BStringView*			fTracksLabel	= nullptr;
	BButton*				fFollowButton	= nullptr;
	BMessageRunner*			fTopTracksRetryRunner = nullptr;
	bool					fFollowing		= false;
	BColumnListView*		fTrackList		= nullptr;
	BColumnListView*		fAlbumList		= nullptr;
};
