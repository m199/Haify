#include "PlaylistCoverController.h"
#include "PlaylistMutationResponse.h"
#include <limits>
#include <utility>

PlaylistCoverError
EncodePlaylistCover(const std::vector<uint8_t>& bytes, std::string& encoded)
{
	if (bytes.size() > kPlaylistCoverPayloadLimit / 4 * 3)
		return PlaylistCoverError::TooLarge;
	if (bytes.size() < 4 || bytes[0] != 0xff || bytes[1] != 0xd8)
		return PlaylistCoverError::NotJpeg;
	static const char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string result;
	result.reserve(((bytes.size() + 2) / 3) * 4);
	for (size_t i = 0; i < bytes.size(); i += 3) {
		uint32_t value = uint32_t(bytes[i]) << 16;
		if (i + 1 < bytes.size()) value |= uint32_t(bytes[i + 1]) << 8;
		if (i + 2 < bytes.size()) value |= bytes[i + 2];
		result += alphabet[(value >> 18) & 0x3f];
		result += alphabet[(value >> 12) & 0x3f];
		result += i + 1 < bytes.size() ? alphabet[(value >> 6) & 0x3f] : '=';
		result += i + 2 < bytes.size() ? alphabet[value & 0x3f] : '=';
	}
	encoded = std::move(result);
	return PlaylistCoverError::None;
}

bool
PlaylistCoverController::Begin(const std::string& playlistId, bool owned,
	PlaylistCoverCommand& command)
{
	if (Pending() || !owned || playlistId.empty()
			|| fNextRequest == std::numeric_limits<int64_t>::max())
		return false;
	fPending = {++fNextRequest, playlistId};
	command = fPending;
	return true;
}

bool
PlaylistCoverController::Complete(const PlaylistCoverResult& result)
{
	if (!Pending() || result.command.requestId != fPending.requestId
			|| result.command.playlistId != fPending.playlistId)
		return false;
	fPending = {};
	return true;
}

static PlaylistCoverResult
CoverReadResult(const PlaylistCoverCommand& command, bool ok, const nlohmann::json& images)
{
	PlaylistCoverResult result;
	result.command = command;
	result.error = PlaylistCoverError::RefreshFailed;
	result.status = PlaylistMutationResponse::Status(images);
	if (!ok || !images.is_array() || images.empty() || !images[0].is_object())
		return result;
	auto url = images[0].find("url");
	if (url != images[0].end() && url->is_string() && !url->get_ref<const std::string&>().empty()) {
		result.coverUrl = url->get<std::string>();
		result.error = PlaylistCoverError::None;
	}
	return result;
}

bool
DispatchPlaylistCover(const PlaylistCoverCommand& command,
	const std::vector<uint8_t>& bytes, const PlaylistCoverUpload& upload,
	const PlaylistCoverRead& read, PlaylistCoverCompletion complete)
{
	if (command.requestId <= 0 || command.playlistId.empty() || !upload || !read || !complete)
		return false;
	PlaylistCoverResult preparation;
	preparation.command = command;
	std::string encoded;
	preparation.error = EncodePlaylistCover(bytes, encoded);
	if (preparation.error != PlaylistCoverError::None) {
		complete(preparation);
		return true;
	}
	upload(command.playlistId, encoded, [command, read, complete](bool ok, const nlohmann::json& data) {
		if (!ok) {
			PlaylistCoverResult failure;
			failure.command = command;
			failure.status = PlaylistMutationResponse::Status(data);
			complete(failure);
			return;
		}
		read(command.playlistId, [command, complete](bool success, const nlohmann::json& images) {
			complete(CoverReadResult(command, success, images));
		});
	});
	return true;
}
