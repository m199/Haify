# Haify
[![Platform: Haiku](https://img.shields.io/badge/platform-Haiku-yellow.svg)](https://www.haiku-os.org/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Spotify](https://img.shields.io/badge/Spotify-Web%20API-1DB954?logo=spotify&logoColor=white)](https://developer.spotify.com/documentation/web-api)
[![AI Assisted](https://img.shields.io/badge/AI%20assisted-OpenAI%20Codex-black)](https://openai.com/codex/)

A Spotify WebAPI client for Haiku.

Spotify meets the year 2000. This is a little homage to my all-time favorite audio player.

<img width="531" height="117" alt="grafik" src="https://github.com/user-attachments/assets/285e6f67-6380-4a07-9c83-f472c9d4b99c" />

## Building

Install `nlohmann_json` from HaikuDepot and run:

```sh
git clone https://github.com/m199/Haify
cd Haify
make
```

## First start: your own Spotify app

Spotify only lets a handful of people use one app registration and no longer
grants larger quotas to individual developers. Every Haify user therefore
creates a free registration of their own, called a **Client ID**.

On first start Haify opens the **Spotify Setup** assistant and walks you 
through it in about two minutes.

1. Open the Spotify Developer Dashboard and log in with your Spotify account.
2. Click **Create app**. Any name and description work. Add the Redirect URI
   `http://127.0.0.1:8765/callback` (the assistant can copy it for you) and
   tick **Web API**.
3. Copy the **Client ID** or simply the address of your app's dashboard page
   and switch back to Haify. It picks the ID up from the clipboard. No client
   secret is needed!
4. Click **Finish** and allow Haify access in your browser.

Spotify requires **Premium** for the account that owns the registration.

## Using Haify

Browse your library, search, edit playlists and control playback through the
Spotify Web API. Without librespot, playback commands go to the currently
active Spotify Connect device (for example a network speaker).

- **Sign in / Sign out:** Settings → Spotify → Account.
- **Refresh:** Haify polls Spotify only every 15 seconds (60 seconds after a
  longer pause) to spare Spotify's servers. Use **File → Refresh** in the
  player, playlist, album or show window to see changes made on other
  devices right away.

## Local playback with librespot

Local audio output on the Haiku computer requires
[librespot](https://github.com/m199/librespot) and a **Spotify Premium
account**. Open **Settings → Librespot**, select the librespot executable and
click **Start librespot**. Enable **Always start librespot on launch** if Haify
should provide local playback automatically.

Registering librespot is separate from signing in to Haify and maybe not needed in some 
Networks. Haify's login authorizes Web API access. Librespot registration authorizes 
the local audio device. Registration does not give Haify access to a Spotify password.

Do not **Disable Zeroconf/mDNS discovery** before registration unless
librespot already has valid cached credentials. Otherwise the local device
cannot authenticate. Playback metadata and artwork always come from the Spotify
Web API.

Icons used from https://hvif-store.art/
