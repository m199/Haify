#pragma once

#include "spotify/SpotifyUri.h"

#include <cstdint>
#include <string>

namespace LibrespotEventFields {
constexpr const char* SessionId = "session_id";
}

enum class LibrespotPositionAction { Refresh, ApplyPending, ApplyCurrent };

// The player looper owns this state. File snapshots are independent: a playing
// event must identify the same item as its metadata, even across process starts.
class LibrespotEventState {
public:
	bool SetSession(int64_t session)
	{
		if (session == fSession)
			return false;
		fSession = session;
		fTrackEvent.clear();
		fPlaybackEvent.clear();
		fTrackUri.clear();
		fTrackId.clear();
		return true;
	}

	bool Accept(const std::string& session, const std::string& eventId, bool track)
	{
		if (fSession <= 0 || session != std::to_string(fSession) || eventId.empty())
			return false;
		std::string& previous = track ? fTrackEvent : fPlaybackEvent;
		if (previous == eventId)
			return false;
		previous = eventId;
		return true;
	}

	bool RememberTrack(const std::string& uri, const std::string& trackId)
	{
		if (trackId.empty() || SpotifyItemIdForUri(uri).empty()
				|| !SpotifyItemIsPlayable(SpotifyItemKindForUri(uri)))
			return false;
		fTrackUri = uri;
		fTrackId = trackId;
		return true;
	}

	LibrespotPositionAction PositionAction(bool playing, const std::string& trackId,
		const std::string& currentUri, const std::string& pendingUri) const
	{
		// Compare the opaque IDs emitted by librespot; do not assume an ID encoding.
		if (trackId.empty() || trackId != fTrackId)
			return LibrespotPositionAction::Refresh;
		if (playing && !pendingUri.empty() && pendingUri == fTrackUri)
			return LibrespotPositionAction::ApplyPending;
		if (!currentUri.empty() && currentUri == fTrackUri)
			return LibrespotPositionAction::ApplyCurrent;
		return LibrespotPositionAction::Refresh;
	}

private:
	int64_t fSession = 0;
	std::string fTrackEvent;
	std::string fPlaybackEvent;
	std::string fTrackUri;
	std::string fTrackId;
};
