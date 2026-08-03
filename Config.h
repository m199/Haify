#pragma once

#define HAIFY_APP_NAME      "Haify"
#define HAIFY_MIME_SIG      "application/x-vnd.Haify"
#define HAIFY_APP_VERSION   "1.0.0"
#define HAIFY_SETTINGS_FILE "Haify_settings"

#define HAIFY_CLIENT_ID     "006877f3073c4796a6e7dedc31aadd46"
#define HAIFY_AUTH_SCOPE_VERSION 2

#define SPOTIFY_API_BASE    "https://api.spotify.com/v1"
#define SPOTIFY_AUTH_URL    "https://accounts.spotify.com/authorize"
#define SPOTIFY_TOKEN_URL   "https://accounts.spotify.com/api/token"
#define SPOTIFY_REDIRECT    "http://127.0.0.1:8765/callback"
#define SPOTIFY_SCOPES      "user-read-playback-state user-modify-playback-state " \
                            "user-read-currently-playing playlist-read-private " \
                            "playlist-read-collaborative user-library-read " \
                            "user-library-modify playlist-modify-public " \
                            "playlist-modify-private user-follow-read user-follow-modify " \
                            "user-read-recently-played user-top-read " \
                            "user-read-private user-read-playback-position " \
                            "ugc-image-upload"

#define SPOTIFY_REQUIRED_SCOPES \
                            "user-read-playback-state user-modify-playback-state " \
                            "user-read-currently-playing playlist-read-private " \
                            "playlist-read-collaborative user-library-read " \
                            "user-library-modify playlist-modify-public " \
                            "playlist-modify-private user-follow-read user-follow-modify " \
                            "user-read-recently-played user-top-read user-read-private"

#define LIBRESPOT_DEVICE_NAME "Haify"
