#pragma once

#include "spotify/SpotifyUri.h"

#include <cstdint>
#include <string>

// The player looper holds its existing display while transfer briefly restores
// the previous item. Only an observed playing target may finish this transition;
// a successful API request or optimistic metadata is not confirmation.
class LocalPlaybackPresentation {
public:
	void Begin(const std::string& uri, int64_t nowUs)
	{
		Cancel();
		if (!SpotifyItemIsPlayable(SpotifyItemKindForUri(uri))
				|| SpotifyItemIdForUri(uri).empty())
			return;
		fTargetUri = uri;
		fUntilUs = nowUs + 15000000;
	}

	void Dispatched(const std::string& uri, int64_t nowUs)
	{
		if (!Active(nowUs)) {
			Cancel();
			return;
		}
		Begin(uri, nowUs);
		fDispatched = true;
	}

	bool Active(int64_t nowUs) const
	{
		return !fTargetUri.empty() && nowUs < fUntilUs;
	}

	bool Defer(const std::string& uri, bool optimistic, bool isPlaying, int64_t nowUs)
	{
		if (!Active(nowUs)) {
			Cancel();
			return false;
		}
		if (!fDispatched || optimistic || uri != fTargetUri || !isPlaying)
			return true;
		Cancel();
		return false;
	}

	void Cancel()
	{
		fTargetUri.clear();
		fUntilUs = 0;
		fDispatched = false;
	}

private:
	std::string fTargetUri;
	int64_t fUntilUs = 0;
	bool fDispatched = false;
};
