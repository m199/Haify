#pragma once

#include <atomic>
#include <cstdint>

// App owns writes; the player reads readiness before consuming its pending
// command. Device discovery alone does not complete a playback transfer.
class LocalPlaybackReadiness {
public:
	void Begin(int64_t generation)
	{
		fReadyGeneration = 0;
		fGeneration = generation;
	}

	int64_t Generation() const { return fGeneration.load(); }

	bool Accepts(int64_t generation) const
	{
		return generation > 0 && generation == fGeneration.load();
	}

	bool Complete(int64_t generation)
	{
		if (!Accepts(generation))
			return false;
		fReadyGeneration = generation;
		return true;
	}

	bool Ready() const
	{
		int64_t generation = fGeneration.load();
		return generation > 0 && fReadyGeneration.load() == generation;
	}

	bool ReadyAfter(int64_t previousGeneration) const
	{
		return Generation() != previousGeneration && Ready();
	}

private:
	std::atomic<int64_t> fGeneration{0};
	std::atomic<int64_t> fReadyGeneration{0};
};
