#include "discover/DiscoverRowFactory.h"
#include "discover/DiscoverTabPolicy.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Discover row factory tests require assertions; compile without NDEBUG.
#endif

using nlohmann::json;
using DiscoverRowFactory::SavedEpisodeRows;

static void
AssertRow(const DiscoverRowData& actual, const DiscoverRowData& expected)
{
	assert(actual.vals == expected.vals);
	assert(actual.uris == expected.uris);
	assert(actual.ttls == expected.ttls);
	assert(actual.writable == expected.writable);
	assert(actual.owned == expected.owned);
}

static json
EpisodeFixture()
{
	return json::parse(R"({
		"type": "episode", "id": "episode1", "uri": "spotify:episode:old",
		"name": "First episode", "release_date": "2026-09-01", "duration_ms": 3661999,
		"show": {"name": "Example show", "id": "show1", "uri": "spotify:show:old"},
		"resume_point": {"fully_played": false, "resume_position_ms": 61234}
	})");
}

static json
PageWithEpisode(const json& episode)
{
	return {{"items", json::array({{{"episode", episode}}})}};
}

static DiscoverRowData
MapEpisode(const json& episode, bool showProgress = true)
{
	auto result = SavedEpisodeRows(PageWithEpisode(episode), showProgress, "Erledigt");
	assert(result && result->size() == 1);
	return result->front();
}

static void
TestColumnsAndUriPrecedence()
{
	const json episode = EpisodeFixture();
	const json original = episode;
	AssertRow(MapEpisode(episode),
		{{"First episode", "Example show", "2026-09-01", "61:01", "1:01"},
		 {"spotify:episode:episode1", "spotify:show:show1", "", "", ""},
		 {"First episode", "Example show", "", "", ""}});
	assert(episode == original);
	assert(MapEpisode(episode, false).vals[4].empty());
}

static void
TestProgressAndDurations()
{
	json episode = EpisodeFixture();
	episode["resume_point"]["fully_played"] = true;
	assert(MapEpisode(episode).vals[4] == "Erledigt");
	assert(MapEpisode(episode, false).vals[4].empty());
	episode["resume_point"]["fully_played"] = "true";
	assert(MapEpisode(episode).vals[4] == "1:01");
	episode["resume_point"]["resume_position_ms"] = -100;
	episode["duration_ms"] = -1;
	assert(MapEpisode(episode).vals[3] == "0:00");
	assert(MapEpisode(episode).vals[4] == "0:00");
	episode["duration_ms"] = 7200000;
	assert(MapEpisode(episode).vals[3] == "120:00");
	episode["duration_ms"] = "61000";
	episode["resume_point"]["resume_position_ms"] = 61000.5;
	assert(MapEpisode(episode).vals[3] == "0:00");
	assert(MapEpisode(episode).vals[4] == "0:00");
	episode["resume_point"] = json::object();
	assert(MapEpisode(episode).vals[4] == "0:00");
	for (const json& resume : {json(), json(42), json::array()}) {
		episode["resume_point"] = resume;
		assert(MapEpisode(episode).vals[4].empty());
	}
	episode.erase("resume_point");
	assert(MapEpisode(episode).vals[4].empty());
}

static void
TestOptionalMetadataAndUriFallback()
{
	json episode = EpisodeFixture();
	episode.erase("id");
	episode["show"]["id"] = "";
	AssertRow(MapEpisode(episode),
		{{"First episode", "Example show", "2026-09-01", "61:01", "1:01"},
		 {"spotify:episode:old", "spotify:show:old", "", "", ""},
		 {"First episode", "Example show", "", "", ""}});
	episode["id"] = 42;
	episode["show"] = nullptr;
	episode["release_date"] = false;
	episode.erase("duration_ms");
	episode.erase("resume_point");
	AssertRow(MapEpisode(episode),
		{{"First episode", "", "", "0:00", ""},
		 {"spotify:episode:old", "", "", "", ""},
		 {"First episode", "", "", "", ""}});
}

static void
TestInvalidEnvelopesAndEmptyPages()
{
	const json invalid[] = {nullptr, 42, json::array(), json::object(),
		{{"items", nullptr}}, {{"items", "bad"}}, {{"items", json::object()}}};
	for (const auto& page : invalid)
		assert(!SavedEpisodeRows(page, true, "Done"));
	auto empty = SavedEpisodeRows({{"items", json::array()}}, true, "Done");
	assert(empty && empty->empty());
	const json unusable[] = {nullptr, 42, json::object(), {{"episode", nullptr}},
		{{"episode", json::array()}}, {{"episode", json::object()}}};
	for (const auto& item : unusable) {
		auto result = SavedEpisodeRows({{"items", json::array({item})}}, true, "Done");
		assert(result && result->empty());
	}
}

