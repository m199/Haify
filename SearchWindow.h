#ifndef SEARCHWINDOW_H
#define SEARCHWINDOW_H

#include <Window.h>
#include <string>

class BTextControl;
class BCheckBox;
class BColumnListView;
class BStringView;

class SearchWindow : public BWindow {
public:
								SearchWindow();
	virtual bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);

private:
	void						_InitLayout();
	void						_DoSearch();
	std::string					_BuildTypeParam() const;
	void						_SetAllMode();
	void						_EnsureValidSelection();
	void						_UpdateCapabilityFilters();
	bool						_AudiobooksEnabled() const;

	BTextControl*				fSearchBar		= nullptr;
	BCheckBox*					fChkAll			= nullptr;
	BCheckBox*					fChkTracks		= nullptr;
	BCheckBox*					fChkArtists		= nullptr;
	BCheckBox*					fChkAlbums		= nullptr;
	BCheckBox*					fChkPlaylists	= nullptr;
	BCheckBox*					fChkShows		= nullptr;
	BCheckBox*					fChkEpisodes	= nullptr;
	BCheckBox*					fChkAudiobooks	= nullptr;
	BColumnListView*			fList			= nullptr;
	BStringView*				fStatusLabel	= nullptr;
	std::string					fCurrentTrackUri;
	int32						fSearchGeneration = 0;

	bool						fUpdatingAll	= false;
};

#endif
