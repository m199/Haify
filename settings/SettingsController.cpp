#include "SettingsController.h"
#include "Config.h"

#include <Directory.h>
#include <Autolock.h>
#include <Errors.h>
#include <FindDirectory.h>
#include <Locker.h>
#include <Path.h>
#include <Entry.h>
#include <Roster.h>
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <fstream>
#include <fcntl.h>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

static const char* kLibrespotSignature = "application/x-vnd.librespot";
static BLocker sSettingsLock("Haify settings");
static bool sLastLoadValid = true;

static bool
EnsureDirectory(const std::string& path)
{
    if (path.empty())
        return false;
    if (create_directory(path.c_str(), 0755) == B_OK)
        return true;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool
EnsurePrivateDirectory(const std::string& path)
{
    if (!EnsureDirectory(path))
        return false;
    return chmod(path.c_str(), S_IRWXU) == 0;
}

static bool
IsSupportedLibrespotBackend(const std::string& backend)
{
    return backend == "sdl";
}

static std::string
LegacySettingsPath()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
        return "";
    path.Append(HAIFY_SETTINGS_FILE);
    return path.Path();
}

static std::string
HaifySettingsDirectoryPath()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
        return "";
    path.Append("Haify");
    if (!EnsureDirectory(path.Path()))
        return "";
    return path.Path();
}

static std::string
LibrespotSystemCacheDirectory()
{
    std::string root = HaifySettingsDirectoryPath();
    if (root.empty())
        return "";

    BPath path(root.c_str());
    path.Append("librespot");
    std::string destination = path.Path();
    return EnsurePrivateDirectory(destination) ? destination : "";
}

static void
MigrateLegacySettingsFile(const std::string& settingsPath)
{
    struct stat st;
    if (stat(settingsPath.c_str(), &st) == 0)
        return;

    std::string legacyPath = LegacySettingsPath();
    if (legacyPath.empty())
        return;
    if (stat(legacyPath.c_str(), &st) != 0)
        return;

    std::rename(legacyPath.c_str(), settingsPath.c_str());
}

static std::string
HaifyCachePath()
{
    BPath path;
    if (find_directory(B_USER_CACHE_DIRECTORY, &path) != B_OK)
        return "";
    path.Append("Haify");
    if (!EnsureDirectory(path.Path()))
        return "";
    return path.Path();
}

static void
MigrateLegacyCacheDirectory(const std::string& relativePath,
    const std::string& destination)
{
    struct stat st;
    if (stat(destination.c_str(), &st) == 0)
        return;

    std::vector<std::string> candidates;
    BPath legacySettings;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &legacySettings) == B_OK) {
        legacySettings.Append("Haify");
        if (relativePath == "images") {
            BPath imageCache(legacySettings);
            imageCache.Append("cache");
            candidates.push_back(imageCache.Path());
        }
        legacySettings.Append(relativePath.c_str());
        candidates.push_back(legacySettings.Path());
    }
    if (relativePath == "images") {
        BPath legacyCache;
        if (find_directory(B_USER_CACHE_DIRECTORY, &legacyCache) == B_OK) {
            legacyCache.Append("Haify");
            legacyCache.Append("cache");
            candidates.push_back(legacyCache.Path());
        }
    }

    for (const std::string& candidate : candidates) {
        if (stat(candidate.c_str(), &st) == 0
                && std::rename(candidate.c_str(), destination.c_str()) == 0) {
            return;
        }
    }
}

std::string SettingsController::SettingsPath()
{
    std::string directory = HaifySettingsDirectoryPath();
    if (directory.empty())
        return "";
    BPath path(directory.c_str());
    path.Append(HAIFY_SETTINGS_FILE);
    std::string settingsPath = path.Path();
    MigrateLegacySettingsFile(settingsPath);
    return settingsPath;
}

std::string SettingsController::CachePath(const std::string& relativePath,
    bool createDirectory)
{
    std::string root = HaifyCachePath();
    if (root.empty() || relativePath.empty())
        return root;

    BPath path(root.c_str());
    path.Append(relativePath.c_str());
    std::string result = path.Path();
    MigrateLegacyCacheDirectory(relativePath, result);
    if (createDirectory && !EnsureDirectory(result))
        return "";
    return result;
}

