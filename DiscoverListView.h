#ifndef DISCOVER_LIST_VIEW_H
#define DISCOVER_LIST_VIEW_H

#include "DropMarkerController.h"

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <string>
#include <vector>

class BMessageRunner;

enum ColAction {
	kColNone = 0,
	kColPlayOnDouble,
	kColOpenOnDouble,
	kColRouteOnDouble,
};

struct ColDef {
	const char*	label;
	float		width;
	ColAction	action;
};

class BoldStringField : public BStringField {
public:
						BoldStringField(const char* string, bool enabled = true)
							: BStringField(string), fIsPlaying(false),
							  fEnabled(enabled) {}
	bool				fIsPlaying;
	bool				fEnabled;
	int32				fDropMarkerPosition = 0;
	bool				fDropTargetHighlight = false;
	bool				fDropTargetFillToRight = false;
};

class BoldStringColumn : public BStringColumn {
public:
						BoldStringColumn(const char* title, float width,
						                 float minW, float maxW, uint32 truncate)
							: BStringColumn(title, width, minW, maxW, truncate) {}
	virtual void		DrawField(BField* field, BRect rect, BView* parent);
};

class DiscoverRow : public BRow {
public:
						DiscoverRow(const std::vector<std::string>& vals,
						            const std::vector<std::string>& uris,
						            const std::vector<std::string>& titles,
						            bool writable = true, bool owned = false);
	bool				SetDropFeedback(int32 markerPosition,
							bool targetHighlight);

	std::vector<std::string>	fUris;
	std::vector<std::string>	fTitles;
	bool				fWritable;
	bool				fOwned;
	bool				fIsPlaying = false;
	int32				fDropMarkerPosition = 0;
	bool				fDropTargetHighlight = false;
	int32				fFieldCount = 0;
};

class DiscoverListView : public BColumnListView {
public:
						DiscoverListView(const char* name,
						                 const std::vector<ColDef>& cols,
						                 int32 logicalTab = -1,
						                 bool showHorizontalScrollbar = false);
	virtual				~DiscoverListView();
	virtual void		SelectionChanged();
	virtual void		ItemInvoked();
	virtual bool		InitiateDrag(BPoint point, bool wasSelected);
	virtual void		MouseDown(BPoint where);
	virtual void		MouseMoved(BPoint point, uint32 transit,
							const BMessage* dragMessage);
	virtual void		Draw(BRect update) override;
	virtual void		MessageReceived(BMessage* message);
	virtual void		AttachedToWindow();
	void				SetPlayingUri(const std::string& uri);
	void				ForwardDroppedMessage(BMessage* message);
	void				UpdateDropMarker(BPoint screenWhere);
	void				UpdateDropTarget(BPoint screenWhere);
	void				ClearDropMarker();
	void				UpdateDropMarkerFromDrag(BPoint screenWhere,
							const BMessage* dragMessage);
	void				SetDropFeedbackFlags(DropFeedbackFlags flags);

private:
	void				_ShowContextMenuAt(BPoint screenWhere);
	void				_DispatchClick(bool isDouble);
	bool				_FindClickTarget(DiscoverRow*& row, int32& column,
							std::string& title);
	void				_PostPlayClick(DiscoverRow* row,
							const std::string& uri,
							const std::string& title);
	void				_PostOpenClick(const std::string& uri,
							const std::string& title);
	void				_PostRouteClick(DiscoverRow* row,
							const std::string& uri,
							const std::string& title);
	void				_StartDropMarkerCleanupRunner();
	void				_StopDropMarkerCleanupRunner();
	void				_ClearDropMarkerIfDragEnded();
	int32				_ColumnAt(float x) const;
	std::vector<ColAction>	fActions;
	int32				fLogicalTab;
	DropMarkerController fDropMarker;
	bool				fFiltersInstalled = false;
	BMessageRunner*		fDropMarkerCleanupRunner = nullptr;
};

#endif