static void
TestInvalidEpisodes()
{
	const json invalidNames[] = {nullptr, 42, ""};
	for (const auto& name : invalidNames) {
		json episode = EpisodeFixture();
		episode["name"] = name;
		auto result = SavedEpisodeRows(PageWithEpisode(episode), true, "Done");
		assert(result && result->empty());
	}
	for (const char* uri : {"", "spotify:episode:", "spotify:track:track1"}) {
		json episode = EpisodeFixture();
		episode.erase("id");
		episode["uri"] = uri;
		auto result = SavedEpisodeRows(PageWithEpisode(episode), true, "Done");
		assert(result && result->empty());
	}
	json episode = EpisodeFixture();
	episode["type"] = "show";
	auto result = SavedEpisodeRows(PageWithEpisode(episode), true, "Done");
	assert(result && result->empty());
	episode.erase("type");
	result = SavedEpisodeRows(PageWithEpisode(episode), true, "Done");
	assert(result && result->empty());
}

static void
TestSourceOrderAndDuplicates()
{
	json first = EpisodeFixture();
	json second = first;
	second["id"] = "episode2";
	second["name"] = "Second episode";
	json page = {{"items", json::array({{{"episode", second}}, nullptr,
		{{"episode", first}}, {{"episode", second}}})}, {"total", 20}};
	const json original = page;
	auto result = SavedEpisodeRows(page, true, "Erledigt");
	assert(result && result->size() == 3);
	assert((*result)[0].uris[0] == "spotify:episode:episode2");
	assert((*result)[1].uris[0] == "spotify:episode:episode1");
	AssertRow((*result)[0], (*result)[2]);
	assert(page == original);
}

static void
TestResolvedLibraryRows()
{
	using namespace DiscoverRowFactory;
	DiscoverRowData row;
	row.writable = false;
	row.owned = true;
	assert(BuildResolvedLibraryRow(TAB_SAVED_EPISODES, "spotify:episode:requested",
		EpisodeFixture(), true, "Erledigt", row));
	assert(row.uris[0] == "spotify:episode:requested" && row.vals[4] == "1:01");
	assert(!row.writable && row.owned);
	json invalid = EpisodeFixture();
	invalid["name"] = "";
	assert(!BuildResolvedLibraryRow(TAB_SAVED_EPISODES, "spotify:episode:requested",
		invalid, false, "Done", row));
	for (int32_t tab : {TAB_SAVED_ALBUMS, TAB_PODCASTS, TAB_FOLLOWED_ARTISTS, TAB_AUDIOBOOKS}) {
		assert(BuildResolvedLibraryRow(tab, "requested", json::object(), false, "Done", row));
		assert(row.vals[0] == "Unknown" && row.uris[0] == "requested");
		assert(row.vals.size() == 2 && row.uris.size() == 2 && row.ttls.size() == 2);
	}
	assert(!BuildResolvedLibraryRow(TAB_TOP_TRACKS, "requested", json::object(), false, "Done", row));
}

static void
TestTrackAndAlbumTabs()
{
	using namespace DiscoverRowFactory;
	json item = {{"name", "Track"}, {"uri", "spotify:track:one"},
		{"artists", json::array({{{"name", "Artist"}, {"uri", "spotify:artist:one"}}})}};
	json page = {{"items", json::array({nullptr, item, item})}};
	auto rows = TopTrackRows(page);
	assert(rows.size() == 2);
	AssertRow(rows[0], {{"Track", "Artist"}, {"spotify:track:one", "spotify:artist:one"},
		{"Track", "Artist"}});
	AssertRow(rows[0], rows[1]);
	item["name"] = "Album";
	item["uri"] = "spotify:album:one";
	auto releases = NewReleaseRows({{"albums", {{"items", json::array({item})}}}});
	auto albums = SavedAlbumRows({{"items", json::array({{{"album", item}}})}});
	assert(releases.size() == 1 && albums.size() == 1);
	AssertRow(releases[0], albums[0]);
	assert(albums[0].uris[0] == "spotify:album:one");
	assert(TopTrackRows(json::object()).empty());
	// Preserve the legacy error for a wrong optional field type on these endpoints.
	item["artists"][0]["name"] = 42;
	bool rejected = false;
	try {
		TopTrackRows({{"items", json::array({item})}});
	} catch (const json::exception&) {
		rejected = true;
	}
	assert(rejected);
}

