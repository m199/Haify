#ifndef DISCOVERWINDOW_H
#define DISCOVERWINDOW_H

#include "discover/DiscoverTabPolicy.h"
#include "discover/DiscoverAsyncScope.h"
#include "discover/DiscoverCacheController.h"
#include "discover/DiscoverLibraryChangeController.h"
#include "discover/DiscoverLibraryWriteController.h"
#include "discover/DiscoverPlaylistMutationController.h"

#include <Window.h>
#include <OS.h>
#include <Point.h>
#include <map>
#include <set>
#include <string>
#include <vector>

class BMenuBar;
class BPopUpMenu;
class BMessenger;
class BTab;
class BTabView;
class BColumnListView;
class BHandler;
class BMenuItem;
class BMessageRunner;
class DiscoverRow;
struct HaifySettings;
class SpotifyApi;

class DiscoverWindow : public BWindow {
public:
								DiscoverWindow();
	virtual					~DiscoverWindow();
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage* message);
	void					LoadData();
	void					ReloadPlaylists();
	bool					ForwardDroppedMessage(BMessage* message,
								BHandler* target);

private:
	void                    _EnsureAsyncContext();
	bool                    _AcceptAsyncResult(const BMessage& message);
	bool                    _AcceptDialogContext(const BMessage& message) const;
	struct RowUpdateData {
		int32					tab = -1;
		int32					cols = 0;
		bool					fromCache = false;
		bool					cacheLast = true;
		bool					snapshotMessage = false;
		int32					nRows = 0;
		std::vector<std::string>	allV;
		std::vector<std::string>	allU;
		std::vector<std::string>	allT;
		std::set<std::string>	snapshotUris;
		std::vector<std::string>	snapshotOrder;
	};

	void					_InitMenu();
	void					_InitLayout();
	BColumnListView*		_MakeList(int32 logicalTab);
	void					_LoadTab(int32 logicalTab,
								bool nextPage = false);
	void					_CheckLazyLoad();
	void					_ToggleTabVisibility(BMessage* message);
	void					_ResetTabOrder();
	void					_ApplySpotifyCapabilities();
	void					_SelectTab(BMessage* message);
	void					_ApplyPageDone(BMessage* message);
	bool					_HandleTabMessage(BMessage* message);
	bool					_HandleDataMessage(BMessage* message);
	bool					_HandlePlaybackOpenMessage(BMessage* message);
	bool					_HandleLibraryActionMessage(BMessage* message);
	bool					_HandleDiscoverDropActionMessage(
								BMessage* message);
	bool					_HandleLibraryMutationMessage(
								BMessage* message);
	bool					_HandlePlaylistActionMessage(BMessage* message);
	bool					_HandleAppForwardMessage(BMessage* message);
	void					_SaveCacheNowFromMessage();
	void					_ApplyDiscoverRows(BMessage* message);
	bool					_ReadRowUpdate(BMessage* message,
								RowUpdateData& update);
	bool					_ApplyCacheRowUpdateStart(BMessage* message,
								RowUpdateData& update);
	bool					_ApplyFreshRowUpdateStart(BMessage* message,
								const RowUpdateData& update);
	void					_ApplyRowUpdateRows(BMessage* message,
								RowUpdateData& update);
	void					_ApplyRowUpdateRow(BMessage* message,
								RowUpdateData& update, int32 rowIndex);
	bool					_RowUpdateColumnsAvailable(
								const RowUpdateData& update,
								int32 rowIndex) const;
	bool					_AcceptRowUpdatePrimaryUri(
								RowUpdateData& update,
								const std::string& uri);
	bool					_ApplyExistingRowUpdateIfPresent(
								const RowUpdateData& update,
								int32 rowIndex, bool writable, bool owned);
	bool					_ShouldSkipPlaceholderRowUpdate(
								const RowUpdateData& update,
								const std::string& uri) const;
	void					_AddRowUpdateRow(const RowUpdateData& update,
								int32 rowIndex, bool writable, bool owned);
	bool					_RowUpdateHasRealRow(int32 tab) const;
	void					_ApplyExistingRowUpdate(DiscoverRow* row,
								const RowUpdateData& update, int32 rowIndex,
								bool writable, bool owned);
	void					_PruneSnapshotRows(
								const RowUpdateData& update);
	void					_ReorderSnapshotRows(
								const RowUpdateData& update);
	void					_FinishRowUpdate(BMessage* message,
								const RowUpdateData& update);
	void					_ForwardPlayback(BMessage* message);
	void					_ForwardOpenRequest(BMessage* message);
	void					_ApplyPlayingTrackUpdate(BMessage* message);
	void					_ShowDiscoverContextMenu(BMessage* message);
	void					_RequestPlayableLibraryState(
								const std::string& uri);
	void					_ShowBrowsableItemContextMenu(
								const std::string& uri,
								const std::string& title, int32 sourceTab,
								BPoint screen);
	void					_AddAlbumContextActions(BPopUpMenu* menu,
								const std::string& uri, int32 sourceTab);
	void					_AddLibraryRemovalContextAction(BPopUpMenu* menu,
								const std::string& uri, int32 sourceTab);
	void					_ShowPlayableContextMenu(BMessage* message);
	void					_ApplyLibraryStateCached(BMessage* message);
	void					_HandleDiscoverDragHover(BMessage* message);
	void					_HandleDiscoverDrop(BMessage* message);
	void					_ApplyPlaylistDropResult(BMessage* message);
	void                    _ApplyLibraryCommand(BMessage* message);
	void                    _QueueLibraryWrite(const DiscoverLibraryWriteRequest& command);
	void                    _DispatchLibraryWrite(const DiscoverLibraryWriteRequest& request);
	void                    _ApplyLibraryWriteResult(BMessage* message);
	void					_PlayTrackFromMessage(BMessage* message);
	void					_ShowNewPlaylistDialog();
	void					_CreatePlaylist(BMessage* message);
	void					_ApplyPlaylistCreateResult(BMessage* message);
	void					_ShowRenamePlaylistDialog(BMessage* message);
	void					_RenamePlaylist(BMessage* message);
	void                    _QueuePlaylistMutation(DiscoverChangeOperation operation,
	                            const std::string& id, const std::string& name = "");
	void                    _DispatchPlaylistMutation(const DiscoverPlaylistMutationRequest& request);
	void                    _ApplyPlaylistMutationResult(BMessage* message);
	void                    _RenderPlaylistMutation(const std::string& id,
	                            const DiscoverPlaylistMutationPlan& plan);
	void                    _DetachPlaylistRow(const std::string& id);
	DiscoverRow*            _RestorePlaylistRow(const std::string& id);
	void                    _DiscardDetachedPlaylistRow(const std::string& id);
	void                    _ReorderPlaylistRows(const std::vector<std::string>& order);
	void					_DeletePlaylist(BMessage* message);
	void					_ApplyPlaylistsChanged(BMessage* message);
	void					_ApplyLibraryChanged(BMessage* message);
	void					_ReloadTabFromMessage(BMessage* message);
	void					_InvalidateTabCache(int32 logicalTab);
	void					_LoadPersistentCache(int32 logicalTab);
	void					_ScheduleCacheSave();
	void					_WriteCacheNow();
	void					_ReloadTab(int32 logicalTab);
	void					_ApplyPlaylistChange(BMessage* message);
	void					_RemovePlaylistRow(DiscoverRow* row);
	void					_AddOrUpdatePlaylistRow(DiscoverRow* row,
								const std::string& uri,
								const std::string& name,
								const std::string& owner, bool writable,
								bool owned);
	void					_ApplyPlaylistSnapshot(BMessage* message);
	bool					_CanApplyPlaylistSnapshot(
								BMessage* message) const;
	void					_ApplyLibraryChange(BMessage* message);
	void					_ApplyLibraryRemoval(int32 tab,
								const std::string& uri);
	void					_ResolveLibraryAddition(int32 logicalTab,
								const std::string& uri, int32 generation);
	void					_ApplyResolvedLibraryAddition(BMessage* message);
	void					_RemoveEmptyRows(int32 logicalTab);
	void					_ApplyAudiobookIdSnapshot(BMessage* message);
	void					_RemoveAudiobookDuplicatesFromPodcasts();
	bool					_CanLoadTab(int32 logicalTab,
								bool nextPage,
								SpotifyApi*& api) const;
	void					_PrepareLoadTab(int32 logicalTab,
								bool nextPage);
	void					_LoadPlaylistsTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	void					_LoadTopTracksTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	void					_LoadTopArtistsTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	void					_LoadNewReleasesTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	void					_LoadSavedAlbumsTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	void					_LoadPodcastsTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	void					_LoadFollowedArtistsTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	void					_LoadSavedEpisodesTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	void					_LoadAudiobooksTab(SpotifyApi* api,
								const BMessenger& messenger,
								bool snapshot, int32 loadGeneration);
	DiscoverRow*			_FindRow(int32 logicalTab,
								const std::string& uri) const;
	DiscoverRow*			_FindPlaylistRow(const std::string& uri) const;
	void					_UpdatePlaylistRow(DiscoverRow* row,
								const std::string& name,
								const std::string& owner, bool writable,
								bool owned);
	void					_RebuildTabs();
	void					_LoadTabVisibility(const HaifySettings& settings);
	void					_SaveTabVisibility(HaifySettings& settings) const;
	void					_LoadTabOrder(const HaifySettings& settings);
	void					_SaveTabOrder(HaifySettings& settings) const;
	bool					_IsTabEffectivelyVisible(int32 logicalTab) const;
	bool					_AudiobooksEnabled() const;
	void					_MoveTab(int32 sourceVisual, int32 targetVisual);
	bool					_HandlePlaylistDrop(const std::string& itemUri,
								const std::string& targetUri, bool writable);
	void					_HandleLibraryDrop(const std::string& uri);
	int32					_VisualTabForLogical(int32 logicalTab) const;
	bool					_IsPointerOverDropTargetTab(
								int32 logicalTab) const;
	void					_SetValidDropTargetTab(int32 logicalTab);
	void					_ScheduleDropTabSwitch(int32 logicalTab);
	void					_CancelDropTabSwitch();
	bool					_SelectDropTargetTab(int32 logicalTab);
	void					_UpdateDropMarkers(int32 logicalTab,
								BPoint screenWhere);
	void					_ClearDropMarkers();
	void					_SelectLibraryTarget(const std::string& uri);
	int32					_LogicalTab(int32 visualIdx) const;
	void					_ShowPlaylistContextMenu(const std::string& playlistId,
								bool owned, BPoint screen);

	BMenuBar*				fMenuBar;
	BTabView*				fTabView;
	BTab*					fTabs[kDiscoverTabCount];
	BColumnListView*		fLists[kDiscoverTabCount];
	BMenuItem*				fTabMenuItems[kDiscoverTabCount];
	DiscoverTabVisibility	fTabVisible{};
	BMessageRunner*			fLazyLoadRunner = nullptr;
	BMessageRunner*			fCacheSaveRunner = nullptr;
	BMessageRunner*			fDropTabSwitchRunner = nullptr;
	int32					fPendingDropTab = -1;
	DiscoverCacheController fCache;
	DiscoverAsyncScope fAsync;
	DiscoverLibraryChangeController fLibrary;
	DiscoverLibraryWriteController fLibraryWrites;
	DiscoverTabLayout		fTabLayout;
	DiscoverTabOrder			fTabOrder;
	std::string				fCurrentTrackUri;
	DiscoverPlaylistMutationController fPlaylistMutations;

	struct PendingPlaylistRemoval {
		DiscoverRow*	row = nullptr;
		int32			index = -1;
		bool			selected = false;
	};
	std::map<std::string, PendingPlaylistRemoval>
							fPendingPlaylistRemovals;

};

#endif
