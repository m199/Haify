#pragma once

#include <Window.h>
#include <string>

class BColumnListView;
class BStringView;
class BButton;
class ArtworkView;

class ArtistWindow : public BWindow {
public:
	explicit				ArtistWindow(const std::string& artistId);
	const std::string&		GetArtistId() const { return fArtistId; }
	virtual void			MessageReceived(BMessage* message);
	void					ShowTrackContextMenu(BPoint local, BPoint screen);

private:
	void					_LoadData();
	void					_LoadArtwork(const std::string& url);
	void					_SetPlayingTrack(const char* uri);

	std::string				fArtistId;
	std::string				fCurrentPlayingTrackUri;

	ArtworkView*			fArtworkView	= nullptr;
	BStringView*			fNameView		= nullptr;
	BStringView*			fFollowersView	= nullptr;
	BButton*				fFollowButton	= nullptr;
	bool					fFollowing		= false;
	BColumnListView*		fTrackList		= nullptr;
	BColumnListView*		fAlbumList		= nullptr;
};
