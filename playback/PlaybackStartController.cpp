#include "PlaybackStartController.h"
#include "PlaybackStartPolicy.h"
#include "spotify/api/PlaybackApi.h"

#include <limits>

static int
_ResponseInteger(const nlohmann::json& data, const char* field)
{
	if (!data.is_object())
		return -1;
	auto value = data.find(field);
	if (value == data.end() || !value->is_number_integer()
			|| *value < 0 || *value > std::numeric_limits<int>::max())
		return -1;
	return value->get<int>();
}

static PlaybackStartStep
_Step(bool ok, const nlohmann::json& data)
{
	return {true, ok, _ResponseInteger(data, "status"),
		_ResponseInteger(data, "retry_after")};
}

static void
_Complete(const PlaybackStartCompletion& complete, const PlaybackStartResult& result)
{
	if (complete)
		complete(result);
}

static void
_Play(PlaybackApi& api, const PlaybackCommand& command, JsonCallback complete)
{
	if (!SpotifyItemIsPlayable(SpotifyItemKindForUri(command.uri))) {
		api.PlayContext(command.uri, complete, command.deviceId);
		return;
	}
	if (!PlaybackTargetsAudiobookQueue(command) && !command.nextQueueUris.empty()) {
		// Compatibility: the URI batch has no context offset or start position.
		api.PlayUris(PlaybackStartUriBatch(command), complete, command.deviceId);
		return;
	}
	api.PlayTrack(command.uri, command.contextUri, complete,
		command.startPositionMs, command.deviceId);
}

static void
_PlayAndRestoreShuffle(PlaybackApi& api, const PlaybackCommand& command,
	PlaybackStartResult result, const PlaybackStartCompletion& complete)
{
	_Play(api, command, [&api, result, complete](bool ok,
			const nlohmann::json& data) mutable {
		result.play = _Step(ok, data);
		// Once shuffle was disabled, restore it even when play failed.
		api.SetShuffle(true, [result, complete](bool restored,
				const nlohmann::json& response) mutable {
			result.restoreShuffle = _Step(restored, response);
			_Complete(complete, result);
		}, result.deviceId);
	});
}

bool
DispatchPlaybackStart(PlaybackApi& api, const PlaybackCommand& command,
	bool shuffleOn, PlaybackStartCompletion complete)
{
	if (!ValidPlaybackCommand(command))
		return false;
	PlaybackStartResult result;
	result.uri = command.uri;
	result.deviceId = command.deviceId;
	if (!PlaybackStartCyclesShuffle(command, shuffleOn)) {
		_Play(api, command, [result, complete](bool ok,
				const nlohmann::json& data) mutable {
			result.play = _Step(ok, data);
			_Complete(complete, result);
		});
		return true;
	}
	api.SetShuffle(false, [&api, command, result, complete](bool ok,
			const nlohmann::json& data) mutable {
		result.disableShuffle = _Step(ok, data);
		if (ok) {
			_PlayAndRestoreShuffle(api, command, result, complete);
			return;
		}
		// Legacy compatibility exception: every failed shuffle-off request tries
		// only the selected item, losing context/queue/position. Keep both outcomes
		// and explicit provenance; this is not an equivalent successful start.
		// Parity/failure traces: PlaybackStartControllerTest; docs/phase-5-verification.md.
		result.usedSingleItemFallback = true;
		api.PlayUris({command.uri}, [result, complete](bool played,
				const nlohmann::json& response) mutable {
			result.play = _Step(played, response);
			_Complete(complete, result);
		}, command.deviceId);
	}, command.deviceId);
	return true;
}
