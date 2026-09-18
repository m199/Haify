#pragma once

#include "LocalPlaybackReadiness.h"

#include <functional>
#include <string>

class PlaybackApi;

enum LibrespotTransferMode {
	kLibrespotTransferAlways,
	kLibrespotTransferIfIdle
};

enum class LibrespotTransferStep { None, FindDevice, InspectPlayback, Transfer };

struct LibrespotTransferRequest {
	int64_t generation = 0;
	int32_t sequence = 0;
	LibrespotTransferStep step = LibrespotTransferStep::None;
	std::string deviceName = "";
	std::string deviceId = "";
};

struct LibrespotTransferResult {
	LibrespotTransferRequest request;
	bool ok = false;
	bool responseValid = true;
	int32_t status = -1;
	int32_t retryAfter = -1;
	std::string foundDeviceId = "";
	bool shouldTransfer = false;
};

struct LibrespotTransferUpdate {
	bool accepted = false;
	bool finishOAuth = false;
	bool ready = false;
	int64_t retryDelayUs = 0;
	LibrespotTransferRequest next;
};

bool ValidLibrespotTransferRequest(const LibrespotTransferRequest& request);

// One request per dispatch. Completion owns its data, never controller/App
// pointers. The owner must accept the result before dispatching the next step.
bool DispatchLibrespotTransfer(PlaybackApi& api,
	const LibrespotTransferRequest& request,
	std::function<void(const LibrespotTransferResult&)> complete);

// App's looper owns this state and performs returned effects. Only Readiness()
// may be read by another thread. API callbacks publish results to the looper.
class LibrespotTransferController {
public:
	void Begin(int64_t generation,
		LibrespotTransferMode mode = kLibrespotTransferAlways,
		bool registeringOAuth = false);
	void ResetAttempts();
	LibrespotTransferRequest Poll(const std::string& deviceName);
	LibrespotTransferUpdate Apply(const LibrespotTransferResult& result);
	const LocalPlaybackReadiness& Readiness() const { return fReadiness; }

private:
	LibrespotTransferRequest _Request(LibrespotTransferStep step,
		const std::string& deviceName, const std::string& deviceId);
	void _ApplyDevice(const LibrespotTransferResult& result,
		LibrespotTransferUpdate& update);

	LocalPlaybackReadiness fReadiness;
	LibrespotTransferMode fMode = kLibrespotTransferAlways;
	LibrespotTransferRequest fPending;
	int32_t fSequence = 0;
	int32_t fAttempts = 0;
	bool fRegisteringOAuth = false;
	bool fPolling = false;
};
