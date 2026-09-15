#include "DiscoverPlaylistMutationController.h"
#include "spotify/SpotifyUri.h"

#include <set>
#include <utility>

namespace {
bool
HasPlaylistColumns(const DiscoverRowData& row)
{
	return row.vals.size() == 2 && row.uris.size() == 2 && row.ttls.size() == 2;
}

DiscoverRowData
ChangedRow(const DiscoverPlaylistChange& change, const std::optional<DiscoverRowData>& existing)
{
	DiscoverRowData row = existing.value_or(DiscoverRowData{
		{"", "Spotify"}, {change.uri, ""}, {"", ""}});
	if (!change.name.empty()) {
		row.vals[0] = change.name;
		row.ttls[0] = change.name;
	}
	if (change.operation == DiscoverChangeOperation::Add) {
		if (!change.owner.empty())
			row.vals[1] = change.owner;
		row.writable = change.writable;
		row.owned = change.owned.value_or(row.owned);
	}
	return row;
}

bool
ValidPlaylistRow(const DiscoverRowData& row)
{
	return HasPlaylistColumns(row)
		&& SpotifyItemKindForUri(row.uris[0]) == kSpotifyItemPlaylist
		&& !SpotifyItemIdForUri(row.uris[0]).empty();
}
}

void
DiscoverPlaylistMutationController::Reset()
{
	InvalidateSnapshot();
	fMutations.clear();
}

std::optional<DiscoverRowData>
DiscoverPlaylistMutationController::_ProjectedRow(const MutationState& state)
{
	auto row = state.confirmed;
	for (const auto& command : state.queue) {
		if (command.operation == DiscoverChangeOperation::Remove)
			row.reset();
		else if (row) {
			row->vals[0] = command.name;
			row->ttls[0] = command.name;
		}
	}
	return row;
}

DiscoverPlaylistMutationPlan
DiscoverPlaylistMutationController::_Plan(const MutationState& state)
{
	DiscoverPlaylistMutationPlan plan;
	plan.accepted = true;
	plan.row = _ProjectedRow(state);
	plan.pending = !state.queue.empty();
	if (plan.pending)
		plan.dispatch = state.queue.front();
	return plan;
}

DiscoverPlaylistMutationPlan
DiscoverPlaylistMutationController::QueueMutation(DiscoverChangeOperation operation,
	const std::string& id, const std::string& name,
	const std::optional<DiscoverRowData>& existing)
{
	if (id.empty() || (operation != DiscoverChangeOperation::Rename
			&& operation != DiscoverChangeOperation::Remove))
		return {};
	if (operation == DiscoverChangeOperation::Rename && name.empty())
		return {};
	if (existing && !HasPlaylistColumns(*existing))
		return {};
	auto found = fMutations.find(id);
	if (found != fMutations.end() && found->second.queue.back().operation == DiscoverChangeOperation::Remove)
		return {};
	if (found == fMutations.end())
		found = fMutations.emplace(id, MutationState{existing, {}}).first;
	auto& state = found->second;
	state.queue.push_back({operation, id, name, ++fNextMutation});
	InvalidateSnapshot();
	auto plan = _Plan(state);
	if (state.queue.size() > 1)
		plan.dispatch.reset();
	return plan;
}

DiscoverPlaylistMutationPlan
DiscoverPlaylistMutationController::CompleteMutation(const std::string& id,
	int32_t generation, bool success)
{
	auto found = fMutations.find(id);
	if (found == fMutations.end() || found->second.queue.front().generation != generation)
		return {};
	auto& state = found->second;
	auto command = state.queue.front();
	if (success) {
		if (command.operation == DiscoverChangeOperation::Remove)
			state.confirmed.reset();
		else if (state.confirmed) {
			state.confirmed->vals[0] = command.name;
			state.confirmed->ttls[0] = command.name;
		}
	}
	state.queue.pop_front();
	auto plan = _Plan(state);
	plan.failed = !success;
	if (success)
		plan.confirmed = DiscoverPlaylistChange{command.operation, "spotify:playlist:" + id, command.name};
	if (!plan.pending)
		fMutations.erase(found);
	InvalidateSnapshot();
	return plan;
}

DiscoverPlaylistRowPlan
DiscoverPlaylistMutationController::PlanChange(const DiscoverPlaylistChange& change,
	const std::optional<DiscoverRowData>& existing)
{
	std::string id = SpotifyItemIdForUri(change.uri);
	if (id.empty() || SpotifyItemKindForUri(change.uri) != kSpotifyItemPlaylist)
		return {};
	if (existing && !HasPlaylistColumns(*existing))
		return {};
	if (change.operation == DiscoverChangeOperation::Remove) {
		fMutations.erase(id);
		return {DiscoverPlaylistRowAction::Remove, {}};
	}
	auto pending = fMutations.find(id);
	auto baseline = pending != fMutations.end() ? pending->second.confirmed : existing;
	if (change.operation == DiscoverChangeOperation::Rename && !baseline)
		return {DiscoverPlaylistRowAction::Reload, {}};
	if (change.operation != DiscoverChangeOperation::Rename
			&& (change.operation != DiscoverChangeOperation::Add || change.name.empty()))
		return {};
	DiscoverRowData row = ChangedRow(change, baseline);
	if (pending != fMutations.end()) {
		pending->second.confirmed = row;
		auto projected = _ProjectedRow(pending->second);
		if (!projected)
			return {};
		row = std::move(*projected);
	}
	return {DiscoverPlaylistRowAction::Upsert, std::move(row)};
}

DiscoverPlaylistSnapshotPlan
DiscoverPlaylistMutationController::PlanSnapshot(const std::vector<DiscoverRowData>& current,
	const std::vector<DiscoverRowData>& server) const
{
	DiscoverPlaylistSnapshotPlan plan;
	for (const auto& row : current) {
		if (!row.uris.empty() && row.uris[0] == "spotify:collection") {
			plan.order.push_back(row.uris[0]);
			break;
		}
	}
	std::set<std::string> serverUris;
	for (const auto& row : server) {
		if (!ValidPlaylistRow(row))
			continue;
		const auto& uri = row.uris[0];
		if (!serverUris.insert(uri).second)
			continue;
		plan.order.push_back(uri);
		std::string id = SpotifyItemIdForUri(uri);
		if (fMutations.count(id))
			continue;
		plan.upserts.push_back(row);
	}
	for (const auto& row : current) {
		if (row.uris.empty() || row.uris[0] == "spotify:collection")
			continue;
		const auto& uri = row.uris[0];
		if (!serverUris.count(uri) && !fMutations.count(SpotifyItemIdForUri(uri)))
			plan.removals.push_back(uri);
	}
	return plan;
}
