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

	BMenuBar*				fMenuBar		= nullptr;
	BTabView*				fTabView		= nullptr;
	BColumnListView*		fList			= nullptr;
	BColumnListView*		fRecentList		= nullptr;
	std::string				fCurrentUri;
	bool					fRecentLoaded	= false;
};

#endif
