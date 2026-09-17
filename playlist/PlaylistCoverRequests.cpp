#include "PlaylistCoverRequests.h"
#include "PlaylistCoverMessages.h"
#include "spotify/api/PlaylistApi.h"
#include <File.h>
#include <Messenger.h>

static PlaylistCoverError
ReadCoverFile(const entry_ref& ref, std::vector<uint8_t>& bytes)
{
	BFile file(&ref, B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size < 0)
		return PlaylistCoverError::FileRead;
	if (size > static_cast<off_t>(kPlaylistCoverPayloadLimit / 4 * 3))
		return PlaylistCoverError::TooLarge;
	if (size < 4)
		return PlaylistCoverError::NotJpeg;
	bytes.resize(static_cast<size_t>(size));
	return file.Read(bytes.data(), bytes.size()) == size
		? PlaylistCoverError::None : PlaylistCoverError::FileRead;
}

bool
PlaylistCoverRequests::Send(PlaylistApi& api, const PlaylistCoverCommand& command,
	const entry_ref& file, const BMessenger& target)
{
	if (command.requestId <= 0 || command.playlistId.empty())
		return false;
	auto complete = [target](const PlaylistCoverResult& result) {
		BMessage message = MakePlaylistCoverResultMessage(result);
		target.SendMessage(&message);
	};
	std::vector<uint8_t> bytes;
	PlaylistCoverResult preparation;
	preparation.command = command;
	preparation.error = ReadCoverFile(file, bytes);
	if (preparation.error != PlaylistCoverError::None) {
		complete(preparation);
		return true;
	}
	return DispatchPlaylistCover(command, bytes,
		[&api](const std::string& id, const std::string& encoded, JsonCallback done) {
			api.UploadPlaylistImage(id, encoded, done);
		}, [&api](const std::string& id, JsonCallback done) {
			api.GetPlaylistImages(id, done);
		}, complete);
}
