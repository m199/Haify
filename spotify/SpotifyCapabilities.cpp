#include "SpotifyCapabilities.h"

#include "api/SpotifyApi.h"
#include "api/SpotifyResponse.h"

#include <Autolock.h>

static bool
IsMarketBlocked(const nlohmann::json& item)
{
    if (!item.is_object() || !item.contains("restrictions")
            || !item["restrictions"].is_object()) {
        return false;
    }
    const auto& restrictions = item["restrictions"];
    auto reason = restrictions.find("reason");
    return reason != restrictions.end() && reason->is_string() && *reason == "market";
}

static bool
AllItemsMarketBlocked(const nlohmann::json& items)
{
    if (!items.is_array() || items.empty())
        return false;
    for (const auto& item : items) {
        if (!IsMarketBlocked(item))
            return false;
    }
    return true;
}

static bool
SavedAudiobooksProbeState(const nlohmann::json& data,
    AudiobookCapabilityState& state)
{
    if (!data.contains("items") || !data["items"].is_array()
            || data["items"].empty()) {
        return false;
    }

    state = AllItemsMarketBlocked(data["items"])
        ? kAudiobookUnavailable : kAudiobookAvailable;
    return true;
}

static bool
SearchAudiobooksAvailable(const nlohmann::json& searchData)
{
    if (!searchData.contains("audiobooks")
            || !searchData["audiobooks"].is_object()) {
        return false;
    }

    const auto& books = searchData["audiobooks"];
    bool available = (books.contains("total") && books["total"].is_number_integer()
            && books["total"] > 0)
        || (books.contains("items") && books["items"].is_array()
            && !books["items"].empty());
    if (books.contains("items") && AllItemsMarketBlocked(books["items"]))
        return false;
    return available;
}

SpotifyCapabilities::SpotifyCapabilities(SpotifyApi* api)
    : fApi(api), fLock("Spotify capabilities")
{
}

void SpotifyCapabilities::SetApi(SpotifyApi* api)
{
    {
        BAutolock lock(&fLock);
        if (fApi == api) return;
        fApi = api;
    }
    Reset();
}

void SpotifyCapabilities::Reset()
{
    std::vector<AudiobookCapabilityCallback> waiters;
    {
        BAutolock lock(&fLock);
        fAudiobookState = kAudiobookUnknown;
        ++fProbeGeneration;
        fAudiobookWasAvailable = false;
        fAudiobookProbeInFlight = false;
        fLastAudiobookCheck = 0;
        waiters.swap(fAudiobookWaiters);
    }
    for (const auto& callback : waiters) {
        if (callback) callback(kAudiobookUnknown);
    }
}

void SpotifyCapabilities::SetAudiobookMode(AudiobookMode mode)
{
    if (mode < kAudiobookAuto || mode > kAudiobookDisabled)
        mode = kAudiobookAuto;
    std::vector<AudiobookCapabilityCallback> waiters;
    {
        BAutolock lock(&fLock);
        if (fAudiobookMode == mode) return;
        fAudiobookMode = mode;
        ++fProbeGeneration;
        fAudiobookProbeInFlight = false;
        waiters.swap(fAudiobookWaiters);
    }
    for (const auto& callback : waiters) {
        if (callback) callback(kAudiobookUnknown);
    }
}

AudiobookMode SpotifyCapabilities::AudiobookModeSetting() const
{
    BAutolock lock(&fLock);
    return fAudiobookMode;
}

AudiobookCapabilityState SpotifyCapabilities::AudiobookState() const
{
    BAutolock lock(&fLock);
    return fAudiobookState;
}

bool SpotifyCapabilities::AudiobooksEnabled() const
{
    BAutolock lock(&fLock);
    if (fAudiobookMode == kAudiobookDisabled)
        return false;
    return fAudiobookMode == kAudiobookEnabled
        || fAudiobookState == kAudiobookAvailable
        || (fAudiobookState == kAudiobookTemporaryError
            && fAudiobookWasAvailable);
}

bool SpotifyCapabilities::ProbeInFlight() const
{
    BAutolock lock(&fLock);
    return fAudiobookProbeInFlight;
}

