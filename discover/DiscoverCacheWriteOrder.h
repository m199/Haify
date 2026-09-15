#pragma once

#include <cstdint>
#include <map>
#include <string>

// Repository serializes Begin/IsCurrent/Complete and the final rename under
// one lock. A stale worker cannot consume a newer request or publish its file.
class DiscoverCacheWriteOrder {
public:
	uint64_t Begin(const std::string& path)
	{
		uint64_t generation = ++fNext;
		fLatest[path] = generation;
		return generation;
	}
	bool IsCurrent(const std::string& path, uint64_t generation) const
	{
		auto found = fLatest.find(path);
		return found != fLatest.end() && found->second == generation;
	}
	void Complete(const std::string& path, uint64_t generation)
	{
		if (IsCurrent(path, generation))
			fLatest.erase(path);
	}
private:
	uint64_t fNext = 0;
	std::map<std::string, uint64_t> fLatest;
};
