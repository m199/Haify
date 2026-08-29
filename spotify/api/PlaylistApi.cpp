#include "PlaylistApi.h"
#include "HaifyDebug.h"
#include "SettingsController.h"
#include "spotify/SpotifyPlaylistPolicy.h"
#include "spotify/SpotifyUri.h"
#include "SpotifyUrl.h"

#include <algorithm>
#include <Autolock.h>
#include <Path.h>
#include <set>
#include <utility>
#include <unistd.h>

static bool
HaifyPlaylistCachePath(const std::string& playlistId, BPath& path,
    bool createDirectories)
{
    std::string file = SettingsController::CacheFilePath("playlists",
        playlistId + ".json", createDirectories);
    return !file.empty() && path.SetTo(file.c_str()) == B_OK;
}

static void
DeletePlaylistCache(const std::string& playlistId)
{
    BPath path;
    if (HaifyPlaylistCachePath(playlistId, path, false))
        unlink(path.Path());
}

static bool
IsPlayablePlaylistItem(const std::string& uri)
{
    return SpotifyItemIsPlayable(SpotifyItemKindForUri(uri));
}

static std::vector<std::pair<std::string, int>>
ValidRemovalPositions(const std::vector<std::pair<std::string, int>>& items)
{
    std::vector<std::pair<std::string, int>> validItems;
    for (const auto& item : items) {
        if (IsPlayablePlaylistItem(item.first) && item.second >= 0)
            validItems.push_back(item);
    }
    return validItems;
}

static nlohmann::json
PlaylistPositionError(const char* error)
{
    return {{"status", 400}, {"error", error}};
}

static const nlohmann::json*
PlaylistEntryItem(const nlohmann::json& entry)
{
    if (!entry.is_object())
        return nullptr;
    if (entry.contains("item") && entry["item"].is_object())
        return &entry["item"];
    if (entry.contains("track") && entry["track"].is_object())
        return &entry["track"];
    return nullptr;
}

static std::string
PlaylistEntryUri(const nlohmann::json& entry)
{
    const nlohmann::json* item = PlaylistEntryItem(entry);
    if (item == nullptr || !item->contains("uri")
            || !(*item)["uri"].is_string()) {
        return "";
    }
    return (*item)["uri"].get<std::string>();
}

static void
AppendPlaylistPageUris(const nlohmann::json& page,
    std::vector<std::string>& uris)
{
    for (const auto& entry : page)
        uris.push_back(PlaylistEntryUri(entry));
}

static int
JsonIntValue(const nlohmann::json& data, const char* name, int fallback)
{
    if (!data.contains(name))
        return fallback;
    if (!data[name].is_number_integer() && !data[name].is_number_unsigned())
        return fallback;
    return data[name].get<int>();
}

static bool
PlaylistPageHasMore(int pageCount, int offset, int total)
{
    return pageCount > 0 && (total < 0 || offset < total);
}

struct PreciseRemovalSelection {
    std::set<int> positions;
    std::set<std::string> affectedUris;
};

static bool
BuildPreciseRemovalSelection(
    const std::vector<std::pair<std::string, int>>& requestedItems,
    const std::vector<std::string>& playlistUris,
    PreciseRemovalSelection& selection)
{
    for (const auto& item : requestedItems) {
        if ((size_t)item.second >= playlistUris.size()
                || playlistUris[item.second] != item.first
                || !selection.positions.insert(item.second).second) {
            return false;
        }
        selection.affectedUris.insert(item.first);
    }
    return true;
}

static bool
BuildDesiredUris(const std::vector<std::string>& playlistUris,
    const std::set<int>& selectedPositions, std::vector<std::string>& desired)
{
    for (size_t position = 0; position < playlistUris.size(); position++) {
        if (selectedPositions.find((int)position) != selectedPositions.end())
            continue;
        const std::string& uri = playlistUris[position];
        if (!IsPlayablePlaylistItem(uri))
            return false;
        desired.push_back(uri);
    }
    return true;
}

static void
AppendRestoreRun(std::vector<std::pair<int, std::vector<std::string>>>& runs,
    int finalPosition, const std::string& uri)
{
    if (!runs.empty()) {
        auto& run = runs.back();
        if (run.second.size() < 100
                && run.first + (int)run.second.size() == finalPosition) {
            run.second.push_back(uri);
            return;
        }
    }
    runs.push_back({finalPosition, {uri}});
}

