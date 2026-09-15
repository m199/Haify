#include "DiscoverLibraryChangeController.h"

#include <utility>

DiscoverTab
DiscoverLibraryChangeController::TargetTab(const std::string& uri)
{
	SpotifyItemKind kind = SpotifyItemKindForUri(uri);
	if (kind == kSpotifyItemTrack || kind == kSpotifyItemPlaylist
			|| SpotifyItemIdForUri(uri).empty())
		return TAB_NONE;
	return DiscoverLibraryTargetTab(kind, true);
}

void
DiscoverLibraryChangeController::Reset()
{
	fAdditions.clear();
	fMembershipRequests.clear();
	fMembership.clear();
	fAudiobookIds.clear();
	fAudiobookIdsKnown = false;
}

DiscoverLibraryChangePlan
DiscoverLibraryChangeController::ApplyChange(const DiscoverLibraryChange& change,
	bool loaded, bool rowExists, bool podcastsLoaded)
{
	DiscoverLibraryChangePlan plan;
	DiscoverTab tab = TargetTab(change.uri);
	bool add = change.operation == DiscoverChangeOperation::Add;
	if (tab == TAB_NONE || (!add && change.operation != DiscoverChangeOperation::Remove))
		return plan;
	plan.request = {tab, change.uri, ++fNextGeneration};
	fAdditions.erase(change.uri);
	plan.removeRow = !add;
	plan.resolveAddition = add && loaded && !rowExists;
	if (plan.resolveAddition)
		fAdditions[change.uri] = plan.request.generation;
	if (tab == TAB_AUDIOBOOKS) {
		std::string id = SpotifyItemIdForUri(change.uri);
		fAudiobookIdsKnown = true;
		plan.saveCache = true;
		plan.invalidatePodcasts = true;
		plan.refreshPodcasts = podcastsLoaded;
		if (add) {
			fAudiobookIds.insert(id);
			plan.removePodcastDuplicates = true;
		} else {
			fAudiobookIds.erase(id);
		}
	}
	return plan;
}

bool
DiscoverLibraryChangeController::AcceptsAddition(const DiscoverLibraryRequest& request) const
{
	auto found = fAdditions.find(request.uri);
	return request.tab == TargetTab(request.uri) && found != fAdditions.end()
		&& found->second == request.generation;
}

void
DiscoverLibraryChangeController::CompleteAddition(const DiscoverLibraryRequest& request)
{
	if (AcceptsAddition(request))
		fAdditions.erase(request.uri);
}

void
DiscoverLibraryChangeController::ObserveMembership(const DiscoverLibraryChange& change)
{
	if (change.uri.empty() || (change.operation != DiscoverChangeOperation::Add
			&& change.operation != DiscoverChangeOperation::Remove))
		return;
	fMembership[change.uri] = change.operation == DiscoverChangeOperation::Add;
	fMembershipRequests.erase(change.uri);
}

DiscoverLibraryRequest
DiscoverLibraryChangeController::BeginMembership(const std::string& uri)
{
	int32_t generation = ++fNextGeneration;
	fMembershipRequests[uri] = generation;
	return {TargetTab(uri), uri, generation};
}

bool
DiscoverLibraryChangeController::AcceptMembership(const DiscoverLibraryRequest& request,
	bool ok, bool saved)
{
	auto found = fMembershipRequests.find(request.uri);
	if (found == fMembershipRequests.end() || found->second != request.generation)
		return false;
	fMembershipRequests.erase(found);
	if (!ok)
		return false;
	fMembership[request.uri] = saved;
	return true;
}

std::optional<bool>
DiscoverLibraryChangeController::KnownMembership(const std::string& uri) const
{
	auto found = fMembership.find(uri);
	return found == fMembership.end() ? std::nullopt : std::optional<bool>(found->second);
}

void
DiscoverLibraryChangeController::SetAudiobookSnapshot(std::set<std::string> ids)
{
	fAudiobookIds = std::move(ids);
	fAudiobookIdsKnown = true;
}

bool
DiscoverLibraryChangeController::IsAudiobook(const std::string& id) const
{
	return !id.empty() && fAudiobookIds.find(id) != fAudiobookIds.end();
}

bool
DiscoverLibraryChangeController::AcceptPrimaryRow(int32_t tab, const std::string& uri)
{
	if (!uri.empty() && !PrimaryUriMatchesTab(tab, uri))
		return false;
	std::string id = SpotifyItemIdForUri(uri);
	if (tab == TAB_PODCASTS && IsAudiobook(id))
		return false;
	if (tab == TAB_AUDIOBOOKS && !id.empty()) {
		fAudiobookIds.insert(id);
		fAudiobookIdsKnown = true;
	}
	return true;
}
