#include "playlist/PlaylistTrackRow.h"
#include "settings/SettingsController.h"
#include "ui/views/DiscoverListView.h"
#include "ui/views/FontScaledListView.h"
#include "ui/UiScalePolicy.h"

#include <Application.h>
#include <Message.h>
#include <ScrollBar.h>
#include <Window.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#ifdef NDEBUG
#error List row scaling tests require assertions; compile without NDEBUG.
#endif

static void
CheckCondition(bool condition, const char* expression, const char* function,
	int line)
{
	if (condition)
		return;
	std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, line, function, expression);
	std::abort();
}

#define Check(condition) CheckCondition((condition), #condition, __func__, __LINE__)

// Linked renderers request dropmarker colors. Keep this fixture independent of
// the user's settings files and the production loader's migration side effects.
HaifySettings
SettingsController::Load()
{
	return {};
}

class ObservedList : public FontScaledListView {
public:
	ObservedList() : FontScaledListView("rows", B_NAVIGABLE, B_PLAIN_BORDER, true) {}
	void SelectionChanged() override
	{
		if (!IsResizingRows())
			++notifications;
		FontScaledListView::SelectionChanged();
	}
	void ItemInvoked() override { ++invocations; }
	int notifications = 0;
	int invocations = 0;
};

class ListFixture {
public:
	ListFixture()
	{
		window = new BWindow(BRect(0, 0, 320, 240), "row test", B_TITLED_WINDOW, 0);
		list = new ObservedList;
		window->AddChild(list);
		list->ResizeTo(300, 210);
		list->AddColumn(new TrackIntegerColumn("#", 60, 30, 100), 0);
		list->AddColumn(new TrackStringColumn("Title", 600, 60, 900, B_TRUNCATE_END), 1);
		list->SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	}
	~ListFixture() { window->Quit(); }
	ListFixture(const ListFixture&) = delete;
	ListFixture& operator=(const ListFixture&) = delete;
	BWindow* window;
	ObservedList* list;
};

static TrackRow*
MakeTrack(int32 position)
{
	auto row = new TrackRow("spotify:track:duplicate", position);
	row->fArtistUri = "spotify:artist:artist";
	row->fAlbumUri = "spotify:album:album";
	row->fDescription = "episode description";
	row->SetField(new TrackIntegerField(position + 1), 0);
	row->SetField(new TrackStringField("Same title with a long suffix"), 1);
	row->SetPlaying(true);
	return row;
}

static void
CheckIdentityAndRollback()
{
	ListFixture fixture;
	auto list = fixture.list;
	for (int32 index = 0; index < 60; ++index)
		list->AddRow(MakeTrack(index));
	list->SetSortColumn(list->ColumnAt(1), false, true);
	BMessage before;
	list->SaveState(&before);
	BRow* reorder = list->RowAt(3);
	BRow* addition = list->RowAt(11);
	std::unique_ptr<TrackRow> removed(MakeTrack(77));
	std::unique_ptr<TrackRow> cleared(MakeTrack(78));
	auto title = static_cast<TrackStringField*>(reorder->GetField(1));
	title->SetWidth(80);
	title->SetClippedString("Old...");
	list->AddToSelection(reorder);
	list->AddToSelection(addition);
	list->SetFocusRow(addition);
	int notifications = list->notifications;
	list->SetDetachedRowsProvider([&]() {
		return std::vector<BRow*>{removed.get(), cleared.get()};
	});
	for (float size : {24.0f, 15.0f, 12.0f}) {
		BFont font(be_plain_font);
		font.SetSize(size);
		Check(list->RefreshRowFont(font) == B_OK);
		Check(list->CountRows() == 60 && list->RowAt(3) == reorder);
		Check(list->RowAt(11) == addition && list->FocusRow() == addition);
		Check(list->CurrentSelection() == addition);
		Check(list->CurrentSelection(addition) == reorder);
		Check(list->CurrentSelection(reorder) == nullptr);
		Check(list->notifications == notifications && list->invocations == 0);
		Check(reorder->GetField(1) == title && title->fIsPlaying);
		Check(title->Width() == 0 && !title->HasClippedString());
		Check(std::strcmp(title->String(), "Same title with a long suffix") == 0);
		Check(removed->Height() == UiScale::ListRowHeight(size));
		Check(cleared->Height() == removed->Height());
		for (int32 index = 0; index < list->CountRows(); ++index) {
			auto row = dynamic_cast<TrackRow*>(list->RowAt(index));
			Check(row && row->fPlaylistPosition == index);
			Check(row->Height() == removed->Height());
			Check(row->fDescription == "episode description");
		}
	}
	BMessage after;
	list->SaveState(&after);
	Check(before.HasSameData(after));
	BMessage fonts(B_FONTS_UPDATED);
	list->MessageReceived(&fonts);
	Check(reorder->Height() == UiScale::ListRowHeight(be_plain_font->Size()));
	Check(list->notifications == notifications && list->RowAt(3) == reorder);
	// The owner's original references remain usable for rollback and commit.
	list->SetDetachedRowsProvider({});
	BRow* restored = removed.release();
	list->AddRow(restored);
	Check(list->IndexOf(restored) >= 0);
	list->RemoveRow(addition);
	delete addition;
}

