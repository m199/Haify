#pragma once

#include "spotify/api/SpotifyApiTypes.h"
#include <cstdint>
#include <vector>

enum class PlaylistCoverError : int32_t {
	None, FileRead, NotJpeg, TooLarge, UploadFailed, RefreshFailed
};
struct PlaylistCoverCommand {
	int64_t requestId = 0;
	std::string playlistId;
};
struct PlaylistCoverResult {
	PlaylistCoverCommand command;
	PlaylistCoverError error = PlaylistCoverError::UploadFailed;
	int32_t status = -1;
	std::string coverUrl;
};

// Payload limit includes Base64 expansion, preserving the existing 256*1024 cap.
// Spotify upload-custom-playlist-cover documentation verified 2026-09-16.
constexpr size_t kPlaylistCoverPayloadLimit = 256 * 1024;
PlaylistCoverError EncodePlaylistCover(const std::vector<uint8_t>& bytes,
	std::string& encoded);

class PlaylistCoverController {
public:
	bool Begin(const std::string& playlistId, bool owned, PlaylistCoverCommand& command);
	bool Pending() const { return fPending.requestId > 0; }
	bool Complete(const PlaylistCoverResult& result);
private:
	int64_t fNextRequest = 0;
	PlaylistCoverCommand fPending;
};

using PlaylistCoverUpload = std::function<void(const std::string&, const std::string&, JsonCallback)>;
using PlaylistCoverRead = std::function<void(const std::string&, JsonCallback)>;
using PlaylistCoverCompletion = std::function<void(const PlaylistCoverResult&)>;
// File preparation failures do not write. RefreshFailed means the upload succeeded.
bool DispatchPlaylistCover(const PlaylistCoverCommand& command,
	const std::vector<uint8_t>& bytes, const PlaylistCoverUpload& upload,
	const PlaylistCoverRead& read, PlaylistCoverCompletion complete);
