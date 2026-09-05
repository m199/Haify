#include "PlaylistTrackRow.h"

#include "DropMarkerStyle.h"

#include <Font.h>
#include <InterfaceDefs.h>
#include <View.h>

TrackStringField::TrackStringField(const char* string)
	:
	BStringField(string),
	fIsPlaying(false),
	fDropMarkerPosition(0)
{
}


TrackStringColumn::TrackStringColumn(const char* title, float width,
	float minWidth, float maxWidth, uint32 truncate, alignment align)
	:
	BStringColumn(title, width, minWidth, maxWidth, truncate, align)
{
}


static void
DrawDropMarkerLine(BView* parent, BRect rect, int32 position)
{
	if (position == 0)
		return;

	rgb_color originalColor = parent->HighColor();
	float originalPenSize = parent->PenSize();
	parent->SetHighColor(HaifyDropMarkerColor());
	parent->SetPenSize(2.0f);
	float y = position < 0 ? rect.top + 1.0f : rect.bottom - 1.0f;
	parent->StrokeLine(BPoint(rect.left - 2.0f, y),
		BPoint(rect.right + 2.0f, y));
	parent->SetPenSize(originalPenSize);
	parent->SetHighColor(originalColor);
}


void
TrackStringColumn::DrawField(BField* field, BRect rect, BView* parent)
{
	TrackStringField* stringField = dynamic_cast<TrackStringField*>(field);
	if (!stringField || !stringField->fIsPlaying) {
		BStringColumn::DrawField(field, rect, parent);
		if (stringField)
			DrawDropMarkerLine(parent, rect,
				stringField->fDropMarkerPosition);
		return;
	}
	BFont font;
	parent->GetFont(&font);
	BFont boldFont(be_bold_font);
	boldFont.SetSize(font.Size());
	parent->SetFont(&boldFont);
	BStringColumn::DrawField(field, rect, parent);
	parent->SetFont(&font);
	DrawDropMarkerLine(parent, rect, stringField->fDropMarkerPosition);
}


TrackIntegerField::TrackIntegerField(int32 value)
	:
	BIntegerField(value),
	fDropMarkerPosition(0)
{
}


TrackIntegerColumn::TrackIntegerColumn(const char* title, float width,
	float minWidth, float maxWidth, alignment align)
	:
	BIntegerColumn(title, width, minWidth, maxWidth, align)
{
}


void
TrackIntegerColumn::DrawField(BField* field, BRect rect, BView* parent)
{
	TrackIntegerField* integerField = dynamic_cast<TrackIntegerField*>(field);
	BIntegerColumn::DrawField(field, rect, parent);
	if (integerField)
		DrawDropMarkerLine(parent, rect,
			integerField->fDropMarkerPosition);
}


TrackRow::TrackRow(const std::string& uri, int32 playlistPosition)
	:
	BRow(),
	fTrackUri(uri),
	fPlaylistPosition(playlistPosition)
{
}


bool
TrackRow::SetPlaying(bool playing)
{
	bool changed = false;
	for (int32 column = 1; column <= 6; column++) {
		TrackStringField* field =
			dynamic_cast<TrackStringField*>(GetField(column));
		if (field && field->fIsPlaying != playing) {
			field->fIsPlaying = playing;
			changed = true;
		}
	}
	return changed;
}


bool
TrackRow::SetDropMarkerPosition(int32 position)
{
	position = position < 0 ? -1 : position > 0 ? 1 : 0;
	bool changed = false;
	TrackIntegerField* numberField =
		dynamic_cast<TrackIntegerField*>(GetField(0));
	if (numberField && numberField->fDropMarkerPosition != position) {
		numberField->fDropMarkerPosition = position;
		changed = true;
	}
	for (int32 column = 0; column <= 6; column++) {
		TrackStringField* field =
			dynamic_cast<TrackStringField*>(GetField(column));
		if (field && field->fDropMarkerPosition != position) {
			field->fDropMarkerPosition = position;
			changed = true;
		}
	}
	return changed;
}
