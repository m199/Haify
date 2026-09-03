#pragma once

#include <SupportDefs.h>

#include <functional>
#include <string>
#include <vector>

enum AudiobookMode {
    kAudiobookAuto = 0,
    kAudiobookEnabled,
    kAudiobookDisabled
};

constexpr int kDefaultSeekBarColorRed = 150;
constexpr int kDefaultSeekBarColorGreen = 150;
constexpr int kDefaultSeekBarColorBlue = 252;
constexpr int kDefaultSeekBarColorAlpha = 255;
constexpr int kDefaultReplicantColorRed = 255;
constexpr int kDefaultReplicantColorGreen = 255;
constexpr int kDefaultReplicantColorBlue = 255;
constexpr int kDefaultReplicantColorAlpha = 255;
constexpr float kDefaultPlayerWindowWidth = 520.0f;
constexpr float kDefaultPlayerWindowHeight = 83.0f;
constexpr float kDefaultDiscoverWindowWidth = 800.0f;
constexpr float kDefaultDiscoverWindowHeight = 250.0f;
constexpr float kDefaultPlaylistWindowWidth = 665.0f;
constexpr float kDefaultPlaylistWindowHeight = 355.0f;
constexpr float kDefaultQueueWindowWidth = 535.0f;
constexpr float kDefaultQueueWindowHeight = 255.0f;
constexpr float kDefaultSearchWindowWidth = 660.0f;
constexpr float kDefaultSearchWindowHeight = 250.0f;
constexpr float kDefaultArtworkWindowSize = 180.0f;

struct HaifySettings {

    std::string accessToken;
    std::string refreshToken;
    std::string grantedScopes;
    int64       accessTokenExpiresAt = 0;
    int         authScopeVersion = 0;
    std::string spotifyAccountId;


    float       playerWindowX = -1, playerWindowY = -1;
    float       playerWindowW = -1, playerWindowH = -1;

    bool        browserWindowOpen = true;
    float       browserWindowX = -1, browserWindowY = -1;
    float       browserWindowW = -1, browserWindowH = -1;
    bool        discoverTabPlaylists = true;
    bool        discoverTabTopTracks = true;
    bool        discoverTabTopArtists = true;
    bool        discoverTabNewReleases = true;
    bool        discoverTabSavedAlbums = true;
    bool        discoverTabPodcasts = true;
    bool        discoverTabFollowedArtists = true;
    bool        discoverTabSavedEpisodes = true;
    bool        discoverTabAudiobooks = true;
    std::vector<std::string> discoverTabOrder = {
        "playlists", "top_tracks", "top_artists", "new_releases",
        "saved_albums", "podcasts", "followed_artists",
        "saved_episodes", "audiobooks"
    };

    bool        queueWindowOpen = false;
    float       queueWindowX = -1, queueWindowY = -1;
    float       queueWindowW = -1, queueWindowH = -1;

    bool        searchWindowOpen = false;
    float       searchWindowX = -1, searchWindowY = -1;
    float       searchWindowW = -1, searchWindowH = -1;

    bool        artworkWindowOpen = false;
    float       artworkWindowX = -1, artworkWindowY = -1;
    float       artworkWindowW = -1, artworkWindowH = -1;

    bool        searchFilterAll       = false;
    bool        searchFilterTracks    = true;
    bool        searchFilterArtists   = false;
    bool        searchFilterAlbums    = false;
    bool        searchFilterPlaylists = false;
    bool        searchFilterShows     = false;
    bool        searchFilterEpisodes  = false;
    bool        searchFilterAudiobooks = false;

    int         audiobookMode = kAudiobookAuto;

    float       playlistWindowX = -1, playlistWindowY = -1;
    float       playlistWindowW = -1, playlistWindowH = -1;

    std::string librespotPath;
    bool        librespotAlwaysStart    = false;

    std::string librespotBackend;
    int         librespotBitrate        = 320;
    int         librespotVolume         = 100;
    bool        librespotAutoplay       = true;
    bool        librespotNormalization  = false;

    std::string librespotDeviceName;
    std::string librespotDeviceType;

    bool        librespotDisableDiscovery = false;
    std::string librespotCachePath;
    std::string librespotAdditionalArgs;

    bool        seekBarUseSystemColor = false;
    int         seekBarColorRed = kDefaultSeekBarColorRed;
    int         seekBarColorGreen = kDefaultSeekBarColorGreen;
    int         seekBarColorBlue = kDefaultSeekBarColorBlue;
    int         seekBarColorAlpha = kDefaultSeekBarColorAlpha;

    bool        deskbarReplicantEnabled = true;

    bool        replicantUseAutomaticColor = true;
    int         replicantColorRed = kDefaultReplicantColorRed;
    int         replicantColorGreen = kDefaultReplicantColorGreen;
    int         replicantColorBlue = kDefaultReplicantColorBlue;
    int         replicantColorAlpha = kDefaultReplicantColorAlpha;

    int         imageCacheLimitMB = 500;
};

class SettingsController {
public:
    static HaifySettings    Load();
    static status_t         Save(const HaifySettings& settings);
    static status_t         Update(
                                const std::function<void(HaifySettings&)>& update);
    static std::string      SettingsPath();
    static std::string      CachePath(const std::string& relativePath = "",
                                bool createDirectory = true);
    static std::string      CacheFilePath(const std::string& directory,
                                const std::string& fileName,
                                bool createDirectory = true);
    static std::string      DefaultCachePath();
    static std::string      LibrespotSystemCachePath(
                                const HaifySettings& settings);
    static bool             PrepareLibrespotOAuthRegistration(
                                const HaifySettings& settings);
    static void             FinishLibrespotOAuthRegistration();
    static std::string      FindLibrespotPath();
    static std::string      LibrespotEventScriptPath();
    static std::string      LibrespotEventStatePath();
    static bool             CredentialsExist(const HaifySettings& s);
};
