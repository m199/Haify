#include "ui/views/FontScaledListView.h"
#include "ui/UiScalePolicy.h"

#include <ColumnTypes.h>
#include <Message.h>
#include <ScrollBar.h>
#include <Window.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <new>
#include <set>
#include <utility>

namespace {

// Haiku BRow uses a default BList, retaining at least 20 slots on assignment
// from an empty list. Bound the adapter so reattaching fields cannot allocate.
// Verified against Haiku 8b91c532 (2026-09-20); see phase-7-verification.md.
constexpr int32 kRetainedFieldSlots = 20;

struct RowHeightChange {
	BRow* row = nullptr;
	int32 fieldCount = 0;
	std::array<BField*, kRetainedFieldSlots> fields{};
};

struct RowScalePlan {
	std::vector<BRow*> attached;
	std::vector<BRow*> selection;
	std::vector<RowHeightChange> changes;
	std::set<BRow*> seen;
	BRow* focus = nullptr;
	int32 anchor = -1;
	float anchorFraction = 0;
	BPoint scroll;
};

status_t
PrepareRow(BRow* row, RowScalePlan& plan)
{
	if (!row || !plan.seen.insert(row).second)
		return B_BAD_VALUE;
	if (row->HasLatch() || row->CountFields() > kRetainedFieldSlots)
		return B_NOT_SUPPORTED;
	RowHeightChange change{row, row->CountFields(), {}};
	for (int32 index = 0; index < change.fieldCount; ++index)
		change.fields[index] = row->GetField(index);
	plan.changes.push_back(change);
	return B_OK;
}

status_t
PrepareRows(FontScaledListView& list, const std::vector<BRow*>& detached,
	RowScalePlan& plan)
{
	plan.focus = list.FocusRow();
	if (list.ScrollView())
		plan.scroll = list.ScrollView()->Bounds().LeftTop();
	for (int32 index = 0; index < list.CountRows(); ++index) {
		BRow* row = list.RowAt(index);
		status_t status = PrepareRow(row, plan);
		if (status != B_OK)
			return status;
		plan.attached.push_back(row);
		BRect rect;
		if (plan.anchor < 0 && list.GetRowRect(row, &rect)
				&& rect.bottom >= plan.scroll.y) {
			plan.anchor = index;
			plan.anchorFraction = (plan.scroll.y - rect.top) / (row->Height() + 1);
		}
	}
	for (BRow* row : detached) {
		status_t status = PrepareRow(row, plan);
		if (status != B_OK)
			return status;
	}
	for (BRow* row = list.CurrentSelection(); row; row = list.CurrentSelection(row))
		plan.selection.push_back(row);
	return B_OK;
}

void
ApplyRowHeight(const RowHeightChange& change, const BRow& emptyRow)
{
	// Use the public copy assignment ONLY from a fresh, empty, detached BRow.
	// BList assignment releases pointer storage, not its pointed-to fields.
	// The derived row and fields stay alive at their original addresses.
	// Never copy a populated BRow: that duplicates ownership/native links.
	*change.row = emptyRow;
	// If BList cannot shrink its storage, assignment leaves its original fields
	// intact. Do not SetField over them: that would delete the retained objects.
	const bool fieldsRetained = change.row->CountFields() != 0;
	for (int32 index = 0; index < change.fieldCount; ++index) {
		if (!fieldsRetained)
			change.row->SetField(change.fields[index], index);
		if (auto field = dynamic_cast<BStringField*>(change.fields[index])) {
			field->SetWidth(0);
			field->SetClippedString("");
		}
	}
}

void
RestoreRows(FontScaledListView& list, const RowScalePlan& plan)
{
	for (BRow* row : plan.attached)
		list.AddRow(row);
	// AddRow honors sort keys. Restore equal-key order without clearing the keys
	// (SetSortingEnabled clears them even when merely toggled).
	for (size_t index = 0; index < plan.attached.size(); ++index) {
		int32 current = list.IndexOf(plan.attached[index]);
		if (current != static_cast<int32>(index))
			list.SwapRows(current, static_cast<int32>(index));
	}
	for (auto row = plan.selection.rbegin(); row != plan.selection.rend(); ++row)
		list.AddToSelection(*row);
	if (plan.focus)
		list.SetFocusRow(plan.focus);
}

void
RestoreScroll(FontScaledListView& list, const RowScalePlan& plan)
{
	BPoint scroll = plan.scroll;
	if (plan.anchor >= 0) {
		BRow* row = list.RowAt(plan.anchor);
		BRect rect;
		if (list.GetRowRect(row, &rect))
			scroll.y = rect.top + plan.anchorFraction * (row->Height() + 1);
	}
	for (orientation direction : {B_HORIZONTAL, B_VERTICAL}) {
		if (BScrollBar* bar = list.ContentScrollBar(direction))
			bar->SetValue(direction == B_HORIZONTAL ? scroll.x : scroll.y);
	}
}

} // namespace

