#ifndef PLAYLISTWINDOW_H
#define PLAYLISTWINDOW_H

#include "playlist/PlaylistEpisode.h"

#include <Window.h>
#include <Entry.h>
#include <string>
#include <utility>
#include <vector>

class BButton;
class BFilePanel;
class BMessage;
class BMessenger;
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
class MediaDescriptionView;
class SpotifyApi;

class PlaylistWindow : public BWindow {
public:
	PlaylistWindow(const char* playlistName, const char* uri, const char* coverUrl);
	virtual					~PlaylistWindow();
	virtual void			MessageReceived(BMessage* message);

	const std::string&      GetUri() const { return fUri; }
	void                    SetPlayingTrack(const char* trackUri);
	void					ShowContextMenu(BView* target, BPoint where, BPoint screenWhere);

private:
	void					_InitMenu();
	void					_InitLayout(const char* playlistName);
	void					_ShowRenamePlaylistDialog(BMessage* message);
	void					_RenamePlaylist(BMessage* message);
	void					_PlayTrackFromMessage(BMessage* message);
	void					_ShowPlayableContextMenu(BMessage* message);
	void					_PlayContextUri();
	void					_PlayCurrentTrack();
	void					_AddFollowingTrackQueue(BMessage& play,
								const std::string& trackUri) const;
	void					_RemoveTrackFromLibrary(BMessage* message);
	void					_SavePlayableItemToLibrary(BMessage* message);
	void					_AddMessageTrackToPlaylist(BMessage* message);
	void					_RemoveSelectedTracksFromPlaylist(
								BMessage* message);
	void					_ApplyTrackRemovalResult(BMessage* message);
	void					_ApplyTrackReorderResult(BMessage* message);
	void					_ApplyClearPlaylistResult(BMessage* message);
	void					_ApplyPlaylistAddResult(BMessage* message);
	void					_HandleTrackDrop(BMessage* message);
	void					_HandleTrackReorderDrop(BMessage* message,
								int32 sourceIndex);
	void					_AddDroppedPlayableItem(BMessage* message,
								const char* trackUri);
	void					_ApplyPageLoadFailure(BMessage* message);
	void					_ApplyPlaylistMetadata(BMessage* message);
	void					_ApplyPlaylistEditResult(BMessage* message);
	void					_ApplyLibraryChange(BMessage* message);
	void					_ApplyAlbumSavedState(BMessage* message);
	void					_ApplySubscriptionState(BMessage* message);
	void					_ApplyPlaylistUserState(BMessage* message);
	void					_UpdatePlaylistDetails(BMessage* message);
	void					_ApplyPlaylistCoverUploadResult(
								BMessage* message);
	void					_NotifyPlaylistDeleted();
	void					_ApplyEpisodePage(BMessage* message);
	size_t					_AppendEpisodePageItems(BMessage* message);
	void					_ApplyPodcastHeadPage(BMessage* message);
	void					_TogglePodcastSubscription();
	void					_ScheduleEpisodeSearch();
	void					_ApplyEpisodeSearch(BMessage* message);
	void					_RetryEpisodeSearch(BMessage* message);
	void					_ApplyEpisodeSelection(BMessage* message);
	bool					_HandleTrackActionMessage(BMessage* message);
	bool					_HandleDataMessage(BMessage* message);
	bool					_HandlePlaylistEditMessage(BMessage* message);
	bool					_HandlePlaylistMenuMessage(BMessage* message);
	bool					_HandlePodcastMessage(BMessage* message);
	bool					_HandleAppForwardMessage(BMessage* message);
	void					_ApplyPlaylistRemoveMarked(BMessage* message);
	void					_ReloadDataIfIdle();
	void					_RefreshEpisodes();
	void					_SaveCacheNowFromMessage();
	void					_ApplyTitleUpdate(BMessage* message);
	void					_UploadPlaylistCoverFromMessage(BMessage* message);
	void					_ShowPlaylistSnapshotConflict();
	void					_ApplyPlayingTrackUpdate(BMessage* message);
	void					_ApplyCoverUpdate(BMessage* message);
	void					_ShowAlbumMenuFromMessage(BMessage* message);
	void					_ShowPlaylistMenuFromMessage(BMessage* message);
	void					_ApplyPlaylistDeleteFailed();
	bool					_CollectPendingTrackRemovals(
								std::vector<std::pair<std::string, int>>&
									items);
	std::vector<std::string> _KnownPlaylistUrisForRemoval() const;
	void					_RemovePendingTrackRows();
	void					_ApplyTrackPage(BMessage* message);
	void					_AddTrackPageRows(BMessage* message);
	void					_LoadData(bool ignoreEpisodeCache = false);
	bool					_PrepareCollectionLoad(SpotifyApi& api);
	bool					_PreparePlaylistLoad(SpotifyApi& api,
								const std::string& playlistId);
	bool					_PrepareAlbumLoad(SpotifyApi& api,
								const std::string& albumId);
	bool					_PrepareShowLoad(SpotifyApi& api,
								const std::string& showId,
								bool ignoreEpisodeCache);
	void					_LoadNextPage();
	void					_LoadCollectionPage(SpotifyApi& api,
								const BMessenger& messenger, int32 offset,
								int32 limit, int32 searchGeneration);
	void					_LoadPlaylistPage(SpotifyApi& api,
								const BMessenger& messenger,
								const std::string& playlistId, int32 offset,
								int32 limit, int32 searchGeneration);
	void					_LoadAlbumPage(SpotifyApi& api,
								const BMessenger& messenger,
								const std::string& albumId, int32 offset,
								int32 limit, int32 searchGeneration);
	void					_LoadShowPage(SpotifyApi& api,
								const BMessenger& messenger,
								const std::string& showId, int32 offset,
								int32 limit, int32 searchGeneration);
	void					_CheckLazyLoad();
	bool					_CanLazyLoadPage() const;
	bool					_ShouldLoadNextPageForScroll() const;
	void					_LoadMoreEpisodes();
	void					_RebuildEpisodeList(const std::string& filter);
	void					_AppendEpisodeRows(size_t firstEpisode,
								const std::string& filter);
	void					_UpdateEpisodeInfo();
	void					_RenumberEpisodes();
	void					_AddPodcastNowPlayingContext(BMessage& play) const;
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
	bool					_CanMoveSelectedItems(int32 delta) const;
	bool					_SelectedRowSpan(int32& source, int32& last,
								int32& selectedCount) const;
	void					_ShowContiguousSelectionAlert() const;
	void					_BeginTrackReorder(int32 sourceIndex,
								int32 rangeLength, int32 insertBefore);
	bool					_CanBeginTrackReorder(
								const std::string& playlistId,
								int32 sourceIndex, int32 rangeLength) const;
	bool					_BuildPendingTrackReorder(int32 sourceIndex,
								int32 rangeLength, int32 targetIndex);
	bool					_IsRowSelected(BRow* row) const;
	void					_ApplyPendingTrackReorder();
	void					_FinishTrackReorder(bool success,
								const std::string& snapshotId);
	void					_RenumberPlaylistRows();
	void					_FinishTrackRemoval(bool success);
	void					_ApplyFinishedTrackRemoval();
	void					_RollbackFinishedTrackRemoval();
	std::vector<int32>		_RemovedPlaylistPositions() const;
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
	bool					_LoadTrackCache(bool isPlaylist);
	bool					_LoadShowCache();
	void					_SaveCache();
	void					_WriteCacheNow();
	bool					_WriteTrackCache(bool isPlaylist);
	void					_WriteShowCache();
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
	MediaDescriptionView*	fDescriptionView  = nullptr;
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
	std::vector<PlaylistEpisode> fEpisodes;
	std::vector<PlaylistEpisode> fPendingPodcastHeadEpisodes;
};

#endif
