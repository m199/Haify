NAME = Haify
TYPE = APP
APP_MIME_SIG = application/x-vnd.Haify
SRCS = App.cpp PlayerWindow.cpp NowPlayingItem.cpp NowPlayingItemMapper.cpp PlaylistWindow.cpp DiscoverWindow.cpp ArtistWindow.cpp EpisodeWindow.cpp AudiobookWindow.cpp QueueWindow.cpp SearchWindow.cpp SettingsWindow.cpp \
	DeskbarReplicantView.cpp \
	ArtworkReplicantView.cpp ArtworkWindow.cpp \
	PlayerBarView.cpp PlaybackSeekBarView.cpp IconButtonView.cpp ClickableLabelView.cpp \
	ArtworkView.cpp MediaDescriptionView.cpp DescriptionTextFormatter.cpp DiscoverListView.cpp TrackContextMenu.cpp TextInputDialog.cpp \
	network/HttpClient.cpp network/OAuthCallbackServer.cpp network/ImageCache.cpp \
	playlist/PlaylistContent.cpp \
	playlist/PlaylistTrackListView.cpp \
	playlist/PlaylistTrackRow.cpp \
	playlist/PlaylistCacheRows.cpp \
	playlist/PlaylistEpisodeRows.cpp \
	playlist/PlaylistCacheDocument.cpp \
	playlist/PlaylistEpisode.cpp \
	playlist/PlaylistCacheFiles.cpp \
	spotify/auth/SpotifyAuth.cpp spotify/api/SpotifyApi.cpp spotify/api/ArtistApi.cpp spotify/api/ContentApi.cpp spotify/api/LibraryApi.cpp spotify/api/PlaybackApi.cpp spotify/api/PlaylistApi.cpp spotify/api/ProfileApi.cpp spotify/api/SpotifyRequestClient.cpp spotify/api/SpotifyResponse.cpp spotify/api/SpotifyUrl.cpp spotify/SpotifyCapabilities.cpp \
	settings/SettingsController.cpp
RDEFS = Haify.rdef
RSRCS =
LIBS = be translation tracker network netservices bnetapi shared localestub stdc++ columnlistview
LOCALES = en de
LIBPATHS =
LOCAL_INCLUDE_PATHS = \
    . \
    network \
    settings \
    spotify/auth

SYSTEM_INCLUDE_PATHS = \
	/boot/system/develop/headers/private/interface \
	/boot/system/develop/headers/private/netservices \
	/boot/system/develop/headers/private/shared
OPTIMIZE := FULL

include /boot/system/develop/etc/makefile-engine
