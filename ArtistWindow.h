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

	std::string				fArtistId;
	std::string				fCurrentPlayingTrackUri;
	int32					fTopTracksGeneration = 0;
	int32					fTopTracksRetryCount = 0;

	ArtworkView*			fArtworkView	= nullptr;
	BStringView*			fNameView		= nullptr;
	BStringView*			fFollowersView	= nullptr;
	BButton*				fFollowButton	= nullptr;
	BMessageRunner*			fTopTracksRetryRunner = nullptr;
	bool					fFollowing		= false;
	BColumnListView*		fTrackList		= nullptr;
	BColumnListView*		fAlbumList		= nullptr;
};
