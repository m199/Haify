#pragma once

#include "DropMarkerController.h"

#include <ColumnListView.h>
#include <InterfaceDefs.h>
#include <Point.h>
#include <SupportDefs.h>

class BMessage;
class BMessageRunner;
class BScrollBar;

class TrackListView : public BColumnListView {
public:
	TrackListView(const char* name, uint32 flags, border_style border,
		bool showHorizontalScrollbar);
	virtual ~TrackListView();

	virtual void AttachedToWindow();
	virtual void MouseDown(BPoint point);
	virtual void MessageReceived(BMessage* message);
	virtual void KeyDown(const char* bytes, int32 numBytes);
	virtual bool InitiateDrag(BPoint point, bool wasSelected);
	virtual void MouseMoved(BPoint point, uint32 transit,
		const BMessage* dragMessage);
	virtual void Draw(BRect update) override;
	virtual void SelectionChanged();
	virtual void ItemInvoked();
	void UpdateDropMarkerFromDrag(BPoint screenWhere,
		const BMessage* dragMessage);
	void ClearDropMarker();
	void SetDropFeedbackFlags(DropFeedbackFlags flags);

private:
	class RightClickFilter;

	void _UpdateDropMarker(BPoint point, const BMessage* dragMessage);
	void _StartDropMarkerCleanupRunner();
	void _StopDropMarkerCleanupRunner();
	void _ClearDropMarkerIfDragEnded();
	int32 _ColumnAt(float x) const;
	DropMarkerController fDropMarker;
	BMessageRunner* fDropMarkerCleanupRunner = nullptr;
};

BScrollBar* TrackVerticalScrollBar(TrackListView* list);