static void
BuildRestoreRuns(const std::vector<std::string>& playlistUris,
    const PreciseRemovalSelection& selection,
    std::vector<std::pair<int, std::vector<std::string>>>& runs)
{
    int selectedBefore = 0;
    auto nextSelected = selection.positions.begin();
    for (size_t position = 0; position < playlistUris.size(); position++) {
        while (nextSelected != selection.positions.end()
                && *nextSelected < (int)position) {
            selectedBefore++;
            nextSelected++;
        }

        const std::string& uri = playlistUris[position];
        if (selection.affectedUris.find(uri) == selection.affectedUris.end()
                || selection.positions.find((int)position)
                    != selection.positions.end()) {
            continue;
        }
        AppendRestoreRun(runs, (int)position - selectedBefore, uri);
    }
}

struct PlaylistApi::PreciseRemovalState {
    std::string playlistId;
    std::string snapshotId;
    std::vector<std::pair<std::string, int>> requestedItems;
    std::vector<std::string> playlistUris;
    int offset = 0;
    std::vector<std::pair<int, std::vector<std::string>>> restoreRuns;
    size_t restoreIndex = 0;
    JsonCallback callback;
};

PlaylistApi::PlaylistApi(GetHandler get, BodyRequestHandler put,
    BodyRequestHandler post, BodyRequestHandler deleteRequest,
    BodyRequestHandler uploadImage, CacheHandler invalidateCachePrefix)
    : fGet(std::move(get)),
      fPut(std::move(put)),
      fPost(std::move(post)),
      fDelete(std::move(deleteRequest)),
      fUploadImage(std::move(uploadImage)),
      fInvalidateCachePrefix(std::move(invalidateCachePrefix)),
      fLock("Playlist API")
{
}

void
PlaylistApi::SetAccountId(const std::string& accountId)
{
    BAutolock lock(&fLock);
    if (fAccountId == accountId)
        return;
    fAccountId = accountId;
    fCachedPlaylists.clear();
}

void
PlaylistApi::ClearSession()
{
    BAutolock lock(&fLock);
    fAccountId.clear();
    fCachedPlaylists.clear();
}

std::vector<std::pair<std::string, std::string>>
PlaylistApi::GetCachedPlaylists() const
{
    BAutolock lock(&fLock);
    return fCachedPlaylists;
}

void
PlaylistApi::GetPlaylists(JsonCallback callback)
{
    _GetPlaylistsPage(0, nlohmann::json::array(), callback);
}

void
PlaylistApi::_GetPlaylistsPage(int offset, nlohmann::json items,
    JsonCallback callback)
{
    fGet("/me/playlists?limit=50&offset=" + std::to_string(offset),
        [this, offset, items, callback](bool ok,
            const nlohmann::json& data) mutable {
        if (!ok || !data.contains("items") || !data["items"].is_array()) {
            if (callback)
                callback(false, data);
            return;
        }
        for (const auto& item : data["items"])
            items.push_back(item);
        int count = (int)data["items"].size();
        int total = data.value("total", offset + count);
        if (count > 0 && offset + count < total) {
            _GetPlaylistsPage(offset + count, std::move(items), callback);
            return;
        }

        nlohmann::json result = data;
        result["items"] = items;
        result["total"] = total;
        {
            BAutolock lock(&fLock);
            fCachedPlaylists.clear();
            for (const auto& item : items) {
                if (!item.is_object())
                    continue;
                std::string ownerAccountId;
                std::string ownerLegacyId;
                if (item.contains("owner") && item["owner"].is_object()) {
                    ownerAccountId = item["owner"].value("account_id", "");
                    ownerLegacyId = item["owner"].value("id", "");
                }
                bool writable = SpotifyPlaylistIsWritable(
                    item.value("collaborative", false), ownerAccountId,
                    ownerLegacyId, fAccountId);
                if (writable) {
                    fCachedPlaylists.push_back({
                        item.value("id", ""),
                        item.value("name", "Unknown")
                    });
                }
            }
        }
        if (callback)
            callback(true, result);
    });
}

void
PlaylistApi::GetPlaylist(const std::string& playlistId, JsonCallback callback)
{
    fGet("/playlists/" + playlistId, callback);
}

void
PlaylistApi::InvalidatePlaylist(const std::string& playlistId)
{
    fInvalidateCachePrefix("/playlists/" + playlistId);
}

