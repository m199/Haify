#pragma once

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <InterfaceDefs.h>
#include <Rect.h>
#include <SupportDefs.h>

#include <string>

class TrackStringField : public BStringField {
public:
	bool fIsPlaying;

	TrackStringField(const char* string);
};

class TrackStringColumn : public BStringColumn {
public:
	TrackStringColumn(const char* title, float width, float minWidth,
		float maxWidth, uint32 truncate,
		alignment align = B_ALIGN_LEFT);

	virtual void DrawField(BField* field, BRect rect, BView* parent);
};

class TrackRow : public BRow {
public:
	std::string fTrackUri;
	std::string fArtistUri;
	std::string fAlbumUri;
	std::string fDescription;
	int32 fPlaylistPosition;

	TrackRow(const std::string& uri, int32 playlistPosition = -1);

	bool SetPlaying(bool playing);
};
