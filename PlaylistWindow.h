#ifndef PLAYLISTWINDOW_H
#define PLAYLISTWINDOW_H

#include <Window.h>
#include <Entry.h>
#include <string>
#include <vector>

class BButton;
class BFilePanel;
class BRow;
class BGroupView;
class BMenuBar;
class BMenuItem;
class BMessageRunner;
class BScrollView;
class BStringView;
class BTextControl;
class BTextView;
class BView;
class BStringColumn;
class TrackListView;

class PlaylistWindow : public BWindow {
public:
	PlaylistWindow(const char* playlistName, const char* uri, const char* coverUrl);
	virtual					~PlaylistWindow();
	virtual void			MessageReceived(BMessage* message);

	const std::string&      GetUri() const { return fUri; }
	void                    SetPlayingTrack(const char* trackUri);
	void					ShowContextMenu(BView* target, BPoint where, BPoint screenWhere);

private:
	struct EpisodeData {
		int32       number;
		std::string title;
		std::string description;
		std::string date;
		std::string duration;
		std::string trackUri;
		std::string searchText;
	};

	void					_InitMenu();
	void					_InitLayout(const char* playlistName);
	void					_LoadData(bool ignoreEpisodeCache = false);
	void					_LoadNextPage();
	void					_CheckLazyLoad();
	void					_LoadMoreEpisodes();
	void					_RebuildEpisodeList(const std::string& filter);
	void					_AppendEpisodeRows(size_t firstEpisode,
								const std::string& filter);
	void					_UpdateEpisodeInfo();
	void					_RenumberEpisodes();
	std::string				_AlbumId() const;
	std::string				_PlaylistId() const;
	void					_UpdateAlbumSavedState();
	void					_UpdateAlbumMenuItem();
	void					_ShowAlbumContextMenu(BPoint screenWhere);
	void					_ReloadArtwork();
	void					_ToggleAlbumSaved();
	void					_ShowPlaylistContextMenu(BPoint screenWhere);
	void					_DeletePlaylist();
	void					_ShowPlaylistDetailsDialog();
	void					_ChoosePlaylistCover();
	void					_UploadPlaylistCover(const entry_ref& ref);
	void					_ClearPlaylist();
	void					_FinishClearPlaylist(bool success,
								const std::string& snapshotId);
	void					_MoveSelectedItem(int32 delta);
	void					_BeginTrackReorder(int32 sourceIndex,
								int32 rangeLength, int32 insertBefore);
	void					_FinishTrackReorder(bool success,
								const std::string& snapshotId);
	void					_RenumberPlaylistRows();
	void					_FinishTrackRemoval(bool success);
	void					_RefreshPlaylistSnapshot();
	void					_UpdatePlaylistTrackInfo();
	void					_UpdatePlaylistMenuState();
	bool					_HasEpisode(const std::string& uri,
								const std::string& title,
								const std::string& date,
								const std::string& duration) const;
	void					_RefreshPodcastHead(int32 offset);
	void					_FinishPodcastHeadRefresh();
	bool					_LoadCache();
	void					_SaveCache();
	void					_WriteCacheNow();
	void					_DeleteCache();

	BMenuBar*				fMenuBar = nullptr;
	BMenuItem*				fAlbumSaveItem		= nullptr;
	BMenuItem*				fPlaylistDeleteItem	= nullptr;
	BMenuItem*				fPlaylistEditItem	= nullptr;
	BMenuItem*				fPlaylistCoverItem	= nullptr;
	BMenuItem*				fPlaylistClearItem	= nullptr;
	BView*					fCoverView = nullptr;
	BTextView*				fPlaylistName = nullptr;
	BStringView*			fPlaylistInfo = nullptr;
	TrackListView*		    fTrackList = nullptr;
	BStringColumn*			fBpmColumn = nullptr;
	BStringColumn*			fKeyColumn = nullptr;
	BButton*				fSubscribeButton	= nullptr;
	BButton*				fAlbumSaveButton	= nullptr;
	BTextControl*			fSearchBox			= nullptr;
	BTextView*				fPodcastSearchInfo	= nullptr;
	bool					fIsSubscribed		= false;
	bool					fSubscriptionKnown	= false;
	bool					fSubscriptionPending	= false;
	BTextView*				fDescriptionView  = nullptr;
	BScrollView*			fDescriptionScroll= nullptr;
	BFilePanel*				fPlaylistCoverPanel = nullptr;
	std::string             fUri;
	std::string             fCoverUrl;
	std::string				fPlaylistSnapshotId;
	std::string				fPlaylistDescription;
	std::string				fPlaylistOwnerId;
	std::string				fCurrentUserId;
	std::string				fCurrentUserLegacyId;
	bool					fPlaylistPublic = false;
	bool					fPlaylistOwned = false;
	std::string				fCachedPlaylistSnapshotId;
	std::string				fCurrentPlayingTrackUri;
	BMessageRunner*			fLazyLoadRunner	= nullptr;
	BMessageRunner*			fCacheSaveRunner = nullptr;
	BMessageRunner*			fEpisodeSearchRunner = nullptr;
	BMessageRunner*			fEpisodeSearchRetryRunner = nullptr;
	bool					fPageLoading	= false;
	bool					fPageHasMore	= false;
	int32					fPageOffset		= 0;
	int32					fPageTotal		= 0;
	int32					fPageBatchSize	= 50;
	bool					fPodcastHeadRefreshing = false;
	bool					fAlbumSaved = false;
	bool					fAlbumSavedKnown = false;
	bool					fAlbumSavePending = false;
	bool					fPlaylistDeletePending = false;
	struct PendingTrackRemoval {
		BRow*	row;
		int32	listIndex;
		int32	playlistPosition;
		bool	selected;
	};
	std::vector<PendingTrackRemoval> fPendingTrackRemovals;
	bool					fTrackRemovalPending = false;
	struct PendingTrackReorder {
		std::vector<BRow*>	rows;
		std::vector<bool>	selected;
		int32				sourceIndex = -1;
		int32				targetIndex = -1;
	};
	PendingTrackReorder		fPendingTrackReorder;
	bool					fTrackReorderPending = false;
	struct PendingPlaylistClear {
		std::vector<BRow*>	rows;
		std::vector<bool>	selected;
		int32				pageOffset = 0;
		int32				pageTotal = 0;
		bool				pageHasMore = false;
		std::string			snapshotId;
	};
	PendingPlaylistClear	fPendingPlaylistClear;
	bool					fPlaylistClearPending = false;
	int32					fEpisodeOffset	= 0;
	int32					fEpisodeTotal	= 0;
	std::string				fEpisodeSearchFilter;
	int32					fEpisodeSearchGeneration = 0;
	int32					fEpisodeSearchRetryCount = 0;
	bool					fEpisodeSearchPaging = false;
	bool					fEpisodeSearchWaitingRetry = false;
	bool					fEpisodeSearchFailed = false;
	std::vector<EpisodeData> fEpisodes;
	std::vector<EpisodeData> fPendingPodcastHeadEpisodes;
};

#endif