std::string SettingsController::CacheFilePath(const std::string& directory,
    const std::string& fileName, bool createDirectory)
{
    std::string cache = CachePath(directory, createDirectory);
    if (cache.empty() || fileName.empty())
        return "";
    BPath path(cache.c_str());
    if (path.Append(fileName.c_str()) != B_OK)
        return "";
    return path.Path();
}

std::string SettingsController::DefaultCachePath()
{
    std::string root = HaifyCachePath();
    if (root.empty())
        return "";

    BPath path(root.c_str());
    path.Append("librespot");
    std::string destination = path.Path();

    struct stat st;
    if (stat(destination.c_str(), &st) != 0) {
        BPath legacy;
        if (find_directory(B_USER_CACHE_DIRECTORY, &legacy) == B_OK) {
            legacy.Append("librespot");
            if (stat(legacy.Path(), &st) == 0
                && std::rename(legacy.Path(), destination.c_str()) != 0) {
                return legacy.Path();
            }
        }
    }
    if (!EnsureDirectory(destination))
        return "";
    return destination;
}

static BPath
LibrespotCredentialsPath(const std::string& directory, const char* fileName)
{
    BPath path(directory.c_str());
    path.Append(fileName);
    return path;
}

static void
RestoreLibrespotBackupIfNeeded(const char* credentialsPath,
    const char* backupPath)
{
    struct stat backupStat;
    if (stat(backupPath, &backupStat) != 0)
        return;

    struct stat credentialsStat;
    if (stat(credentialsPath, &credentialsStat) == 0)
        unlink(backupPath);
    else
        std::rename(backupPath, credentialsPath);
}

static bool
UseExistingLibrespotCredentials(const char* credentialsPath)
{
    struct stat st;
    if (stat(credentialsPath, &st) != 0)
        return false;
    chmod(credentialsPath, S_IRUSR | S_IWUSR);
    return true;
}

