#include "LibrespotTransferController.h"
#include "spotify/api/PlaybackApi.h"

#include <limits>

namespace {
int32_t
ResponseInteger(const nlohmann::json& data, const char* field)
{
	if (!data.is_object())
		return -1;
	auto value = data.find(field);
	if (value == data.end() || !value->is_number_integer())
		return -1;
	if (value->is_number_unsigned()) {
		auto number = value->get<uint64_t>();
		return number <= uint64_t(std::numeric_limits<int32_t>::max())
			? static_cast<int32_t>(number) : -1;
	}
	auto number = value->get<int64_t>();
	return number >= 0 && number <= std::numeric_limits<int32_t>::max()
		? static_cast<int32_t>(number) : -1;
}

bool
OptionalString(const nlohmann::json& data, const char* field, std::string& value)
{
	auto item = data.find(field);
	if (item == data.end() || item->is_null())
		return true;
	if (!item->is_string())
		return false;
	value = item->get<std::string>();
	return true;
}

bool
FindDevice(const nlohmann::json& data, LibrespotTransferResult& result)
{
	if (!data.is_object())
		return false;
	auto devices = data.find("devices");
	if (devices == data.end() || !devices->is_array())
		return false;
	for (const auto& device : *devices) {
		if (!device.is_object())
			continue;
		std::string name;
		if (!OptionalString(device, "name", name))
			return false;
		// Preserve first exact-name match, even when its nullable ID is empty.
		if (name == result.request.deviceName)
			return OptionalString(device, "id", result.foundDeviceId);
	}
	return true;
}

bool
InspectPlayback(const nlohmann::json& data, LibrespotTransferResult& result)
{
	if (!data.is_object())
		return false;
	// The request client represents an empty successful GET (204) as {}.
	// Preserve missing-field defaults, but never treat wrong types as idle.
	auto playing = data.find("is_playing");
	if (playing != data.end() && !playing->is_boolean())
		return false;
	bool isPlaying = playing != data.end() && playing->get<bool>();
	std::string activeId;
	auto device = data.find("device");
	if (device != data.end() && !device->is_null()) {
		if (!device->is_object() || !OptionalString(*device, "id", activeId))
			return false;
	}
	result.shouldTransfer = !isPlaying || activeId == result.request.deviceId;
	return true;
}

LibrespotTransferResult
MapResult(const LibrespotTransferRequest& request, bool ok,
	const nlohmann::json& data)
{
	LibrespotTransferResult result;
	result.request = request;
	result.ok = ok;
	result.status = ResponseInteger(data, "status");
	result.retryAfter = ResponseInteger(data, "retry_after");
	if (!ok)
		return result;
	if (request.step == LibrespotTransferStep::FindDevice)
		result.responseValid = FindDevice(data, result);
	else if (request.step == LibrespotTransferStep::InspectPlayback)
		result.responseValid = InspectPlayback(data, result);
	return result;
}

bool
SameRequest(const LibrespotTransferRequest& a, const LibrespotTransferRequest& b)
{
	return a.generation == b.generation && a.sequence == b.sequence
		&& a.step == b.step && a.deviceName == b.deviceName && a.deviceId == b.deviceId;
}
}

bool
ValidLibrespotTransferRequest(const LibrespotTransferRequest& request)
{
	if (request.generation <= 0 || request.sequence <= 0)
		return false;
	switch (request.step) {
		case LibrespotTransferStep::FindDevice:
			return !request.deviceName.empty() && request.deviceId.empty();
		case LibrespotTransferStep::InspectPlayback:
		case LibrespotTransferStep::Transfer:
			return !request.deviceId.empty() && request.deviceName.empty();
		default:
			return false;
	}
}

bool
DispatchLibrespotTransfer(PlaybackApi& api, const LibrespotTransferRequest& request,
	std::function<void(const LibrespotTransferResult&)> complete)
{
	if (!ValidLibrespotTransferRequest(request) || !complete)
		return false;
	auto done = [request, complete](bool ok, const nlohmann::json& data) {
		complete(MapResult(request, ok, data));
	};
	switch (request.step) {
		case LibrespotTransferStep::FindDevice:
			api.GetDevices(done);
			break;
		case LibrespotTransferStep::InspectPlayback:
			api.GetPlaybackState(done);
			break;
		case LibrespotTransferStep::Transfer:
			api.TransferPlayback(request.deviceId, done);
			break;
		default:
			return false;
	}
	return true;
}

void
LibrespotTransferController::Begin(int64_t generation, LibrespotTransferMode mode,
	bool registeringOAuth)
{
	fReadiness.Begin(generation);
	fMode = mode;
	fRegisteringOAuth = registeringOAuth;
	fAttempts = 0;
	fSequence = 0;
	fPending = {};
	fPolling = generation > 0;
}

void
LibrespotTransferController::ResetAttempts()
{
	fAttempts = 0;
	if (fPending.step == LibrespotTransferStep::None)
		fPolling = fReadiness.Generation() > 0 && !fReadiness.Ready();
}

LibrespotTransferRequest
LibrespotTransferController::_Request(LibrespotTransferStep step,
	const std::string& deviceName, const std::string& deviceId)
{
	fPending = {fReadiness.Generation(), ++fSequence, step, deviceName, deviceId};
	return fPending;
}

LibrespotTransferRequest
LibrespotTransferController::Poll(const std::string& deviceName)
{
	if (!fPolling || fPending.step != LibrespotTransferStep::None || deviceName.empty())
		return {};
	++fAttempts;
	return _Request(LibrespotTransferStep::FindDevice, deviceName, "");
}

void
LibrespotTransferController::_ApplyDevice(const LibrespotTransferResult& result,
	LibrespotTransferUpdate& update)
{
	if (!result.ok || !result.responseValid || result.foundDeviceId.empty()) {
		// Compatibility: failed discovery consumes the same bounded retries as
		// an absent device. The result still retains failure/status provenance.
		fPolling = fAttempts < (fRegisteringOAuth ? 150 : 5);
		if (fPolling)
			update.retryDelayUs = fRegisteringOAuth ? 2000000LL : 1000000LL;
		return;
	}
	fPolling = false;
	update.finishOAuth = fRegisteringOAuth;
	fRegisteringOAuth = false;
	auto step = fMode == kLibrespotTransferAlways
		? LibrespotTransferStep::Transfer : LibrespotTransferStep::InspectPlayback;
	update.next = _Request(step, "", result.foundDeviceId);
}

LibrespotTransferUpdate
LibrespotTransferController::Apply(const LibrespotTransferResult& result)
{
	LibrespotTransferUpdate update;
	if (!fReadiness.Accepts(result.request.generation)
			|| !ValidLibrespotTransferRequest(result.request)
			|| !SameRequest(result.request, fPending))
		return update;
	update.accepted = true;
	fPending = {};
	if (result.request.step == LibrespotTransferStep::FindDevice) {
		_ApplyDevice(result, update);
		return update;
	}
	if (!result.ok || !result.responseValid)
		return update;
	if (result.request.step == LibrespotTransferStep::InspectPlayback) {
		if (result.shouldTransfer)
			update.next = _Request(LibrespotTransferStep::Transfer, "", result.request.deviceId);
		return update;
	}
	update.ready = fReadiness.Complete(result.request.generation);
	return update;
}
