# Haify
[![Platform: Haiku](https://img.shields.io/badge/platform-Haiku-yellow.svg)](https://www.haiku-os.org/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Spotify](https://img.shields.io/badge/Spotify-Web%20API-1DB954?logo=spotify&logoColor=white)](https://developer.spotify.com/documentation/web-api)
[![AI Assisted](https://img.shields.io/badge/AI%20assisted-OpenAI%20Codex-black)](https://openai.com/codex/)

A Spotify WebAPI client for Haiku.

Spotify meets the year 2000. This is a little homage to my all-time favorite audio player.

<img width="531" height="117" alt="grafik" src="https://github.com/user-attachments/assets/285e6f67-6380-4a07-9c83-f472c9d4b99c" />

To build Haify, install `nlohmann_json` from HaikuDepot and run:

```sh
git clone https://github.com/m199/Haify
cd Haify
make
```

## Using Haify

Sign in to Spotify from Haify to browse your library, search, edit playlists,
and control playback through the Spotify Web API. Without librespot, playback
commands are sent to the currently active Spotify Connect device (like Network Speaker)

Local audio output on the Haiku computer requires
[librespot](https://github.com/m199/librespot) and a **Spotify Premium
account**. Open **Settings -> Librespot**, select the librespot executable and click **Start librespot**. 
Enable **Always start librespot on launch** if Haify should provide local playback automatically.

### Local playback with librespot
Registering librespot is separate from signing in to Haify. Haify's login
authorizes Web API access. Librespot registration authorizes the local audio
device. Registration does not give Haify access to a Spotify password.

Do not **Disable Zeroconf/mDNS discovery** before registration unless
librespot already has valid cached credentials. Otherwise the local device
cannot authenticate. Playback metadata and artwork always come from the Spotify
Web API. 

Icons used form https://hvif-store.art/