time_t SpotifyCapabilities::LastAudiobookCheck() const
{
    BAutolock lock(&fLock);
    return fLastAudiobookCheck;
}

AudiobookCapabilityState SpotifyCapabilities::_FailureState(
    const nlohmann::json& data)
{
    int status = SpotifyResponseStatus(data);
    if (status == 403)
        return kAudiobookForbidden;
    if (SpotifyResponseIsTemporaryFailure(data))
        return kAudiobookTemporaryError;
    return kAudiobookUnavailable;
}

void SpotifyCapabilities::ProbeAudiobooks(
    AudiobookCapabilityCallback callback, bool force)
{
    SpotifyApi* api = nullptr;
    AudiobookCapabilityState immediate = kAudiobookUnknown;
    uint64_t generation;
    {
        BAutolock lock(&fLock);
        if (callback)
            fAudiobookWaiters.push_back(callback);
        if (fAudiobookProbeInFlight && fAudiobookMode != kAudiobookDisabled)
            return;
        generation = ++fProbeGeneration;
        fAudiobookProbeInFlight = true;
        api = fApi;
        if (fAudiobookMode == kAudiobookDisabled)
            immediate = kAudiobookUnavailable;
        else if (!force && fAudiobookState != kAudiobookUnknown
                && fAudiobookState != kAudiobookTemporaryError)
            immediate = fAudiobookState;
        else if (!api)
            immediate = kAudiobookTemporaryError;
    }
    if (immediate != kAudiobookUnknown)
        _FinishAudiobookProbe(immediate, generation);
    else
        _StartAudiobookProbe(api, generation);
}

void SpotifyCapabilities::_StartAudiobookProbe(SpotifyApi* api, uint64_t generation)
{
    api->Library().GetSavedAudiobooks(0, 1,
        [this, api, generation](bool ok, const nlohmann::json& data) {
            if (!_ProbeIsCurrent(generation)) return;
            if (!ok) {
                _FinishAudiobookProbe(_FailureState(data), generation);
                return;
            }
            AudiobookCapabilityState savedState;
            if (SavedAudiobooksProbeState(data, savedState)) {
                _FinishAudiobookProbe(savedState, generation);
                return;
            }
            api->Content().Search("a", "audiobook",
                [this, generation](bool searchOk, const nlohmann::json& searchData) {
                    if (!searchOk) {
                        _FinishAudiobookProbe(_FailureState(searchData), generation);
                        return;
                    }
                    bool available = SearchAudiobooksAvailable(searchData);
                    _FinishAudiobookProbe(available
                        ? kAudiobookAvailable : kAudiobookUnavailable, generation);
                });
        });
}

void SpotifyCapabilities::_FinishAudiobookProbe(
    AudiobookCapabilityState state, uint64_t generation)
{
    std::vector<AudiobookCapabilityCallback> waiters;
    {
        BAutolock lock(&fLock);
        if (generation != fProbeGeneration || !fAudiobookProbeInFlight) return;
        fAudiobookState = state;
        if (state == kAudiobookAvailable)
            fAudiobookWasAvailable = true;
        else if (state == kAudiobookUnavailable
                || state == kAudiobookForbidden)
            fAudiobookWasAvailable = false;
        fAudiobookProbeInFlight = false;
        fLastAudiobookCheck = time(NULL);
        waiters.swap(fAudiobookWaiters);
    }
    for (const auto& callback : waiters) {
        if (callback) callback(state);
    }
}

bool SpotifyCapabilities::_ProbeIsCurrent(uint64_t generation) const
{
    BAutolock lock(&fLock);
    return generation == fProbeGeneration && fAudiobookProbeInFlight;
}

void SpotifyCapabilities::RefreshForSession(bool authenticated, AudiobookMode mode,
    AudiobookCapabilityCallback callback, bool force)
{
    SetAudiobookMode(mode);
    if (!authenticated) {
        Reset();
        if (callback) callback(kAudiobookUnknown);
        return;
    }
    ProbeAudiobooks(callback, force);
}
