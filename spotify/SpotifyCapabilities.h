#pragma once

#include <Locker.h>

#include <functional>
#include <nlohmann/json.hpp>
#include <time.h>
#include <vector>

#include "SettingsController.h"

class SpotifyApi;

enum AudiobookCapabilityState {
    kAudiobookUnknown = 0,
    kAudiobookAvailable,
    kAudiobookUnavailable,
    kAudiobookForbidden,
    kAudiobookTemporaryError
};

using AudiobookCapabilityCallback
    = std::function<void(AudiobookCapabilityState state)>;

class SpotifyCapabilities {
public:
    explicit SpotifyCapabilities(SpotifyApi* api = nullptr);

    void SetApi(SpotifyApi* api);
    void Reset();
    void SetAudiobookMode(AudiobookMode mode);

    AudiobookMode AudiobookModeSetting() const;
    AudiobookCapabilityState AudiobookState() const;
    bool AudiobooksEnabled() const;
    bool ProbeInFlight() const;
    time_t LastAudiobookCheck() const;

    void ProbeAudiobooks(AudiobookCapabilityCallback callback = nullptr,
                         bool force = false);

private:
    void _FinishAudiobookProbe(AudiobookCapabilityState state);
    static AudiobookCapabilityState _FailureState(
        const nlohmann::json& data);

    SpotifyApi* fApi = nullptr;
    mutable BLocker fLock;
    AudiobookMode fAudiobookMode = kAudiobookAuto;
    AudiobookCapabilityState fAudiobookState = kAudiobookUnknown;
    bool fAudiobookWasAvailable = false;
    bool fAudiobookProbeInFlight = false;
    time_t fLastAudiobookCheck = 0;
    std::vector<AudiobookCapabilityCallback> fAudiobookWaiters;
};
