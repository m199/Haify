#include "PlaylistTrackRow.h"

#include <Font.h>
#include <View.h>

TrackStringField::TrackStringField(const char* string)
	:
	BStringField(string),
	fIsPlaying(false)
{
}


TrackStringColumn::TrackStringColumn(const char* title, float width,
	float minWidth, float maxWidth, uint32 truncate, alignment align)
	:
	BStringColumn(title, width, minWidth, maxWidth, truncate, align)
{
}


void
TrackStringColumn::DrawField(BField* field, BRect rect, BView* parent)
{
	TrackStringField* stringField = dynamic_cast<TrackStringField*>(field);
	if (!stringField || !stringField->fIsPlaying) {
		BStringColumn::DrawField(field, rect, parent);
		return;
	}
	BFont font;
	parent->GetFont(&font);
	BFont boldFont(be_bold_font);
	boldFont.SetSize(font.Size());
	parent->SetFont(&boldFont);
	BStringColumn::DrawField(field, rect, parent);
	parent->SetFont(&font);
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
