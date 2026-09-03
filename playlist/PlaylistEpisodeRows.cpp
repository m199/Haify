#include "PlaylistEpisodeRows.h"

#include "DescriptionTextFormatter.h"

#include <ColumnTypes.h>
#include <DateFormat.h>
#include <DateTime.h>
#include <Message.h>
#include <String.h>

#include <cstdio>

namespace {

std::string
FormatEpisodeDate(const std::string& isoDate)
{
	int year = 0;
	int month = 0;
	int day = 0;
	if (sscanf(isoDate.c_str(), "%d-%d-%d", &year, &month, &day) < 3
			|| year <= 0 || month <= 0 || day <= 0) {
		return isoDate;
	}

	BDate date(year, month, day);
	BDateFormat formatter;
	BString result;
	if (formatter.Format(result, date, B_SHORT_DATE_FORMAT) == B_OK)
		return std::string(result.String());
	return isoDate;
}


bool
MessageEpisodeAt(BMessage* message, int32 index, PlaylistEpisode& episode)
{
	int32 number;
	if (!message || message->FindInt32("number", index, &number) != B_OK)
		return false;

	const char* title = message->FindString("title", index);
	const char* description = message->FindString("description", index);
	const char* date = message->FindString("date", index);
	const char* duration = message->FindString("duration", index);
	const char* trackUri = message->FindString("trackUri", index);
	episode = MakePlaylistEpisode(number, title ? title : "",
		description ? description : "", date ? date : "",
		duration ? duration : "", trackUri ? trackUri : "");
	return true;
}

}


TrackRow*
PlaylistEpisodeRowFromEpisode(const PlaylistEpisode& episode,
	const std::string& currentPlayingTrackUri)
{
	TrackRow* row = new TrackRow(episode.trackUri);
	row->fDescription = episode.description;
	row->SetField(new BIntegerField(episode.number), 0);
	row->SetField(new TrackStringField(episode.title.c_str()), 1);
	std::string displayDescription = FormatMediaDescription(
		episode.description);
	row->SetField(new TrackStringField(displayDescription.c_str()), 2);
	row->SetField(new TrackStringField(""), 3);
	row->SetField(new TrackStringField(""), 4);
	row->SetField(new TrackStringField(FormatEpisodeDate(
		episode.date).c_str()), 5);
	row->SetField(new TrackStringField(episode.duration.c_str()), 6);
	row->SetPlaying(!currentPlayingTrackUri.empty()
		&& row->fTrackUri == currentPlayingTrackUri);
	return row;
}


std::vector<PlaylistEpisode>
PlaylistEpisodesFromMessage(BMessage* message)
{
	std::vector<PlaylistEpisode> episodes;
	PlaylistEpisode episode;
	for (int32 i = 0; MessageEpisodeAt(message, i, episode); i++)
		episodes.push_back(episode);
	return episodes;
}