void
PlaylistApi::GetPlaylistTracks(const std::string& playlistId, int offset,
    int limit, JsonCallback callback)
{
    fGet("/playlists/" + playlistId + "/items?limit="
        + std::to_string(limit) + "&offset=" + std::to_string(offset),
        callback);
}

void
PlaylistApi::_RemoveCachedPlaylist(const std::string& playlistId)
{
    BAutolock lock(&fLock);
    fCachedPlaylists.erase(std::remove_if(fCachedPlaylists.begin(),
        fCachedPlaylists.end(), [&playlistId](const auto& playlist) {
            return playlist.first == playlistId;
        }), fCachedPlaylists.end());
}

void
PlaylistApi::_UpdateCachedPlaylistName(const std::string& playlistId,
    const std::string& name)
{
    BAutolock lock(&fLock);
    for (auto& playlist : fCachedPlaylists) {
        if (playlist.first == playlistId) {
            playlist.second = name;
            break;
        }
    }
}

void
PlaylistApi::CreatePlaylist(const std::string& name, JsonCallback callback)
{
    std::string body = nlohmann::json({{"name", name}, {"public", false}})
        .dump();
    fInvalidateCachePrefix("/me/playlists");
    fPost("/me/playlists", body, [this, name, callback](bool ok,
        const nlohmann::json& data) {
        if (ok) {
            std::string id = data.value("id", "");
            if (!id.empty()) {
                _RemoveCachedPlaylist(id);
                BAutolock lock(&fLock);
                fCachedPlaylists.insert(fCachedPlaylists.begin(), {id, name});
            }
        }
        if (callback)
            callback(ok, data);
    });
}

void
PlaylistApi::RenamePlaylist(const std::string& playlistId,
    const std::string& name, JsonCallback callback)
{
    std::string body = nlohmann::json({{"name", name}}).dump();
    fInvalidateCachePrefix("/me/playlists");
    fInvalidateCachePrefix("/playlists/" + playlistId);
    fPut("/playlists/" + playlistId, body, [this, playlistId, name, callback](
        bool ok, const nlohmann::json& data) {
        if (ok)
            _UpdateCachedPlaylistName(playlistId, name);
        if (callback)
            callback(ok, data);
    });
}

void
PlaylistApi::UpdatePlaylistDetails(const std::string& playlistId,
    const std::string& name, const std::string& description, bool isPublic,
    JsonCallback callback)
{
    nlohmann::json request = {
        {"name", name},
        {"description", description},
        {"public", isPublic}
    };
    fInvalidateCachePrefix("/me/playlists");
    fInvalidateCachePrefix("/playlists/" + playlistId);
    fPut("/playlists/" + playlistId, request.dump(),
        [this, playlistId, name, callback](bool ok,
            const nlohmann::json& data) {
            if (ok)
                _UpdateCachedPlaylistName(playlistId, name);
            if (callback)
                callback(ok, data);
        });
}

void
PlaylistApi::UnfollowPlaylist(const std::string& playlistId,
    JsonCallback callback)
{
    fInvalidateCachePrefix("/me/playlists");
    fInvalidateCachePrefix("/playlists/" + playlistId);
    fDelete("/me/library?uris="
            + SpotifyUrlEncode(SpotifyUriForItemKind(kSpotifyItemPlaylist,
                playlistId)), "",
        [this, playlistId, callback](bool ok, const nlohmann::json& data) {
            if (ok)
                _RemoveCachedPlaylist(playlistId);
            if (callback)
                callback(ok, data);
        });
}

void
PlaylistApi::AddTrackToPlaylist(const std::string& playlistId,
    const std::string& trackUri, JsonCallback callback)
{
    std::string body = nlohmann::json({{"uris", {trackUri}}}).dump();
    fInvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    fPost("/playlists/" + playlistId + "/items", body, callback);
}

void
PlaylistApi::RemoveTrackFromPlaylist(const std::string& playlistId,
    const std::string& trackUri, JsonCallback callback)
{
    nlohmann::json request;
    request["items"] = nlohmann::json::array();
    request["items"].push_back({{"uri", trackUri}});
    std::string body = request.dump();
    std::string path = "/playlists/" + playlistId + "/items";
    DEBUG_PRINT("PlaylistApi: RemoveTrackFromPlaylist body: %s\n",
        body.c_str());
    fInvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    fDelete(path, body, callback);
}

