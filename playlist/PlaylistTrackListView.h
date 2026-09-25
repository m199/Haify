#pragma once

#include "ui/drag/DropMarkerController.h"
#include "messages/DragItem.h"
#include "ui/views/FontScaledListView.h"

#include <InterfaceDefs.h>
#include <Point.h>
#include <SupportDefs.h>
#include <cstdint>
#include <vector>

class BMessage;
class BMessageRunner;
class BScrollBar;

class TrackListView : public FontScaledListView {
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
	std::vector<int32_t> SelectedRowIndices() const;

private:
	class MouseDownFilter;
	class MouseUpFilter;
	MessageContracts::DragItem _DragItemForRow(BRow* row) const;
	void _RememberDragSelection(BPoint point);
	bool _DragSelectionIsCurrent(const MessageContracts::DragItem& saved,
		BRow* row) const;
	bool _RestoreDragSelection(BRow* row);
	void _FinishDeferredClick();

	void _UpdateDropMarker(BPoint point, const BMessage* dragMessage);
	void _StartDropMarkerCleanupRunner();
	void _StopDropMarkerCleanupRunner();
	void _ClearDropMarkerIfDragEnded();
	int32 _ColumnAt(float x) const;
	DropMarkerController fDropMarker;
	BMessageRunner* fDropMarkerCleanupRunner = nullptr;
	MessageContracts::DragItem fMouseDownDrag;
	bool fPreserveGroupOnMouseDown = false;
	bool fDeferredGroupClick = false;
};

BScrollBar* TrackVerticalScrollBar(TrackListView* list);
