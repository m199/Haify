#include "PlaylistCacheRows.h"

#include <ColumnListView.h>
#include <ColumnTypes.h>

static std::string
TrackRowStringAt(TrackRow* row, int32 column)
{
	BStringField* field = dynamic_cast<BStringField*>(row->GetField(column));
	return field ? field->String() : "";
}


static int32
TrackRowNumber(TrackRow* row, int32 fallback)
{
	BIntegerField* field = dynamic_cast<BIntegerField*>(row->GetField(0));
	return field ? field->Value() : fallback;
}


PlaylistCacheDocument::Track
CachedTrackFromRow(TrackRow* row, int32 rowIndex)
{
	PlaylistCacheDocument::Track track;
	track.number = TrackRowNumber(row, rowIndex + 1);
	track.title = TrackRowStringAt(row, 1);
	track.artist = TrackRowStringAt(row, 2);
	track.bpm = TrackRowStringAt(row, 3);
	track.key = TrackRowStringAt(row, 4);
	track.album = TrackRowStringAt(row, 5);
	track.duration = TrackRowStringAt(row, 6);
	track.uri = row->fTrackUri;
	track.artistUri = row->fArtistUri;
	track.albumUri = row->fAlbumUri;
	return track;
}


TrackRow*
CachedTrackRowFromCache(const PlaylistCacheDocument::Track& track,
	const std::string& currentPlayingTrackUri)
{
	TrackRow* row = new TrackRow(track.uri, track.number - 1);
	row->fArtistUri = track.artistUri;
	row->fAlbumUri = track.albumUri;
	row->SetField(new TrackIntegerField(track.number), 0);
	row->SetField(new TrackStringField(track.title.c_str()), 1);
	row->SetField(new TrackStringField(track.artist.c_str()), 2);
	row->SetField(new TrackStringField(track.bpm.c_str()), 3);
	row->SetField(new TrackStringField(track.key.c_str()), 4);
	row->SetField(new TrackStringField(track.album.c_str()), 5);
	row->SetField(new TrackStringField(track.duration.c_str()), 6);
	row->SetPlaying(!currentPlayingTrackUri.empty()
		&& row->fTrackUri == currentPlayingTrackUri);
	return row;
}


void
AddCachedTrackRows(const std::vector<PlaylistCacheDocument::Track>& tracks,
	BColumnListView* trackList, const std::string& currentPlayingTrackUri)
{
	for (const PlaylistCacheDocument::Track& track : tracks)
		trackList->AddRow(CachedTrackRowFromCache(track,
			currentPlayingTrackUri));
}