void
PlaylistApi::RemoveItemsFromPlaylist(const std::string& playlistId,
    const std::vector<std::string>& uris, const std::string& snapshotId,
    JsonCallback callback)
{
    nlohmann::json items = nlohmann::json::array();
    for (const std::string& uri : uris) {
        if (IsPlayablePlaylistItem(uri))
            items.push_back({{"uri", uri}});
    }
    if (items.empty()) {
        if (callback)
            callback(false, {{"status", 400},
                {"error", "no_valid_playlist_items"}});
        return;
    }
    nlohmann::json request = {{"items", items}};
    if (!snapshotId.empty())
        request["snapshot_id"] = snapshotId;
    fInvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    fDelete("/playlists/" + playlistId + "/items", request.dump(), callback);
}

void
PlaylistApi::RemovePlaylistItemsAtPositions(
    const std::string& playlistId,
    const std::vector<std::pair<std::string, int>>& items,
    const std::string& snapshotId, JsonCallback callback)
{
    std::vector<std::pair<std::string, int>> validItems
        = ValidRemovalPositions(items);
    if (validItems.empty()) {
        if (callback)
            callback(false, PlaylistPositionError("no_valid_playlist_positions"));
        return;
    }

    std::shared_ptr<PreciseRemovalState> state(new PreciseRemovalState());
    state->playlistId = playlistId;
    state->snapshotId = snapshotId;
    state->requestedItems = std::move(validItems);
    state->callback = callback;
    InvalidatePlaylist(playlistId);
    DeletePlaylistCache(playlistId);
    _FetchPreciseRemovalPage(state);
}

void
PlaylistApi::RemovePlaylistItemsFromKnownSnapshot(
    const std::string& playlistId,
    const std::vector<std::pair<std::string, int>>& items,
    const std::string& snapshotId,
    const std::vector<std::string>& playlistUris, JsonCallback callback)
{
    if (snapshotId.empty() || playlistUris.empty()) {
        RemovePlaylistItemsAtPositions(playlistId, items, snapshotId, callback);
        return;
    }
    std::vector<std::pair<std::string, int>> validItems
        = ValidRemovalPositions(items);
    if (validItems.empty()) {
        if (callback)
            callback(false, PlaylistPositionError("no_valid_playlist_positions"));
        return;
    }
    std::shared_ptr<PreciseRemovalState> state(new PreciseRemovalState());
    state->playlistId = playlistId;
    state->snapshotId = snapshotId;
    state->requestedItems = std::move(validItems);
    state->playlistUris = playlistUris;
    state->callback = callback;
    InvalidatePlaylist(playlistId);
    DeletePlaylistCache(playlistId);
    _ApplyPreciseRemoval(state);
}

void
PlaylistApi::_FetchPreciseRemovalPage(
    const std::shared_ptr<PreciseRemovalState>& state)
{
    GetPlaylistTracks(state->playlistId, state->offset, 50,
        [this, state](bool ok, const nlohmann::json& data) {
            if (!ok || !data.is_object() || !data.contains("items")
                    || !data["items"].is_array()) {
                if (state->callback)
                    state->callback(false, data);
                return;
            }

            const nlohmann::json& page = data["items"];
            AppendPlaylistPageUris(page, state->playlistUris);
            int pageCount = (int)page.size();
            state->offset += pageCount;
            int total = JsonIntValue(data, "total", -1);

            if (PlaylistPageHasMore(pageCount, state->offset, total)) {
                _FetchPreciseRemovalPage(state);
                return;
            }
            if (total >= 0 && state->offset < total) {
                if (state->callback) {
                    state->callback(false, {{"status", -1},
                        {"error", "incomplete_playlist_read"}});
                }
                return;
            }
            _ApplyPreciseRemoval(state);
        });
}

