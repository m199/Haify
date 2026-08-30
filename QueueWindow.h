#ifndef QUEUEWINDOW_H
#define QUEUEWINDOW_H

#include <Window.h>
#include <string>

class BColumnListView;
class BMenuBar;
class BTabView;

class QueueWindow : public BWindow {
public:
							QueueWindow();
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage* message);

	void					SetPlayingTrack(const char* uri);

private:
	void					_InitLayout();
	void					_LoadQueue();
	void					_LoadRecent();
	void					_LoadRecentIfNeeded(int32 tab);
	void					_RefreshQueueAndRecent();
	void					_ApplyPlayingTrack(BMessage* message);
	void					_ApplyQueueItems(BMessage* message);
	int32					_QueueMessageCount(BMessage* message) const;
	bool					_CanUpdateQueueRows(BMessage* message,
								int32 count) const;
	void					_UpdateQueueRows(BMessage* message, int32 count);
	void					_RebuildQueueRows(BMessage* message);
	void					_ApplyCurrentPlayingTrack();
	void					_ApplyRecentRows(BMessage* message);
	void					_PlayTrackFromMessage(BMessage* message);
	void					_ForwardPlayMessage(BMessage* message);

	BMenuBar*				fMenuBar		= nullptr;
	BTabView*				fTabView		= nullptr;
	BColumnListView*		fList			= nullptr;
	BColumnListView*		fRecentList		= nullptr;
	std::string				fCurrentUri;
	bool					fRecentLoaded	= false;
};

#endif
