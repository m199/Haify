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
	void						_ApplyTypeSelection();
	void						_ApplyCapabilityChange();
	void						_ApplyResults(BMessage* message);
	void						_ApplyPlayingTrack(BMessage* message);
	void						_ForwardOpen(BMessage* message);
	void						_PrepareContextMenu(BMessage* message);
	void						_ShowContextMenu(BMessage* message);
	void						_ApplyLibraryActionResult(BMessage* message);
	void						_ApplyPlaylistActionResult(BMessage* message);
	void						_PlayTrackFromMessage(BMessage* message);
	void						_ForwardPlay(BMessage* message);

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
