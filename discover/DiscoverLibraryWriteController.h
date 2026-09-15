#pragma once

#include "DiscoverLibraryChangeController.h"

#include <deque>

enum class DiscoverLibraryWriteKind { EnsureSaved, Save, Remove };

struct DiscoverLibraryWriteRequest {
	std::string uri;
	DiscoverLibraryWriteKind kind = DiscoverLibraryWriteKind::EnsureSaved;
	int32_t generation = 0;
	bool selectTarget = false;
};

struct DiscoverLibraryWritePlan {
	bool accepted = false;
	bool failed = false;
	bool selectTarget = false;
	std::optional<DiscoverLibraryChange> confirmed;
	std::optional<DiscoverLibraryWriteRequest> dispatch;
};

// Serialize writes to each URI. Membership checks are a separate stage: only
// the window, after accepting the account context, may dispatch the save.
class DiscoverLibraryWriteController {
public:
	void Reset() { fPending.clear(); }
	DiscoverLibraryWritePlan Queue(const std::string& uri, DiscoverLibraryWriteKind kind,
		bool selectTarget, bool audiobooksEnabled)
	{
		auto itemKind = SpotifyItemKindForUri(uri);
		if (SpotifyItemIdForUri(uri).empty() || itemKind == kSpotifyItemUnknown
				|| itemKind == kSpotifyItemPlaylist
				|| (itemKind == kSpotifyItemAudiobook && !audiobooksEnabled))
			return {};
		auto& queue = fPending[uri];
		queue.push_back({uri, kind, ++fNextGeneration, selectTarget});
		DiscoverLibraryWritePlan plan;
		plan.accepted = true;
		if (queue.size() == 1)
			plan.dispatch = queue.front();
		return plan;
	}
	DiscoverLibraryWritePlan Complete(const DiscoverLibraryWriteRequest& request,
		bool ok, bool saved)
	{
		auto found = fPending.find(request.uri);
		if (found == fPending.end() || found->second.front().generation != request.generation
				|| found->second.front().kind != request.kind)
			return {};
		auto& queue = found->second;
		DiscoverLibraryWritePlan plan;
		plan.accepted = true;
		if (ok && request.kind == DiscoverLibraryWriteKind::EnsureSaved && !saved) {
			queue.front().kind = DiscoverLibraryWriteKind::Save;
			plan.dispatch = queue.front();
			return plan;
		}
		plan.failed = !ok;
		plan.selectTarget = ok && queue.front().selectTarget;
		if (ok) {
			auto operation = request.kind == DiscoverLibraryWriteKind::Remove
				? DiscoverChangeOperation::Remove : DiscoverChangeOperation::Add;
			plan.confirmed = DiscoverLibraryChange{operation, request.uri};
		}
		queue.pop_front();
		if (queue.empty())
			fPending.erase(found);
		else
			plan.dispatch = queue.front();
		return plan;
	}

private:
	int32_t fNextGeneration = 0;
	std::map<std::string, std::deque<DiscoverLibraryWriteRequest>> fPending;
};
