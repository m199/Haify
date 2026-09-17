#ifndef PLAYLISTWINDOW_H
#define PLAYLISTWINDOW_H

#include "playlist/PlaylistEpisode.h"
#include "DragItem.h"
#include "playlist/PlaylistMetadataController.h"
#include "playlist/PlaylistPageState.h"
#include "playlist/PlaylistRemovalController.h"
#include "playlist/PlaylistReorderController.h"
#include "playlist/PlaylistWriteController.h"
#include "playlist/PlaylistCoverController.h"
#include "playlist/PlaylistPresentation.h"
#include "spotify/SpotifyUri.h"

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
	virtual bool			QuitRequested() override;

	const std::string&      GetUri() const { return fUri; }
	const std::string&      GetPlaylistSnapshot() const { return fPlaylistSnapshotId; }
	void                    SetPlayingTrack(const char* trackUri);
	void					SetCoverUrl(const std::string& coverUrl);
	void					ShowContextMenu(BView* target, BPoint where, BPoint screenWhere);

private:
	void					_InitMenu();
	void					_InitLayout(const char* playlistName);
	void					_InitTrackList(SpotifyItemKind kind);
	void					_InitPodcastLayout();
	void					_InitAlbumLayout(float artworkSize);
	void					_InitDefaultLayout();
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
	void					_ApplyPlaylistWriteResult(BMessage* message);
	void					_HandleTrackDrop(BMessage* message);
	void					_HandleTrackReorderDrop(BMessage* message,
								const MessageContracts::DragItem& item);
	void					_AddDroppedPlayableItem(BMessage* message,
								const char* trackUri);
	void					_ApplyPageLoadFailure(BMessage* message);
	void					_ApplyMetadataResult(BMessage* message);
	void					_ApplyPlaylistMetadata(const PlaylistMetadataResult& result);
	void					_ApplyPlaylistEditResult(BMessage* message);
	void					_ApplyLibraryChange(BMessage* message);
	void					_ApplyAlbumSavedState(BMessage* message);
	void					_ApplySubscriptionState(BMessage* message);
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
	void					_ApplyPlaylistSnapshot(BMessage* message);
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
	bool					_HandleTrackPlaybackActionMessage(
								BMessage* message);
	bool					_HandleTrackLibraryActionMessage(
								BMessage* message);
	bool					_HandleTrackDragActionMessage(BMessage* message);
	bool					_CollectPendingTrackRemovals(
								std::vector<std::pair<std::string, int>>&
									items);
	std::vector<std::string> _VisiblePlaylistUris() const;
	void					_RemovePendingTrackRows();
	void					_ApplyTrackPage(BMessage* message);
	void					_AddTrackPageRows(BMessage* message);
	void					_LoadData(bool ignoreEpisodeCache = false);
	void					_ResetLoadState();
	bool					_PrepareCollectionLoad(SpotifyApi& api);
	bool					_PreparePlaylistLoad(SpotifyApi& api,
								const std::string& playlistId);
	bool					_PrepareAlbumLoad(SpotifyApi& api,
								const std::string& albumId);
	bool					_PrepareShowLoad(SpotifyApi& api,
								const std::string& showId,
								bool ignoreEpisodeCache);
	void					_LoadNextPage();
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
	void					_FinishClearPlaylist(bool success);
	PlaylistMenuState _PlaylistMenuState() const;
	bool _PlaylistMutationPending() const;
	PlaylistWriteContext _PlaylistWriteContext() const;
	void _SendPlaylistWrite(const PlaylistWriteCommand& command);
	void					_MoveSelectedItem(int32 delta);
	bool					_CanMoveSelectedItems(int32 delta) const;
	bool					_SelectedRowSpan(int32& source, int32& last,
								int32& selectedCount) const;
	void					_ShowContiguousSelectionAlert() const;
	void					_BeginTrackReorder(const std::vector<int32_t>& indices,
								int32 insertBefore);
	void					_SendTrackReorder(const PlaylistReorderCommand& command);
	bool					_BuildPendingTrackReorder(const std::vector<int32_t>& indices);
	bool					_IsRowSelected(BRow* row) const;
	void					_ApplyPendingTrackReorder(const std::vector<int32_t>& indices);
	void					_FinishTrackReorder(const PlaylistReorderUpdate& update);
	void					_ApplyPlaylistPositions(const std::vector<int32_t>& positions);
	void					_FinishTrackRemoval(const PlaylistRemovalUpdate& update);
	void					_ApplyFinishedTrackRemoval(const PlaylistRemovalUpdate& update);
	void					_RollbackFinishedTrackRemoval();
	void					_RefreshPlaylistSnapshot();
	void					_UpdatePlaylistTrackInfo();
	void					_UpdatePlaylistMenuState();
	void					_UpdateTrackDropMarkerMode();
	void					_RefreshPodcastHead(int32 offset);
	void					_FinishPodcastHeadRefresh();
	bool					_LoadCache();
	bool					_LoadTrackCache();
	bool					_LoadShowCache();
	void					_SaveCache();
	void					_WriteCacheNow();
	bool					_WriteTrackCache();
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
	PlaylistMetadataState	fMetadata;
	std::string				fCachedPlaylistSnapshotId;
	std::string				fCurrentPlayingTrackUri;
	BMessageRunner*			fLazyLoadRunner	= nullptr;
	BMessageRunner*			fCacheSaveRunner = nullptr;
	BMessageRunner*			fEpisodeSearchRunner = nullptr;
	BMessageRunner*			fEpisodeSearchRetryRunner = nullptr;
	PlaylistPageState		fPaging;
	int32					fPageBatchSize = 50;
	bool					fAlbumSaved = false;
	bool					fAlbumSavedKnown = false;
	bool					fAlbumSavePending = false;
	bool					fPlaylistDeletePending = false;
	struct PendingTrackRemoval {
		BRow*	row = nullptr;
		int32	listIndex = -1;
		bool	selected = false;
	};
	std::vector<PendingTrackRemoval> fPendingTrackRemovals;
	PlaylistRemovalController fRemoval;
	struct PendingTrackReorder {
		std::vector<BRow*>	rows;
		std::vector<bool>	selected;
	};
	PendingTrackReorder		fPendingTrackReorder;
	PlaylistReorderController fReorder;
	bool fCloseAfterReorder = false;
	struct PendingPlaylistClear {
		std::vector<BRow*>	rows;
		std::vector<bool>	selected;
	};
	PendingPlaylistClear	fPendingPlaylistClear;
	PlaylistCoverController fCover;
	PlaylistClearController fClear;
	PlaylistAddController fAdd;
	BRow* fPendingPlaylistAdd = nullptr;
	std::vector<PlaylistEpisode> fEpisodes;
	std::vector<PlaylistEpisode> fPendingPodcastHeadEpisodes;
};

#endif
