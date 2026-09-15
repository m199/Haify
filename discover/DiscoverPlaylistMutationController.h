#pragma once

#include "DiscoverChanges.h"
#include "DiscoverRowData.h"

#include <cstdint>
#include <map>
#include <optional>
#include <deque>

struct DiscoverPlaylistChange {
	DiscoverChangeOperation operation = DiscoverChangeOperation::Invalid;
	std::string uri;
	std::string name = "";
	std::string owner = "";
	bool writable = true;
	std::optional<bool> owned = std::nullopt;
};

enum class DiscoverPlaylistRowAction { Ignore, Remove, Upsert, Reload };

struct DiscoverPlaylistRowPlan {
	DiscoverPlaylistRowAction action = DiscoverPlaylistRowAction::Ignore;
	DiscoverRowData row;
};

struct DiscoverPlaylistSnapshotPlan {
	std::vector<DiscoverRowData> upserts;
	std::vector<std::string> removals;
	std::vector<std::string> order;
};

struct DiscoverPlaylistMutationRequest {
	DiscoverChangeOperation operation = DiscoverChangeOperation::Invalid;
	std::string id;
	std::string name;
	int32_t generation = 0;
};

struct DiscoverPlaylistMutationPlan {
	bool accepted = false;
	bool pending = false;
	bool failed = false;
	std::optional<DiscoverRowData> row;
	std::optional<DiscoverPlaylistMutationRequest> dispatch;
	std::optional<DiscoverPlaylistChange> confirmed;
};

// Owns mutation tokens and snapshot acceptance, never BRows. The window owns
// detached rows + selection for optimistic deletion and renders these plans.
class DiscoverPlaylistMutationController {
public:
	void Reset();
	int32_t BeginSnapshot() { return ++fSnapshotGeneration; }
	void InvalidateSnapshot() { ++fSnapshotGeneration; }
	bool AcceptsSnapshot(int32_t generation) const { return generation == fSnapshotGeneration; }
	DiscoverPlaylistMutationPlan QueueMutation(DiscoverChangeOperation operation,
		const std::string& id, const std::string& name,
		const std::optional<DiscoverRowData>& existing);
	DiscoverPlaylistMutationPlan CompleteMutation(const std::string& id,
		int32_t generation, bool success);
	// Persist only confirmed rows. The window schedules another save after
	// applying a result or rolling its optimistic change back.
	bool CanPersist() const { return fMutations.empty(); }
	DiscoverPlaylistRowPlan PlanChange(const DiscoverPlaylistChange& change,
		const std::optional<DiscoverRowData>& existing);
	DiscoverPlaylistSnapshotPlan PlanSnapshot(const std::vector<DiscoverRowData>& current,
		const std::vector<DiscoverRowData>& server) const;

private:
	struct MutationState {
		std::optional<DiscoverRowData> confirmed;
		std::deque<DiscoverPlaylistMutationRequest> queue;
	};
	static std::optional<DiscoverRowData> _ProjectedRow(const MutationState& state);
	static DiscoverPlaylistMutationPlan _Plan(const MutationState& state);
	int32_t fNextMutation = 0;
	int32_t fSnapshotGeneration = 0;
	std::map<std::string, MutationState> fMutations;
};
