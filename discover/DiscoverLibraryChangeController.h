#pragma once

#include "DiscoverChanges.h"
#include "DiscoverTabPolicy.h"

#include <map>
#include <optional>
#include <set>

struct DiscoverLibraryChange {
	DiscoverChangeOperation operation = DiscoverChangeOperation::Invalid;
	std::string uri;
};

struct DiscoverLibraryRequest {
	int32_t tab = TAB_NONE;
	std::string uri;
	int32_t generation = 0;
};

struct DiscoverLibraryChangePlan {
	DiscoverLibraryRequest request;
	bool removeRow = false;
	bool resolveAddition = false;
	bool refreshPodcasts = false;
	bool invalidatePodcasts = false;
	bool removePodcastDuplicates = false;
	bool saveCache = false;
};

// One window owns this controller on its message loop. Requests carry copies
// of identity + generation. Reset invalidates all requests without reusing IDs.
class DiscoverLibraryChangeController {
public:
	static DiscoverTab TargetTab(const std::string& uri);
	void Reset();
	DiscoverLibraryChangePlan ApplyChange(const DiscoverLibraryChange& change,
		bool loaded, bool rowExists, bool podcastsLoaded);
	bool AcceptsAddition(const DiscoverLibraryRequest& request) const;
	void CompleteAddition(const DiscoverLibraryRequest& request);

	void ObserveMembership(const DiscoverLibraryChange& change);
	DiscoverLibraryRequest BeginMembership(const std::string& uri);
	bool AcceptMembership(const DiscoverLibraryRequest& request, bool ok, bool saved);
	std::optional<bool> KnownMembership(const std::string& uri) const;

	const std::set<std::string>& AudiobookIds() const { return fAudiobookIds; }
	bool AudiobookIdsKnown() const { return fAudiobookIdsKnown; }
	void SetAudiobookSnapshot(std::set<std::string> ids);
	bool AcceptPrimaryRow(int32_t tab, const std::string& uri);
	bool IsAudiobook(const std::string& id) const;

private:
	int32_t fNextGeneration = 0;
	std::map<std::string, int32_t> fAdditions;
	std::map<std::string, int32_t> fMembershipRequests;
	std::map<std::string, bool> fMembership;
	std::set<std::string> fAudiobookIds;
	bool fAudiobookIdsKnown = false;
};
