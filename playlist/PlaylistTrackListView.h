#pragma once

#include <ColumnListView.h>
#include <InterfaceDefs.h>
#include <Point.h>
#include <SupportDefs.h>

class BMessage;
class BScrollBar;

class TrackListView : public BColumnListView {
public:
	TrackListView(const char* name, uint32 flags, border_style border,
		bool showHorizontalScrollbar);

	virtual void AttachedToWindow();
	virtual void MouseDown(BPoint point);
	virtual void MessageReceived(BMessage* message);
	virtual void KeyDown(const char* bytes, int32 numBytes);
	virtual bool InitiateDrag(BPoint point, bool wasSelected);
	virtual void MouseMoved(BPoint point, uint32 transit,
		const BMessage* dragMessage);
	virtual void SelectionChanged();
	virtual void ItemInvoked();

private:
	class RightClickFilter;

	int32 _ColumnAt(float x) const;
};

BScrollBar* TrackVerticalScrollBar(TrackListView* list);
