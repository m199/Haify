#pragma once

#include "spotify/api/SpotifyApiTypes.h"

#include <cstdint>

enum class PlaylistMetadataKind { Unsupported, Playlist, Album, Podcast, CurrentUser };

struct PlaylistMetadataRequest {
	PlaylistMetadataKind kind = PlaylistMetadataKind::Unsupported;
	std::string id = "";
};

struct PlaylistMetadataResult {
	PlaylistMetadataRequest request;
	bool ok = false;
	bool responseValid = true;
	int32_t status = -1;
	int32_t retryAfter = -1;
	std::string title;
	std::string coverUrl;
	std::string snapshotId;
	std::string description;
	std::string ownerId;
	bool isPublic = false;
	int32_t total = -1;
	std::string userId;
	std::string legacyUserId;
};

// Dispatches one read. Completion owns its request; it never captures this.
class PlaylistMetadataController {
public:
	using Getter = std::function<void(const std::string&, JsonCallback)>;
	using ProfileGetter = std::function<void(JsonCallback)>;
	using Completion = std::function<void(const PlaylistMetadataResult&)>;

	PlaylistMetadataController(Getter playlist, Getter album, Getter podcast,
		ProfileGetter profile);
	bool Load(const PlaylistMetadataRequest& request, Completion done) const;

private:
	Getter fPlaylist;
	Getter fAlbum;
	Getter fPodcast;
	ProfileGetter fProfile;
};

// Owned by the window's event thread, independently of callback order.
// Failures preserve confirmed details. Snapshot/paging state is separate.
class PlaylistMetadataState {
public:
	bool Apply(const PlaylistMetadataResult& result);
	void UpdateDetails(const std::string& description, bool isPublic);
	bool IsOwned() const;
	const std::string& Description() const { return fDescription; }
	bool IsPublic() const { return fPublic; }

private:
	std::string fDescription;
	std::string fOwnerId;
	std::string fUserId;
	std::string fLegacyUserId;
	bool fPublic = false;
};
