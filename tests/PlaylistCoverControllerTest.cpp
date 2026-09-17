#include "playlist/PlaylistCoverController.h"
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Playlist cover tests require assertions; compile without NDEBUG.
#endif
static void Check(bool condition) { assert(condition); }

static void TestEncoding()
{
	std::string encoded = "untouched";
	Check(EncodePlaylistCover({}, encoded) == PlaylistCoverError::NotJpeg);
	Check(encoded == "untouched");
	Check(EncodePlaylistCover({0x89, 0x50, 0x4e, 0x47}, encoded) == PlaylistCoverError::NotJpeg);
	Check(EncodePlaylistCover({0xff, 0xd8, 0xff, 0xd9}, encoded) == PlaylistCoverError::None);
	Check(encoded == "/9j/2Q==");
	Check(EncodePlaylistCover({0xff, 0xd8, 0x00, 0xff, 0xd9}, encoded) == PlaylistCoverError::None);
	Check(encoded == "/9gA/9k=");
	Check(EncodePlaylistCover({0xff, 0xd8, 0, 0, 0xff, 0xd9}, encoded) == PlaylistCoverError::None);
	Check(encoded == "/9gAAP/Z");
	std::vector<uint8_t> boundary(kPlaylistCoverPayloadLimit / 4 * 3, 0);
	boundary[0] = 0xff;
	boundary[1] = 0xd8;
	Check(EncodePlaylistCover(boundary, encoded) == PlaylistCoverError::None);
	Check(encoded.size() == kPlaylistCoverPayloadLimit);
	boundary.push_back(0);
	encoded = "untouched";
	Check(EncodePlaylistCover(boundary, encoded) == PlaylistCoverError::TooLarge);
	Check(encoded == "untouched");
}

static void TestState()
{
	PlaylistCoverController controller;
	PlaylistCoverCommand command;
	Check(!controller.Begin("", true, command));
	Check(!controller.Begin("list", false, command));
	Check(controller.Begin("list", true, command));
	Check(!controller.Begin("list", true, command));
	PlaylistCoverResult result;
	result.command = command;
	result.command.requestId++;
	Check(!controller.Complete(result) && controller.Pending());
	result.command = command;
	result.command.playlistId = "other";
	Check(!controller.Complete(result));
	result.command = command;
	Check(controller.Complete(result) && !controller.Pending());
	Check(!controller.Complete(result));
	Check(controller.Begin("list", true, command));
	Check(!controller.Complete(result) && controller.Pending());
}

static nlohmann::json ImageReply(int scenario)
{
	if (scenario == 2) return {{"status", 429}};
	if (scenario == 3) return nlohmann::json::array();
	if (scenario == 4) return nlohmann::json::array({{{"url", 23}}});
	return nlohmann::json::array({{{"url", "https://image/cover"}}});
}

static void TestDispatch()
{
	for (int scenario = 0; scenario < 6; scenario++) {
		PlaylistCoverCommand command{42, "list"};
		std::vector<uint8_t> bytes = {0xff, 0xd8, 0xff, 0xd9};
		if (scenario == 0) bytes.clear();
		int uploads = 0, reads = 0, results = 0;
		JsonCallback held;
		auto upload = [&](const std::string& id, const std::string& encoded, JsonCallback done) {
			Check(id == "list" && encoded == "/9j/2Q==");
			uploads++;
			held = done;
		};
		auto read = [&](const std::string& id, JsonCallback done) {
			Check(id == "list");
			reads++;
			done(scenario != 2, ImageReply(scenario));
		};
		auto complete = [&](const PlaylistCoverResult& result) {
			results++;
			Check(result.command.requestId == 42 && result.command.playlistId == "list");
			Check(result.error == (scenario == 0 ? PlaylistCoverError::NotJpeg
				: scenario == 1 ? PlaylistCoverError::UploadFailed
				: scenario == 5 ? PlaylistCoverError::None : PlaylistCoverError::RefreshFailed));
			Check(result.coverUrl == (scenario == 5 ? "https://image/cover" : ""));
			if (scenario == 1) Check(result.status == 403);
			if (scenario == 2) Check(result.status == 429);
		};
		Check(!DispatchPlaylistCover(command, bytes, {}, read, complete));
		Check(!DispatchPlaylistCover(command, bytes, upload, {}, complete));
		Check(!DispatchPlaylistCover(command, bytes, upload, read, {}));
		Check(DispatchPlaylistCover(command, bytes, upload, read, complete));
		command.playlistId = "changed after dispatch";
		if (held) held(scenario != 1, {{"status", scenario == 1 ? 403 : 202}});
		Check(uploads == (scenario == 0 ? 0 : 1));
		Check(reads == (scenario < 2 ? 0 : 1));
		Check(results == 1);
	}
}

int main()
{
	TestEncoding();
	TestState();
	TestDispatch();
	std::puts("Playlist cover controller tests passed.");
}
