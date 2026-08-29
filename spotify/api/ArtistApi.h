#pragma once

#include "SpotifyApiTypes.h"

#include <functional>
#include <string>

enum class ArtistTopTracksSource {
    kOfficialTopTracks,
    kSearchFallback
};

struct ArtistTopTracksResult {
    nlohmann::json tracks = nlohmann::json::array();
    ArtistTopTracksSource source = ArtistTopTracksSource::kOfficialTopTracks;
};

class ArtistApi {
public:
    using GetHandler = std::function<void(const std::string& path,
        JsonCallback callback)>;
    using CacheEraseHandler = std::function<void(const std::string& path)>;
    using TopTracksCallback = std::function<void(bool ok,
        const ArtistTopTracksResult& result, const nlohmann::json& error)>;

    ArtistApi(GetHandler get, CacheEraseHandler eraseCache);

    void            GetFollowedArtists(const std::string& after, int limit,
                                       JsonCallback callback);
    void            GetArtist(const std::string& artistId,
                              JsonCallback callback);
    void            GetArtistTopTracks(const std::string& artistId,
                                       TopTracksCallback callback);
    void            GetArtistAlbums(const std::string& artistId, int limit,
                                    JsonCallback callback);
private:
    void            _SearchTracksFallback(const std::string& artistId,
                            const std::string& artistName, int offset,
                            nlohmann::json tracks,
                            TopTracksCallback callback);

    GetHandler      fGet;
    CacheEraseHandler fEraseCache;
};