void
PlaylistApi::_ApplyPreciseRemoval(
    const std::shared_ptr<PreciseRemovalState>& state)
{
    std::sort(state->requestedItems.begin(), state->requestedItems.end(),
        [](const auto& left, const auto& right) {
            return left.second < right.second;
        });

    PreciseRemovalSelection selection;
    if (!BuildPreciseRemovalSelection(state->requestedItems,
            state->playlistUris, selection)) {
        if (state->callback) {
            state->callback(false, {{"status", 409},
                {"error", "playlist_position_changed"}});
        }
        return;
    }

    std::vector<std::string> desiredUris;
    bool canReplaceAtomically = state->playlistUris.size()
        - selection.positions.size() <= 100
        && BuildDesiredUris(state->playlistUris, selection.positions,
            desiredUris);
    if (canReplaceAtomically) {
        DEBUG_PRINT("PlaylistApi: precise removal replaces %lu items with %lu items\n",
            (unsigned long)state->playlistUris.size(),
            (unsigned long)desiredUris.size());
        ReplacePlaylistItems(state->playlistId, desiredUris,
            [state](bool ok, const nlohmann::json& data) {
                if (state->callback)
                    state->callback(ok, data);
            });
        return;
    }

    DEBUG_PRINT("PlaylistApi: precise removal uses duplicate restore for %lu items\n",
        (unsigned long)state->playlistUris.size());

    BuildRestoreRuns(state->playlistUris, selection, state->restoreRuns);
    std::vector<std::string> affected(selection.affectedUris.begin(),
        selection.affectedUris.end());
    RemoveItemsFromPlaylist(state->playlistId, affected, state->snapshotId,
        [this, state](bool ok, const nlohmann::json& data) {
            if (!ok) {
                if (state->callback)
                    state->callback(false, data);
                return;
            }
            _RestorePreciseRemovalItems(state);
        });
}

void
PlaylistApi::_RestorePreciseRemovalItems(
    const std::shared_ptr<PreciseRemovalState>& state)
{
    if (state->restoreIndex >= state->restoreRuns.size()) {
        if (state->callback)
            state->callback(true, {{"status", 200}});
        return;
    }

    const auto& run = state->restoreRuns[state->restoreIndex];
    nlohmann::json request = {{"uris", run.second}, {"position", run.first}};
    fInvalidateCachePrefix("/playlists/" + state->playlistId);
    DeletePlaylistCache(state->playlistId);
    fPost("/playlists/" + state->playlistId + "/items", request.dump(),
        [this, state](bool ok, const nlohmann::json& data) {
            if (!ok) {
                if (state->callback) {
                    nlohmann::json error = data;
                    error["partial_update"] = true;
                    error["error"] = "duplicate_restore_failed";
                    state->callback(false, error);
                }
                return;
            }
            state->restoreIndex++;
            _RestorePreciseRemovalItems(state);
        });
}

void
PlaylistApi::ReorderPlaylistItems(const std::string& playlistId,
    int rangeStart, int insertBefore, int rangeLength,
    const std::string& snapshotId, JsonCallback callback)
{
    nlohmann::json request = {
        {"range_start", rangeStart},
        {"insert_before", insertBefore},
        {"range_length", rangeLength < 1 ? 1 : rangeLength}
    };
    if (!snapshotId.empty())
        request["snapshot_id"] = snapshotId;
    fInvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    fPut("/playlists/" + playlistId + "/items", request.dump(), callback);
}

void
PlaylistApi::ReplacePlaylistItems(const std::string& playlistId,
    const std::vector<std::string>& uris, JsonCallback callback)
{
    nlohmann::json validUris = nlohmann::json::array();
    for (const std::string& uri : uris) {
        if (IsPlayablePlaylistItem(uri))
            validUris.push_back(uri);
    }
    if (validUris.size() > 100) {
        if (callback)
            callback(false, {{"status", 400},
                {"error", "too_many_playlist_items"}});
        return;
    }
    fInvalidateCachePrefix("/playlists/" + playlistId);
    DeletePlaylistCache(playlistId);
    fPut("/playlists/" + playlistId + "/items",
        nlohmann::json({{"uris", validUris}}).dump(), callback);
}

void
PlaylistApi::GetPlaylistImages(const std::string& playlistId,
    JsonCallback callback)
{
    fGet("/playlists/" + playlistId + "/images", callback);
}

void
PlaylistApi::UploadPlaylistImage(const std::string& playlistId,
    const std::string& base64Jpeg, JsonCallback callback)
{
    if (base64Jpeg.empty() || base64Jpeg.size() > 256 * 1024) {
        if (callback)
            callback(false, {{"status", 400},
                {"error", "invalid_playlist_image_size"}});
        return;
    }
    fInvalidateCachePrefix("/playlists/" + playlistId);
    fUploadImage("/playlists/" + playlistId + "/images", base64Jpeg,
        callback);
}
