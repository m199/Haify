#ifndef DISCOVERWINDOW_H
#define DISCOVERWINDOW_H

#include <Window.h>
#include <OS.h>
#include <map>
#include <set>
#include <string>
#include <vector>

class BMenuBar;
class BTab;
class BTabView;
class BColumnListView;
class BHandler;
class BMenuItem;
class BMessageRunner;
class DiscoverRow;
struct HaifySettings;

static const int32 kDiscoverTabCount = 9;

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
	void					_InitMenu();
	void					_InitLayout();
	BColumnListView*		_MakeList(int32 logicalTab);
	void					_LoadTab(int32 logicalTab,
								bool nextPage = false);
	void					_CheckLazyLoad();
	void					_InvalidateTabCache(int32 logicalTab);
	void					_LoadPersistentCache(int32 logicalTab);
	void					_ScheduleCacheSave();
	void					_WriteCacheNow();
	void					_ReloadTab(int32 logicalTab);
	void					_ApplyPlaylistChange(BMessage* message);
	void					_ApplyPlaylistSnapshot(BMessage* message);
	void					_ApplyLibraryChange(BMessage* message);
	void					_ResolveLibraryAddition(int32 logicalTab,
								const std::string& uri, int32 generation);
	void					_ApplyResolvedLibraryAddition(BMessage* message);
	void					_ApplyAudiobookIdSnapshot(BMessage* message);
	void					_RemoveAudiobookDuplicatesFromPodcasts();
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
	void					_SelectLibraryTarget(const std::string& uri);
	int32					_LogicalTab(int32 visualIdx) const;
	void					_ShowPlaylistContextMenu(const std::string& playlistId,
								bool owned, BPoint screen);

	BMenuBar*				fMenuBar;
	BTabView*				fTabView;
	BTab*					fTabs[kDiscoverTabCount];
	BColumnListView*		fLists[kDiscoverTabCount];
	BMenuItem*				fTabMenuItems[kDiscoverTabCount];
	bool					fTabVisible[kDiscoverTabCount];
	bool					fLoaded[kDiscoverTabCount];
	bigtime_t				fLoadTime[kDiscoverTabCount];
	bool					fPageLoading[kDiscoverTabCount];
	bool					fPageHasMore[kDiscoverTabCount];
	bool					fCacheBacked[kDiscoverTabCount];
	bool					fFreshSnapshot[kDiscoverTabCount];
	int32					fPageOffset[kDiscoverTabCount];
	int32					fTabLoadGeneration[kDiscoverTabCount];
	std::string				fPageCursor[kDiscoverTabCount];
	BMessageRunner*			fLazyLoadRunner = nullptr;
	BMessageRunner*			fCacheSaveRunner = nullptr;
	int32					fCacheLoadGeneration[kDiscoverTabCount];
	bool					fCacheLoadPending[kDiscoverTabCount];
	std::string				fCacheAccountId;
	std::set<std::string>	fAudiobookIds;
	bool					fAudiobookIdsKnown = false;
	int32					fTabMap[kDiscoverTabCount];
	std::vector<int32>		fTabOrder;
	std::string				fCurrentTrackUri;
	int32					fPlaylistSyncGeneration;
	std::map<std::string, int32>
							fLibraryChangeGenerations;
	std::map<std::string, bool>	fKnownLibraryStates;
	std::map<std::string, int32>
							fLibraryStateGenerations;

	struct PendingPlaylistRemoval {
		DiscoverRow*	row;
		int32			index;
		bool			selected;
	};
	std::map<std::string, PendingPlaylistRemoval>
							fPendingPlaylistRemovals;

	static const bigtime_t	kCacheExpiry = 5LL * 60 * 1000000;
};

#endif
