#pragma once

#include <ColumnListView.h>
#include <functional>
#include <vector>

// Owns presentation changes only. Calls and the detached-row provider run under
// the window lock. Rows, fields and owner references keep their addresses.
class FontScaledListView : public BColumnListView {
public:
	FontScaledListView(const char* name, uint32 flags, border_style border,
		bool horizontalScrollbar);
	void AttachedToWindow() override;
	void MessageReceived(BMessage* message) override;
	void SelectionChanged() override;
	status_t RefreshRowFont(const BFont& font);
	void SetDetachedRowsProvider(std::function<std::vector<BRow*>()> provider);
	// Borrowed native scrollbar; horizontal scrolling targets the column header.
	BScrollBar* ContentScrollBar(orientation direction) const;

protected:
	bool IsResizingRows() const { return fResizingRows; }

private:
	void _RefreshSystemFont();
	std::function<std::vector<BRow*>()> fDetachedRows;
	bool fResizingRows = false;
};