static void
CheckViewport(float initialFontSize)
{
	ListFixture fixture;
	auto list = fixture.list;
	for (int32 index = 0; index < 80; ++index)
		list->AddRow(MakeTrack(index));
	BFont font(be_plain_font);
	font.SetSize(initialFontSize);
	Check(list->RefreshRowFont(font) == B_OK);
	BRect anchor;
	Check(list->GetRowRect(list->RowAt(20), &anchor));
	float offset = 0.25f * (list->RowAt(20)->Height() + 1);
	auto vertical = list->ContentScrollBar(B_VERTICAL);
	auto horizontal = list->ContentScrollBar(B_HORIZONTAL);
	Check(vertical && horizontal);
	Check(vertical->Target() == list->ScrollView());
	Check(horizontal->Target() && horizontal->Target() != list->ScrollView());
	vertical->SetValue(anchor.top + offset);
	horizontal->SetValue(80);
	float horizontalValue = horizontal->Value();
	Check(vertical->Value() >= anchor.top && vertical->Value() <= anchor.bottom);
	Check(horizontalValue > 0);
	Check(list->ScrollView()->Bounds().left == horizontalValue);
	Check(horizontal->Target()->Bounds().left == horizontalValue);
	// BScrollBar rounds SetValue to whole pixels. Preserve the resulting offset,
	// not the requested quarter-row offset, which may have been rounded up.
	float anchorFraction = (vertical->Value() - anchor.top)
		/ (list->RowAt(20)->Height() + 1);
	font.SetSize(24);
	Check(list->RefreshRowFont(font) == B_OK);
	Check(list->GetRowRect(list->RowAt(20), &anchor));
	float expectedScroll = anchor.top + anchorFraction * 35;
	Check(std::fabs(vertical->Value() - expectedScroll) <= 0.5f);
	Check(horizontal->Value() == horizontalValue);
	Check(list->ScrollView()->Bounds().left == horizontalValue);
	Check(horizontal->Target()->Bounds().left == horizontalValue);
	Check(list->ScrollView()->Bounds().top == vertical->Value());
	// Reattaching an inactive tab catches a font change it could have missed.
	Check(list->RemoveSelf());
	fixture.window->AddChild(list);
	Check(list->RowAt(20)->Height() == UiScale::ListRowHeight(be_plain_font->Size()));
	Check(list->ContentScrollBar(B_HORIZONTAL) == horizontal);
	Check(list->ScrollView()->Bounds().left == horizontalValue);
	Check(horizontal->Target()->Bounds().left == horizontalValue);
}

class CountedField : public BField {
public:
	explicit CountedField(int& destroyed) : fDestroyed(destroyed) {}
	~CountedField() override { ++fDestroyed; }
private:
	int& fDestroyed;
};

static void
CheckDetachedFieldsAndEmptyList()
{
	ListFixture fixture;
	int destroyed = 0;
	{
		BRow opaque;
		for (int32 index = 0; index < 20; ++index)
			opaque.SetField(new CountedField(destroyed), index);
		BField* last = opaque.GetField(19);
		DiscoverRow discover({"Playlist", "Owner"}, {"spotify:playlist:p", "owner"},
			{"Title", "Owner title"}, false, true);
		discover.fIsPlaying = true;
		discover.SetDropFeedback(1, true);
		BField* first = discover.GetField(0);
		fixture.list->SetDetachedRowsProvider([&]() {
			return std::vector<BRow*>{&opaque, &discover};
		});
		BFont font(be_plain_font);
		font.SetSize(24);
		Check(fixture.list->RefreshRowFont(font) == B_OK);
		Check(opaque.Height() == 34 && opaque.GetField(19) == last && destroyed == 0);
		Check(discover.Height() == 34 && discover.GetField(0) == first);
		Check(discover.fUris[0] == "spotify:playlist:p" && discover.fTitles[0] == "Title");
		Check(discover.fIsPlaying && discover.fOwned && !discover.fWritable);
		Check(discover.fDropTargetHighlight && discover.fDropMarkerPosition == 1);
		auto field = static_cast<BoldStringField*>(discover.GetField(1));
		Check(field->fDropTargetFillToRight && !field->fEnabled);
		fixture.list->SetDetachedRowsProvider({});
		Check(fixture.list->RefreshRowFont(font) == B_OK);
	}
	Check(destroyed == 20);
}

static void
CheckFailureIsAtomic()
{
	ListFixture fixture;
	auto list = fixture.list;
	BRow* original = MakeTrack(0);
	list->AddRow(original);
	list->AddToSelection(original);
	BRow oversized;
	for (int32 index = 0; index < 21; ++index)
		oversized.SetField(new BStringField("field"), index);
	list->SetDetachedRowsProvider([&]() { return std::vector<BRow*>{&oversized}; });
	BFont before;
	list->GetFont(B_FONT_ROW, &before);
	BFont changed(before);
	changed.SetSize(24);
	float height = original->Height();
	Check(list->RefreshRowFont(changed) == B_NOT_SUPPORTED);
	BFont after;
	list->GetFont(B_FONT_ROW, &after);
	Check(list->RowAt(0) == original && list->CurrentSelection() == original);
	Check(after.Size() == before.Size() && original->Height() == height);
	list->SetDetachedRowsProvider([&]() { return std::vector<BRow*>{original}; });
	Check(list->RefreshRowFont(changed) == B_BAD_VALUE);
	Check(list->RowAt(0) == original && original->Height() == height);
	list->SetDetachedRowsProvider({});
}

int
main()
{
	BApplication application("application/x-vnd.Haify-ListRowScaleTest");
	Check(UiScale::ListRowHeight(be_plain_font->Size()) == BRow().Height());
	CheckIdentityAndRollback();
	for (float size : {12.0f, 15.0f, 18.0f, 24.0f})
		CheckViewport(size);
	CheckDetachedFieldsAndEmptyList();
	CheckFailureIsAtomic();
	std::puts("List row scaling tests passed");
	return 0;
}