static bool
IsRegularFile(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool
CopyLibrespotCredentials(const char* sourcePath, const char* destinationPath)
{
    std::ifstream input(sourcePath, std::ios::binary);
    std::ofstream output(destinationPath, std::ios::binary | std::ios::trunc);
    if (input.is_open() && output.is_open()) {
        output << input.rdbuf();
        output.flush();
        if ((input.good() || input.eof()) && output.good()) {
            output.close();
            chmod(destinationPath, S_IRUSR | S_IWUSR);
            unlink(sourcePath);
            return true;
        }
    }
    output.close();
    unlink(destinationPath);
    return false;
}

static bool
MoveLibrespotCredentials(const char* sourcePath, const char* destinationPath)
{
    if (std::rename(sourcePath, destinationPath) == 0) {
        chmod(destinationPath, S_IRUSR | S_IWUSR);
        return true;
    }

    // A custom cache may live on another volume, where rename() cannot move
    // the file. Copy it privately and only remove the old file after success.
    return CopyLibrespotCredentials(sourcePath, destinationPath);
}

std::string SettingsController::LibrespotSystemCachePath(
    const HaifySettings& settings)
{
    std::string destination = LibrespotSystemCacheDirectory();
    if (destination.empty())
        return "";

    BPath credentialsPath = LibrespotCredentialsPath(destination,
        "credentials.json");
    BPath backupPath = LibrespotCredentialsPath(destination,
        "credentials.json.oauth-backup");

    RestoreLibrespotBackupIfNeeded(credentialsPath.Path(), backupPath.Path());
    if (UseExistingLibrespotCredentials(credentialsPath.Path()))
        return destination;

    std::string oldCache = settings.librespotCachePath.empty()
        ? DefaultCachePath() : settings.librespotCachePath;
    if (oldCache.empty() || oldCache == destination)
        return destination;

    BPath oldCredentialsPath = LibrespotCredentialsPath(oldCache,
        "credentials.json");
    if (!IsRegularFile(oldCredentialsPath.Path()))
        return destination;

    MoveLibrespotCredentials(oldCredentialsPath.Path(), credentialsPath.Path());
    return destination;
}

bool SettingsController::PrepareLibrespotOAuthRegistration(
    const HaifySettings& settings)
{
    std::string directory = LibrespotSystemCachePath(settings);
    if (directory.empty())
        return false;

    BPath credentialsPath(directory.c_str());
    credentialsPath.Append("credentials.json");
    BPath backupPath(directory.c_str());
    backupPath.Append("credentials.json.oauth-backup");

    struct stat st;
    if (stat(credentialsPath.Path(), &st) != 0)
        return true;

    unlink(backupPath.Path());
    return std::rename(credentialsPath.Path(), backupPath.Path()) == 0;
}

void SettingsController::FinishLibrespotOAuthRegistration()
{
    std::string directory = LibrespotSystemCacheDirectory();
    if (directory.empty())
        return;

    BPath credentialsPath(directory.c_str());
    credentialsPath.Append("credentials.json");
    BPath backupPath(directory.c_str());
    backupPath.Append("credentials.json.oauth-backup");

    struct stat st;
    if (stat(backupPath.Path(), &st) != 0)
        return;
    if (stat(credentialsPath.Path(), &st) == 0)
        unlink(backupPath.Path());
    else
        std::rename(backupPath.Path(), credentialsPath.Path());

    if (stat(credentialsPath.Path(), &st) == 0)
        chmod(credentialsPath.Path(), S_IRUSR | S_IWUSR);
}

std::string SettingsController::FindLibrespotPath()
{
    entry_ref ref;
    if (be_roster->FindApp(kLibrespotSignature, &ref) != B_OK)
        return "";

    BEntry entry(&ref);
    BPath path;
    if (entry.GetPath(&path) != B_OK)
        return "";
    return path.Path();
}

std::string SettingsController::LibrespotEventScriptPath()
{
    std::string cache = HaifyCachePath();
    return cache.empty() ? "" : cache + "/librespot_event.sh";
}

std::string SettingsController::LibrespotEventStatePath()
{
    std::string cache = HaifyCachePath();
    return cache.empty() ? "" : cache + "/librespot_event.state";
}

bool SettingsController::CredentialsExist(const HaifySettings& s)
{
    std::string systemCache = LibrespotSystemCachePath(s);
    if (systemCache.empty())
        return false;
    systemCache += "/credentials.json";
    struct stat st;
    return stat(systemCache.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static void
JsonGetString(const nlohmann::json& j, const char* key, std::string& value)
{
    if (j.contains(key) && j[key].is_string())
        value = j[key];
}

static void
JsonGetInt(const nlohmann::json& j, const char* key, int& value)
{
    if (j.contains(key) && j[key].is_number_integer())
        value = j[key];
}

static void
JsonGetBool(const nlohmann::json& j, const char* key, bool& value)
{
    if (j.contains(key) && j[key].is_boolean())
        value = j[key];
}

static void
JsonGetFloat(const nlohmann::json& j, const char* key, float& value)
{
    if (j.contains(key) && j[key].is_number())
        value = j[key];
}

static void
LoadAuthSettings(const nlohmann::json& j, HaifySettings& s)
{
    JsonGetString(j, "access_token", s.accessToken);
    JsonGetString(j, "refresh_token", s.refreshToken);
    JsonGetString(j, "granted_scopes", s.grantedScopes);
    JsonGetInt(j, "auth_scope_version", s.authScopeVersion);
    JsonGetString(j, "spotify_account_id", s.spotifyAccountId);
    if (j.contains("access_token_expires_at")
            && j["access_token_expires_at"].is_number_integer()) {
        s.accessTokenExpiresAt = j["access_token_expires_at"];
    }
}

static void
LoadWindowSettings(const nlohmann::json& j, HaifySettings& s)
{
    JsonGetFloat(j, "player_window_x", s.playerWindowX);
    JsonGetFloat(j, "player_window_y", s.playerWindowY);
    JsonGetFloat(j, "player_window_w", s.playerWindowW);
    JsonGetFloat(j, "player_window_h", s.playerWindowH);
    JsonGetBool(j, "browser_window_open", s.browserWindowOpen);
    JsonGetFloat(j, "browser_window_x", s.browserWindowX);
    JsonGetFloat(j, "browser_window_y", s.browserWindowY);
    JsonGetFloat(j, "browser_window_w", s.browserWindowW);
    JsonGetFloat(j, "browser_window_h", s.browserWindowH);
    JsonGetBool(j, "queue_window_open", s.queueWindowOpen);
    JsonGetFloat(j, "queue_window_x", s.queueWindowX);
    JsonGetFloat(j, "queue_window_y", s.queueWindowY);
    JsonGetFloat(j, "queue_window_w", s.queueWindowW);
    JsonGetFloat(j, "queue_window_h", s.queueWindowH);
    JsonGetBool(j, "search_window_open", s.searchWindowOpen);
    JsonGetFloat(j, "search_window_x", s.searchWindowX);
    JsonGetFloat(j, "search_window_y", s.searchWindowY);
    JsonGetFloat(j, "search_window_w", s.searchWindowW);
    JsonGetFloat(j, "search_window_h", s.searchWindowH);
    JsonGetBool(j, "artwork_window_open", s.artworkWindowOpen);
    JsonGetFloat(j, "artwork_window_x", s.artworkWindowX);
    JsonGetFloat(j, "artwork_window_y", s.artworkWindowY);
    JsonGetFloat(j, "artwork_window_w", s.artworkWindowW);
    JsonGetFloat(j, "artwork_window_h", s.artworkWindowH);
    JsonGetFloat(j, "playlist_window_x", s.playlistWindowX);
    JsonGetFloat(j, "playlist_window_y", s.playlistWindowY);
    JsonGetFloat(j, "playlist_window_w", s.playlistWindowW);
    JsonGetFloat(j, "playlist_window_h", s.playlistWindowH);
}

static void
LoadDiscoverSettings(const nlohmann::json& j, HaifySettings& s)
{
    JsonGetBool(j, "discover_tab_playlists", s.discoverTabPlaylists);
    JsonGetBool(j, "discover_tab_top_tracks", s.discoverTabTopTracks);
    JsonGetBool(j, "discover_tab_top_artists", s.discoverTabTopArtists);
    JsonGetBool(j, "discover_tab_new_releases", s.discoverTabNewReleases);
    JsonGetBool(j, "discover_tab_saved_albums", s.discoverTabSavedAlbums);
    JsonGetBool(j, "discover_tab_podcasts", s.discoverTabPodcasts);
    JsonGetBool(j, "discover_tab_followed_artists",
        s.discoverTabFollowedArtists);
    JsonGetBool(j, "discover_tab_saved_episodes",
        s.discoverTabSavedEpisodes);
    JsonGetBool(j, "discover_tab_audiobooks", s.discoverTabAudiobooks);
    if (!j.contains("discover_tab_order") || !j["discover_tab_order"].is_array())
        return;
    s.discoverTabOrder.clear();
    for (const auto& value : j["discover_tab_order"]) {
        if (value.is_string())
            s.discoverTabOrder.push_back(value.get<std::string>());
    }
}

static void
LoadSearchSettings(const nlohmann::json& j, HaifySettings& s)
{
    bool hasNewSearchMode = j.contains("search_filter_all")
        && j["search_filter_all"].is_boolean();
    bool hasLegacySearchFilters = j.contains("search_filter_tracks")
        || j.contains("search_filter_artists")
        || j.contains("search_filter_albums")
        || j.contains("search_filter_playlists")
        || j.contains("search_filter_shows");
    JsonGetBool(j, "search_filter_all", s.searchFilterAll);
    JsonGetBool(j, "search_filter_tracks", s.searchFilterTracks);
    JsonGetBool(j, "search_filter_artists", s.searchFilterArtists);
    JsonGetBool(j, "search_filter_albums", s.searchFilterAlbums);
    JsonGetBool(j, "search_filter_playlists", s.searchFilterPlaylists);
    JsonGetBool(j, "search_filter_shows", s.searchFilterShows);
    JsonGetBool(j, "search_filter_episodes", s.searchFilterEpisodes);
    JsonGetBool(j, "search_filter_audiobooks", s.searchFilterAudiobooks);
    if (!hasNewSearchMode && hasLegacySearchFilters) {
        bool legacyAll = s.searchFilterTracks && s.searchFilterArtists
            && s.searchFilterAlbums && s.searchFilterPlaylists
            && s.searchFilterShows;
        s.searchFilterAll = legacyAll;
        if (legacyAll) {
            s.searchFilterTracks = false;
            s.searchFilterArtists = false;
            s.searchFilterAlbums = false;
            s.searchFilterPlaylists = false;
            s.searchFilterShows = false;
        }
    }
    JsonGetInt(j, "audiobook_mode", s.audiobookMode);
    if (s.audiobookMode < kAudiobookAuto
            || s.audiobookMode > kAudiobookDisabled) {
        s.audiobookMode = kAudiobookAuto;
    }
}

static void
LoadLibrespotSettings(const nlohmann::json& j, HaifySettings& s)
{
    JsonGetString(j, "librespot_path", s.librespotPath);
    JsonGetBool(j, "librespot_always_start", s.librespotAlwaysStart);
    JsonGetString(j, "librespot_backend", s.librespotBackend);
    JsonGetInt(j, "librespot_bitrate", s.librespotBitrate);
    JsonGetInt(j, "librespot_volume", s.librespotVolume);
    JsonGetBool(j, "librespot_autoplay", s.librespotAutoplay);
    JsonGetBool(j, "haify_autoplay", s.haifyAutoplay);
    if (s.librespotAutoplay && s.haifyAutoplay)
        s.librespotAutoplay = false;
    JsonGetBool(j, "librespot_normalization", s.librespotNormalization);
    JsonGetString(j, "librespot_device_name", s.librespotDeviceName);
    JsonGetString(j, "librespot_device_type", s.librespotDeviceType);
    JsonGetBool(j, "librespot_disable_discovery",
        s.librespotDisableDiscovery);
    JsonGetString(j, "librespot_cache_path", s.librespotCachePath);
    JsonGetString(j, "librespot_additional_args", s.librespotAdditionalArgs);
    if (s.librespotPath.empty())
        JsonGetString(j, "librespot", s.librespotPath);
    if (s.librespotBackend.empty())
        JsonGetString(j, "audio_backend", s.librespotBackend);
    if (!s.librespotBackend.empty()
            && !IsSupportedLibrespotBackend(s.librespotBackend)) {
        s.librespotBackend = "sdl";
    }
}

static void
LoadColorSettings(const nlohmann::json& j, HaifySettings& s)
{
    JsonGetBool(j, "seekbar_use_system_color", s.seekBarUseSystemColor);
    JsonGetInt(j, "seekbar_color_red", s.seekBarColorRed);
    JsonGetInt(j, "seekbar_color_green", s.seekBarColorGreen);
    JsonGetInt(j, "seekbar_color_blue", s.seekBarColorBlue);
    JsonGetInt(j, "seekbar_color_alpha", s.seekBarColorAlpha);
    JsonGetBool(j, "deskbar_replicant_enabled", s.deskbarReplicantEnabled);
    JsonGetBool(j, "replicant_use_automatic_color",
        s.replicantUseAutomaticColor);
    JsonGetInt(j, "replicant_color_red", s.replicantColorRed);
    JsonGetInt(j, "replicant_color_green", s.replicantColorGreen);
    JsonGetInt(j, "replicant_color_blue", s.replicantColorBlue);
    JsonGetInt(j, "replicant_color_alpha", s.replicantColorAlpha);
    JsonGetInt(j, "image_cache_limit_mb", s.imageCacheLimitMB);
}

static void
NormalizeColorSettings(HaifySettings& s)
{
    bool formerBlueDefault = s.seekBarColorRed == 0
        && s.seekBarColorGreen == 120 && s.seekBarColorBlue == 215;
    bool interimVioletDefault = s.seekBarColorRed == 142
        && s.seekBarColorGreen == 136 && s.seekBarColorBlue == 242;
    if ((formerBlueDefault || interimVioletDefault)
            && s.seekBarColorAlpha == 255) {
        s.seekBarColorRed = kDefaultSeekBarColorRed;
        s.seekBarColorGreen = kDefaultSeekBarColorGreen;
        s.seekBarColorBlue = kDefaultSeekBarColorBlue;
        s.seekBarColorAlpha = kDefaultSeekBarColorAlpha;
    }

    s.seekBarColorRed = std::max(0, std::min(255, s.seekBarColorRed));
    s.seekBarColorGreen = std::max(0, std::min(255, s.seekBarColorGreen));
    s.seekBarColorBlue = std::max(0, std::min(255, s.seekBarColorBlue));
    s.seekBarColorAlpha = std::max(0, std::min(255, s.seekBarColorAlpha));
    s.replicantColorRed = std::max(0, std::min(255, s.replicantColorRed));
    s.replicantColorGreen = std::max(0, std::min(255, s.replicantColorGreen));
    s.replicantColorBlue = std::max(0, std::min(255, s.replicantColorBlue));
    s.replicantColorAlpha = std::max(0, std::min(255, s.replicantColorAlpha));
}

HaifySettings SettingsController::Load()
{
    BAutolock lock(&sSettingsLock);
    HaifySettings s;
    std::string settingsPath = SettingsPath();
    std::ifstream f(settingsPath);
    if (!f.is_open())
    {
        sLastLoadValid = true;
        return s;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        sLastLoadValid = false;
        std::string backup = settingsPath + ".corrupt";
        std::rename(settingsPath.c_str(), backup.c_str());
        return s;
    }
    if (!j.is_object()) {
        sLastLoadValid = false;
        std::string backup = settingsPath + ".corrupt";
        std::rename(settingsPath.c_str(), backup.c_str());
        return s;
    }
    sLastLoadValid = true;

    LoadAuthSettings(j, s);
    LoadWindowSettings(j, s);
    LoadDiscoverSettings(j, s);
    LoadSearchSettings(j, s);
    LoadLibrespotSettings(j, s);
    LoadColorSettings(j, s);
    NormalizeColorSettings(s);

    return s;
}

status_t SettingsController::Save(const HaifySettings& s)
{
    BAutolock lock(&sSettingsLock);
    nlohmann::json j = {
        {"access_token",                 s.accessToken},
        {"refresh_token",                s.refreshToken},
        {"granted_scopes",               s.grantedScopes},
        {"access_token_expires_at",      s.accessTokenExpiresAt},
        {"auth_scope_version",           s.authScopeVersion},
        {"spotify_account_id",            s.spotifyAccountId},

        {"player_window_x",              s.playerWindowX},
        {"player_window_y",              s.playerWindowY},
        {"player_window_w",              s.playerWindowW},
        {"player_window_h",              s.playerWindowH},

        {"browser_window_open",          s.browserWindowOpen},
        {"browser_window_x",             s.browserWindowX},
        {"browser_window_y",             s.browserWindowY},
        {"browser_window_w",             s.browserWindowW},
        {"browser_window_h",             s.browserWindowH},
        {"discover_tab_playlists",        s.discoverTabPlaylists},
        {"discover_tab_top_tracks",       s.discoverTabTopTracks},
        {"discover_tab_top_artists",      s.discoverTabTopArtists},
        {"discover_tab_new_releases",     s.discoverTabNewReleases},
        {"discover_tab_saved_albums",     s.discoverTabSavedAlbums},
        {"discover_tab_podcasts",         s.discoverTabPodcasts},
        {"discover_tab_followed_artists", s.discoverTabFollowedArtists},
        {"discover_tab_saved_episodes",   s.discoverTabSavedEpisodes},
        {"discover_tab_audiobooks",       s.discoverTabAudiobooks},
        {"discover_tab_order",             s.discoverTabOrder},

        {"queue_window_open",            s.queueWindowOpen},
        {"queue_window_x",               s.queueWindowX},
        {"queue_window_y",               s.queueWindowY},
        {"queue_window_w",               s.queueWindowW},
        {"queue_window_h",               s.queueWindowH},

        {"search_window_open",           s.searchWindowOpen},
        {"search_window_x",              s.searchWindowX},
        {"search_window_y",              s.searchWindowY},
        {"search_window_w",              s.searchWindowW},
        {"search_window_h",              s.searchWindowH},

        {"artwork_window_open",          s.artworkWindowOpen},
        {"artwork_window_x",             s.artworkWindowX},
        {"artwork_window_y",             s.artworkWindowY},
        {"artwork_window_w",             s.artworkWindowW},
        {"artwork_window_h",             s.artworkWindowH},

        {"search_filter_all",            s.searchFilterAll},
        {"search_filter_tracks",         s.searchFilterTracks},
        {"search_filter_artists",        s.searchFilterArtists},
        {"search_filter_albums",         s.searchFilterAlbums},
        {"search_filter_playlists",      s.searchFilterPlaylists},
        {"search_filter_shows",          s.searchFilterShows},
        {"search_filter_episodes",       s.searchFilterEpisodes},
        {"search_filter_audiobooks",     s.searchFilterAudiobooks},
        {"audiobook_mode",               s.audiobookMode},

        {"playlist_window_x",            s.playlistWindowX},
        {"playlist_window_y",            s.playlistWindowY},
        {"playlist_window_w",            s.playlistWindowW},
        {"playlist_window_h",            s.playlistWindowH},

        {"librespot_path",               s.librespotPath},
        {"librespot_always_start",       s.librespotAlwaysStart},

        {"librespot_backend",            s.librespotBackend},
        {"librespot_bitrate",            s.librespotBitrate},
        {"librespot_volume",             s.librespotVolume},
        {"librespot_autoplay",           s.librespotAutoplay},
        {"haify_autoplay",               s.haifyAutoplay},
        {"librespot_normalization",      s.librespotNormalization},

        {"librespot_device_name",        s.librespotDeviceName},
        {"librespot_device_type",        s.librespotDeviceType},

        {"librespot_disable_discovery",  s.librespotDisableDiscovery},
        {"librespot_cache_path",         s.librespotCachePath},
        {"librespot_additional_args",    s.librespotAdditionalArgs},
        {"seekbar_use_system_color",     s.seekBarUseSystemColor},
        {"seekbar_color_red",            s.seekBarColorRed},
        {"seekbar_color_green",          s.seekBarColorGreen},
        {"seekbar_color_blue",           s.seekBarColorBlue},
        {"seekbar_color_alpha",          s.seekBarColorAlpha},
        {"deskbar_replicant_enabled",    s.deskbarReplicantEnabled},
        {"replicant_use_automatic_color", s.replicantUseAutomaticColor},
        {"replicant_color_red",          s.replicantColorRed},
        {"replicant_color_green",        s.replicantColorGreen},
        {"replicant_color_blue",         s.replicantColorBlue},
        {"replicant_color_alpha",        s.replicantColorAlpha},
        {"image_cache_limit_mb",         s.imageCacheLimitMB},
    };

    std::string path = SettingsPath();
    if (path.empty())
        return B_BAD_VALUE;

    std::string temporaryPath = path + ".tmp";
    std::string data = j.dump(4);
    int fd = open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return B_IO_ERROR;

    FILE* file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(temporaryPath.c_str());
        return B_IO_ERROR;
    }

    bool writeFailed = fwrite(data.data(), 1, data.size(), file) != data.size();
    bool flushFailed = fflush(file) != 0;
    bool syncFailed = fsync(fileno(file)) != 0;
    bool closeFailed = fclose(file) != 0;
    if (syncFailed || closeFailed) {
        unlink(temporaryPath.c_str());
        return B_IO_ERROR;
    }
    if (writeFailed || flushFailed) {
        unlink(temporaryPath.c_str());
        return B_IO_ERROR;
    }
    chmod(temporaryPath.c_str(), 0600);
    if (std::rename(temporaryPath.c_str(), path.c_str()) != 0) {
        unlink(temporaryPath.c_str());
        return B_IO_ERROR;
    }
    return B_OK;
}

status_t SettingsController::Update(
    const std::function<void(HaifySettings&)>& update)
{
    if (!update)
        return B_BAD_VALUE;

    BAutolock lock(&sSettingsLock);
    HaifySettings settings = Load();
    if (!sLastLoadValid)
        return B_BAD_DATA;
    update(settings);
    return Save(settings);
}
