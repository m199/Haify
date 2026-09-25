#include "ui/MediaWindowScale.h"

#include <Application.h>
#include <cassert>
#include <cstdio>
#include <cstring>

#ifdef NDEBUG
#error Media window scaling tests require assertions; compile without NDEBUG.
#endif

static void Check(bool condition) { assert(condition); }

static bool
SameColor(rgb_color first, rgb_color second)
{
	return first.red == second.red && first.green == second.green
		&& first.blue == second.blue && first.alpha == second.alpha;
}

static void
CheckDescriptionStyles()
{
	BTextView text("description");
	text.SetStylable(true);
	text.SetText("Bold link plain");
	BFont plain(be_plain_font);
	plain.SetSize(9);
	text.SetFontAndColor(0, text.TextLength(), &plain);
	BFont bold(be_bold_font);
	bold.SetSize(10);
	text.SetFontAndColor(0, 4, &bold);
	rgb_color linkColor = {20, 60, 180, 255};
	text.SetFontAndColor(5, 9, &plain, B_FONT_ALL, &linkColor);
	text.Select(5, 9);
	BFont originalBold, originalLink;
	rgb_color originalBoldColor, originalLinkColor;
	text.GetFontAndColor(0, &originalBold, &originalBoldColor);
	text.GetFontAndColor(5, &originalLink, &originalLinkColor);

	MediaWindowScale::ApplyTextSize(&text);
	BFont updatedBold, updatedLink, updatedPlain;
	rgb_color updatedBoldColor, updatedLinkColor;
	text.GetFontAndColor(0, &updatedBold, &updatedBoldColor);
	text.GetFontAndColor(5, &updatedLink, &updatedLinkColor);
	text.GetFontAndColor(10, &updatedPlain);
	Check(updatedBold.Size() == be_plain_font->Size());
	Check(updatedLink.Size() == be_plain_font->Size());
	Check(updatedPlain.Size() == be_plain_font->Size());
	Check(updatedBold.FamilyAndStyle() == originalBold.FamilyAndStyle());
	Check(updatedBold.Face() == originalBold.Face());
	Check(updatedLink.FamilyAndStyle() == originalLink.FamilyAndStyle());
	Check(SameColor(updatedBoldColor, originalBoldColor));
	Check(SameColor(updatedLinkColor, originalLinkColor));
	int32 start, end;
	text.GetSelection(&start, &end);
	Check(start == 5 && end == 9);
	Check(std::strcmp(text.Text(), "Bold link plain") == 0);
}

static void
CheckTitleRange()
{
	BTextView title("title");
	title.SetStylable(true);
	title.SetText("Whole title");
	title.Select(1, 2);
	MediaWindowScale::ApplyTitleFont(&title);
	BFont first, last;
	title.GetFontAndColor(0, &first);
	title.GetFontAndColor(title.TextLength() - 1, &last);
	Check(first.Size() == be_plain_font->Size() * MediaHeaderStyle::kTitleScale);
	Check(last.Size() == first.Size());
	Check(first.FamilyAndStyle() == be_bold_font->FamilyAndStyle());
}

int
main()
{
	BApplication application("application/x-vnd.Haify-MediaWindowScaleTest");
	CheckDescriptionStyles();
	CheckTitleRange();
	std::puts("Media window scaling tests passed");
	return 0;
}
