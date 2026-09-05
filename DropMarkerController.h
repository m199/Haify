#pragma once

#include "DropMarkerStyle.h"

#include <ColumnListView.h>
#include <Point.h>
#include <Rect.h>
#include <SupportDefs.h>
#include <View.h>

#include <algorithm>
#include <functional>

typedef uint32 DropFeedbackFlags;

static const DropFeedbackFlags kDropFeedbackNone = 0;
static const DropFeedbackFlags kDropFeedbackInsertMarker = 1 << 0;
static const DropFeedbackFlags kDropFeedbackAppendMarker = 1 << 1;
static const DropFeedbackFlags kDropFeedbackTargetRow = 1 << 2;

class DropMarkerController {
public:
	typedef std::function<bool(BRow*, int32, bool)> RowFeedbackSetter;

	DropMarkerController(BColumnListView* list, RowFeedbackSetter setRowFeedback)
		:
		fList(list),
		fSetRowFeedback(setRowFeedback)
	{
	}

	bool SetFlags(DropFeedbackFlags flags)
	{
		if (fFlags == flags)
			return false;
		bool cleared = Clear();
		fFlags = flags;
		return cleared;
	}

	DropFeedbackFlags Flags() const
	{
		return fFlags;
	}

	bool IsActive() const
	{
		return fActive;
	}

	bool UpdateInsertForPoint(BPoint point)
	{
		if ((fFlags & kDropFeedbackInsertMarker) == 0)
			return Clear();
		return _UpdateForInsertBefore(_InsertBeforeForPoint(point));
	}

	bool UpdateAppend()
	{
		if ((fFlags & kDropFeedbackAppendMarker) == 0)
			return Clear();
		return _UpdateForInsertBefore(fList ? fList->CountRows() : -1);
	}

	bool UpdateTargetForPoint(BPoint point)
	{
		if ((fFlags & kDropFeedbackTargetRow) == 0)
			return Clear();
		BRow* targetRow = fList ? fList->RowAt(point) : nullptr;
		bool wasActive = fActive;
		fActive = targetRow != nullptr;
		fMarkerY = -1.0f;
		bool rowChanged = _SetRowFeedback(targetRow, 0, targetRow != nullptr);
		if (wasActive != fActive || rowChanged) {
			if (fList)
				fList->Invalidate();
			return true;
		}
		return false;
	}

	bool Clear()
	{
		bool rowChanged = _SetRowFeedback(nullptr, 0, false);
		if (!fActive && !rowChanged)
			return false;
		fActive = false;
		fMarkerY = -1.0f;
		if (fList)
			fList->Invalidate();
		return true;
	}

	void Draw(BView* view) const
	{
		if (!view || fMarkerY < 0.0f)
			return;
		rgb_color originalColor = view->HighColor();
		float originalPenSize = view->PenSize();
		view->SetHighColor(HaifyDropMarkerColor());
		view->SetPenSize(2.0f);
		BRect bounds = view->Bounds();
		view->StrokeLine(BPoint(bounds.left + 1.0f, fMarkerY),
			BPoint(bounds.right - 1.0f, fMarkerY));
		view->SetPenSize(originalPenSize);
		view->SetHighColor(originalColor);
	}

private:
	bool _UpdateForInsertBefore(int32 insertBefore)
	{
		bool wasActive = fActive;
		fActive = insertBefore >= 0 && fList && fList->CountRows() > 0;
		bool rowChanged = fActive
			? _SetForInsertBefore(insertBefore)
			: _SetRowFeedback(nullptr, 0, false);
		if (wasActive != fActive || rowChanged) {
			if (fList)
				fList->Invalidate();
			return true;
		}
		return false;
	}

	bool _SetForInsertBefore(int32 insertBefore)
	{
		int32 rowCount = fList ? fList->CountRows() : 0;
		if (rowCount <= 0) {
			fMarkerY = -1.0f;
			return _SetRowFeedback(nullptr, 0, false);
		}

		insertBefore = std::max<int32>(0,
			std::min<int32>(insertBefore, rowCount));
		int32 targetIndex = insertBefore == 0 ? 0 : insertBefore - 1;
		int32 markerPosition = insertBefore == 0 ? -1 : 1;
		BRow* targetRow = fList->RowAt(targetIndex);
		BRect rowRect;
		float previousY = fMarkerY;
		fMarkerY = targetRow && fList->GetRowRect(targetRow, &rowRect)
			? (markerPosition < 0 ? rowRect.top + 1.0f
				: rowRect.bottom - 1.0f)
			: -1.0f;
		return _SetRowFeedback(targetRow, markerPosition, false)
			|| previousY != fMarkerY;
	}

	int32 _InsertBeforeForPoint(BPoint point) const
	{
		int32 rowCount = fList ? fList->CountRows() : 0;
		if (rowCount <= 0)
			return -1;

		for (int32 i = 0; i < rowCount; i++) {
			const BRow* row = fList->RowAt(i);
			BRect rowRect;
			if (!row || !fList->GetRowRect(row, &rowRect))
				continue;
			if (point.y < rowRect.top)
				return i;
			if (point.y <= rowRect.bottom) {
				float midpoint = rowRect.top + rowRect.Height() / 2.0f;
				return point.y < midpoint ? i : i + 1;
			}
		}
		return rowCount;
	}

	bool _SetRowFeedback(BRow* targetRow, int32 markerPosition,
		bool targetHighlight)
	{
		if (!fList || !fSetRowFeedback)
			return false;
		bool changed = false;
		for (int32 i = 0; i < fList->CountRows(); i++) {
			BRow* row = fList->RowAt(i);
			if (!row)
				continue;
			int32 position = row == targetRow ? markerPosition : 0;
			bool highlight = row == targetRow && targetHighlight;
			if (fSetRowFeedback(row, position, highlight)) {
				fList->InvalidateRow(row);
				changed = true;
			}
		}
		return changed;
	}

	BColumnListView* fList;
	RowFeedbackSetter fSetRowFeedback;
	DropFeedbackFlags fFlags = kDropFeedbackNone;
	bool fActive = false;
	float fMarkerY = -1.0f;
};