static void
TestPlaylistOwnershipAndColumns()
{
	json owned = {{"uri", "spotify:playlist:one"}, {"name", "One"},
		{"owner", {{"account_id", "account"}, {"display_name", "Owner"}}}};
	json legacy = owned;
	legacy["owner"] = {{"id", "account"}};
	json collaborative = {{"uri", "spotify:playlist:two"}, {"collaborative", true}};
	json readonly = {{"uri", "spotify:playlist:three"}, {"collaborative", "true"}};
	json page = {{"items", json::array({owned, nullptr, legacy, collaborative, readonly, owned})}};
	const json original = page;
	auto rows = DiscoverRowFactory::PlaylistRows(page, "account");
	assert(rows && rows->size() == 5);
	AssertRow((*rows)[0], {{"One", "Owner"}, {"spotify:playlist:one", ""}, {"One", ""}, true, true});
	assert((*rows)[1].owned && (*rows)[1].writable && (*rows)[1].vals[1] == "Spotify");
	assert(!(*rows)[2].owned && (*rows)[2].writable && (*rows)[2].vals[0] == "Unknown");
	assert(!(*rows)[3].owned && !(*rows)[3].writable);
	AssertRow((*rows)[0], (*rows)[4]);
	assert(page == original);
	rows = DiscoverRowFactory::PlaylistRows(page, "");
	assert(rows && !(*rows)[0].owned && !(*rows)[0].writable && (*rows)[2].writable);
}

static void
TestPlaylistPageValidation()
{
	for (const json& page : {json(), json::array(), json::object(), json{{"items", nullptr}}})
		assert(!DiscoverRowFactory::PlaylistRows(page, "account"));
	auto empty = DiscoverRowFactory::PlaylistRows({{"items", json::array()}}, "account");
	assert(empty && empty->empty());
	auto skipped = DiscoverRowFactory::PlaylistRows({{"items", json::array({
		nullptr, 42, json::object(), {{"uri", 42}}, {{"uri", ""}}})}}, "account");
	assert(skipped && skipped->empty());
}

static void
TestArtistsPodcastsAndAudiobooks()
{
	using namespace DiscoverRowFactory;
	json artist = {{"name", "Artist"}, {"uri", "spotify:artist:one"}, {"genres", {"Jazz"}}};
	auto top = TopArtistRows({{"items", json::array({artist})}});
	auto followed = FollowedArtistRows({{"artists", {{"items", json::array({artist})}}}});
	assert(top.size() == 1 && followed.size() == 1);
	AssertRow(top[0], followed[0]);
	assert(top[0].vals[1] == "Jazz");
	json show = {{"type", "show"}, {"id", "one"}, {"uri", "spotify:show:one"},
		{"name", "Show"}, {"publisher", "Publisher"}};
	json saved = {{"items", json::array({{{"show", show}}, nullptr})}};
	assert(PodcastRows(saved, {}).size() == 1);
	assert(PodcastRows(saved, {"one"}).empty());
	saved["items"][0]["show"]["uri"] = "";
	assert(PodcastRows(saved, {}).empty());
	json book = {{"type", "audiobook"}, {"id", "book"},
		{"authors", json::array({{{"name", "Author"}}})}};
	auto books = AudiobookRows({{"items", json::array({book, show})}});
	assert(books.size() == 1);
	AssertRow(books[0], {{"Unknown", "Author"}, {"spotify:audiobook:book", ""}, {"Unknown", ""}});
}

static void
TestCreatedPlaylistResponse()
{
	auto created = DiscoverRowFactory::CreatedPlaylist({{"id", "one"}}, "Requested");
	assert(created && created->vals[0] == "Requested" && created->uris[0] == "spotify:playlist:one");
	assert(created->owned && created->writable);
	auto wrapped = DiscoverRowFactory::CreatedPlaylist({{"body", R"({"id":"one","name":"Server"})"}}, "Requested");
	assert(wrapped && wrapped->vals[0] == "Server");
	assert(!DiscoverRowFactory::CreatedPlaylist({{"body", "broken"}}, "Requested"));
	assert(!DiscoverRowFactory::CreatedPlaylist({{"name", "Missing ID"}}, "Requested"));
}

int
main()
{
	TestColumnsAndUriPrecedence();
	TestProgressAndDurations();
	TestOptionalMetadataAndUriFallback();
	TestInvalidEnvelopesAndEmptyPages();
	TestInvalidEpisodes();
	TestSourceOrderAndDuplicates();
	TestResolvedLibraryRows();
	TestTrackAndAlbumTabs();
	TestPlaylistOwnershipAndColumns();
	TestPlaylistPageValidation();
	TestArtistsPodcastsAndAudiobooks();
	TestCreatedPlaylistResponse();
	std::puts("Discover row factory tests passed.");
	return 0;
}
