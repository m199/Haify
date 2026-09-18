#pragma once

#include <Locker.h>

#include <functional>
#include <cstdint>
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
    void RefreshForSession(bool authenticated, AudiobookMode mode,
                           AudiobookCapabilityCallback callback, bool force = false);

private:
    void _StartAudiobookProbe(SpotifyApi* api, uint64_t generation);
    bool _ProbeIsCurrent(uint64_t generation) const;
    void _FinishAudiobookProbe(AudiobookCapabilityState state, uint64_t generation);
    static AudiobookCapabilityState _FailureState(
        const nlohmann::json& data);

    SpotifyApi* fApi = nullptr;
    mutable BLocker fLock;
    AudiobookMode fAudiobookMode = kAudiobookAuto;
    AudiobookCapabilityState fAudiobookState = kAudiobookUnknown;
    bool fAudiobookWasAvailable = false;
    bool fAudiobookProbeInFlight = false;
    uint64_t fProbeGeneration = 0;
    time_t fLastAudiobookCheck = 0;
    std::vector<AudiobookCapabilityCallback> fAudiobookWaiters;
};