FontScaledListView::FontScaledListView(const char* name, uint32 flags,
	border_style border, bool horizontalScrollbar)
	: BColumnListView(name, flags, border, horizontalScrollbar)
{
}

void
FontScaledListView::AttachedToWindow()
{
	BColumnListView::AttachedToWindow();
	// Inactive tabs may have been detached when B_FONTS_UPDATED was delivered.
	_RefreshSystemFont();
}

void
FontScaledListView::SetDetachedRowsProvider(std::function<std::vector<BRow*>()> provider)
{
	fDetachedRows = std::move(provider);
}

BScrollBar*
FontScaledListView::ContentScrollBar(orientation direction) const
{
	// Both scrollbars are direct children of BColumnListView. ScrollView() is
	// only the row view; the horizontal bar targets a separate header view.
	for (BView* child = ChildAt(0); child; child = child->NextSibling()) {
		auto* bar = dynamic_cast<BScrollBar*>(child);
		if (bar && bar->Orientation() == direction)
			return bar;
	}
	return nullptr;
}

status_t
FontScaledListView::RefreshRowFont(const BFont& font)
{
	if (fResizingRows || !std::isfinite(font.Size()) || font.Size() <= 0)
		return B_BAD_VALUE;
	RowScalePlan plan;
	try {
		auto detached = fDetachedRows ? fDetachedRows() : std::vector<BRow*>();
		status_t status = PrepareRows(*this, detached, plan);
		if (status != B_OK)
			return status;
	} catch (const std::bad_alloc&) {
		return B_NO_MEMORY;
	}
	const BRow emptyRow(UiScale::ListRowHeight(font.Size()));
	fResizingRows = true;
	if (Window())
		Window()->BeginViewTransaction();
	for (auto row = plan.attached.rbegin(); row != plan.attached.rend(); ++row)
		RemoveRow(*row);
	for (const auto& change : plan.changes)
		ApplyRowHeight(change, emptyRow);
	RestoreRows(*this, plan);
	SetFont(B_FONT_ROW, &font);
	SetFont(B_FONT_HEADER, &font);
	InvalidateLayout();
	if (Window())
		DoLayout();
	RestoreScroll(*this, plan);
	Invalidate();
	if (Window())
		Window()->EndViewTransaction();
	fResizingRows = false;
	return B_OK;
}

void
FontScaledListView::_RefreshSystemFont()
{
	status_t status = RefreshRowFont(*be_plain_font);
	if (status != B_OK)
		std::fprintf(stderr, "Haify: list font refresh failed (%ld)\n", long(status));
}

void
FontScaledListView::MessageReceived(BMessage* message)
{
	if (message->what == B_FONTS_UPDATED) {
		_RefreshSystemFont();
		return;
	}
	BColumnListView::MessageReceived(message);
}

void
FontScaledListView::SelectionChanged()
{
	if (!fResizingRows)
		BColumnListView::SelectionChanged();
}
