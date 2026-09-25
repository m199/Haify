#pragma once

#include "ui/MediaHeaderStyle.h"

#include <Layout.h>
#include <TextView.h>
#include <Window.h>
#include <algorithm>

// Native UI adapters only: content, selection and feature state stay with their
// existing owners. Sizes are always derived from the current system font.
namespace MediaWindowScale {

inline void
ApplyPlainFont(BView* view)
{
	if (!view)
		return;
	view->SetFont(be_plain_font);
	view->InvalidateLayout();
	view->Invalidate();
}

inline void
ApplyTitleFont(BView* view, float scale = MediaHeaderStyle::kTitleScale)
{
	if (!view)
		return;
	BFont font(be_bold_font);
	font.SetSize(be_plain_font->Size() * scale);
	if (auto* text = dynamic_cast<BTextView*>(view))
		text->SetFontAndColor(0, text->TextLength(), &font);
	else
		view->SetFont(&font);
	view->InvalidateLayout();
	view->Invalidate();
}

inline void
ApplyTextSize(BTextView* view)
{
	if (!view)
		return;
	// Explicit range includes unselected text; the size-only mask preserves
	// bold/italic faces and link colors in stylable descriptions.
	view->SetFontAndColor(0, view->TextLength(), be_plain_font, B_FONT_SIZE);
}

inline void
ApplyHeaderInfoSize(BView* view)
{
	if (!view)
		return;
	float height = MediaHeaderStyle::ArtworkSize();
	if (view->GetLayout())
		height = std::max(height, view->GetLayout()->MinSize().height);
	view->SetExplicitMinSize(BSize(0, height));
	view->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, height));
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, height));
}

inline void
ApplyWindowMinimum(BWindow* window, float baseWidth, float baseHeight)
{
	BSize minimum(baseWidth, baseHeight);
	if (window->GetLayout()) {
		BSize layoutSize = window->GetLayout()->MinSize();
		minimum.width = std::max(minimum.width, layoutSize.width);
		minimum.height = std::max(minimum.height, layoutSize.height);
	}
	window->SetSizeLimits(minimum.width, 100000, minimum.height, 100000);
	window->ResizeTo(std::max(window->Bounds().Width(), minimum.width),
		std::max(window->Bounds().Height(), minimum.height));
}

} // namespace MediaWindowScale
