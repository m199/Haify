#include "SpotifyCapabilities.h"

#include "api/SpotifyApi.h"

#include <Autolock.h>

static bool
IsMarketBlocked(const nlohmann::json& item)
{
    if (!item.is_object() || !item.contains("restrictions")
            || !item["restrictions"].is_object()) {
        return false;
    }
    return item["restrictions"].value("reason", "") == "market";
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

SpotifyCapabilities::SpotifyCapabilities(SpotifyApi* api)
    : fApi(api), fLock("Spotify capabilities")
{
}

void SpotifyCapabilities::SetApi(SpotifyApi* api)
{
    BAutolock lock(&fLock);
    fApi = api;
}

void SpotifyCapabilities::Reset()
{
    std::vector<AudiobookCapabilityCallback> waiters;
    {
        BAutolock lock(&fLock);
        fAudiobookState = kAudiobookUnknown;
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
    BAutolock lock(&fLock);
    fAudiobookMode = mode;
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
    int status = SpotifyApi::ResponseStatus(data);
    if (status == 403)
        return kAudiobookForbidden;
    if (SpotifyApi::IsTemporaryFailure(data))
        return kAudiobookTemporaryError;
    return kAudiobookUnavailable;
}

void SpotifyCapabilities::ProbeAudiobooks(
    AudiobookCapabilityCallback callback, bool force)
{
    SpotifyApi* api = nullptr;
    AudiobookCapabilityState current;
    {
        BAutolock lock(&fLock);
        if (callback)
            fAudiobookWaiters.push_back(callback);
        if (fAudiobookMode == kAudiobookDisabled) {
            lock.Unlock();
            _FinishAudiobookProbe(kAudiobookUnavailable);
            return;
        }
        if (fAudiobookProbeInFlight)
            return;
        current = fAudiobookState;
        if (!force && current != kAudiobookUnknown
                && current != kAudiobookTemporaryError) {
            lock.Unlock();
            _FinishAudiobookProbe(current);
            return;
        }
        api = fApi;
        if (!api) {
            lock.Unlock();
            _FinishAudiobookProbe(kAudiobookTemporaryError);
            return;
        }
        fAudiobookProbeInFlight = true;
    }

    api->GetSavedAudiobooks(0, 1,
        [this, api](bool ok, const nlohmann::json& data) {
            if (!ok) {
                _FinishAudiobookProbe(_FailureState(data));
                return;
            }
            if (data.contains("items") && data["items"].is_array()
                    && !data["items"].empty()) {
                _FinishAudiobookProbe(AllItemsMarketBlocked(data["items"])
                    ? kAudiobookUnavailable : kAudiobookAvailable);
                return;
            }
            api->Search("a", "audiobook",
                [this](bool searchOk, const nlohmann::json& searchData) {
                    if (!searchOk) {
                        _FinishAudiobookProbe(_FailureState(searchData));
                        return;
                    }
                    bool available = false;
                    if (searchData.contains("audiobooks")
                            && searchData["audiobooks"].is_object()) {
                        const auto& books = searchData["audiobooks"];
                        available = books.value("total", 0) > 0
                            || (books.contains("items")
                                && books["items"].is_array()
                                && !books["items"].empty());
                        if (books.contains("items")
                                && AllItemsMarketBlocked(books["items"]))
                            available = false;
                    }
                    _FinishAudiobookProbe(available
                        ? kAudiobookAvailable : kAudiobookUnavailable);
                });
        });
}

void SpotifyCapabilities::_FinishAudiobookProbe(
    AudiobookCapabilityState state)
{
    std::vector<AudiobookCapabilityCallback> waiters;
    {
        BAutolock lock(&fLock);
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
