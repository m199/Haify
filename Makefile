NAME = Haify
TYPE = APP
APP_MIME_SIG = application/x-vnd.Haify
SRCS = app/App.cpp ui/windows/PlayerWindow.cpp playback/NowPlayingItem.cpp playback/NowPlayingItemMapper.cpp ui/windows/PlaylistWindow.cpp ui/windows/DiscoverWindow.cpp ui/windows/ArtistWindow.cpp ui/windows/EpisodeWindow.cpp ui/windows/AudiobookWindow.cpp ui/windows/QueueWindow.cpp ui/windows/SearchWindow.cpp ui/windows/SettingsWindow.cpp \
	playback/LibrespotArguments.cpp \
	navigation/SpotifyNavigation.cpp navigation/SpotifyNavigationMessages.cpp \
	spotify/session/SpotifyAccountSession.cpp spotify/session/SpotifyCredentialStore.cpp \
	spotify/session/SpotifySessionMessages.cpp \
	ui/replicants/DeskbarReplicantView.cpp \
	ui/views/FontScaledListView.cpp \
	ui/replicants/ArtworkReplicantView.cpp ui/windows/ArtworkWindow.cpp \
	ui/views/PlayerBarView.cpp ui/views/PlaybackSeekBarView.cpp ui/dialogs/PlaybackDevicePromptWindow.cpp playback/PlaybackDeviceResolver.cpp ui/views/IconButtonView.cpp ui/views/ClickableLabelView.cpp \
	ui/views/ArtworkView.cpp ui/views/MediaDescriptionView.cpp ui/DescriptionTextFormatter.cpp ui/views/DiscoverListView.cpp ui/menus/TrackContextMenu.cpp ui/dialogs/TextInputDialog.cpp \
	network/HttpClient.cpp network/OAuthCallbackServer.cpp network/ImageCache.cpp \
	playback/PlaybackStartController.cpp \
	playback/LibrespotTransferController.cpp \
	playback/LibrespotTransferMessages.cpp \
	discover/DiscoverRowFactory.cpp \
	playlist/PlaylistContent.cpp \
	playlist/PlaylistMetadataController.cpp \
	playlist/PlaylistMetadataMessages.cpp \
	playlist/PlaylistMetadataRequests.cpp \
	playlist/PlaylistPageController.cpp \
	playlist/PlaylistPageMessages.cpp \
	playlist/PlaylistPageRequests.cpp \
	playlist/PlaylistPageState.cpp \
	playlist/PlaylistRemovalController.cpp \
	playlist/PlaylistRemovalMessages.cpp \
	playlist/PlaylistRemovalRequests.cpp \
	playlist/PlaylistReorderController.cpp \
	playlist/PlaylistReorderMessages.cpp \
	playlist/PlaylistReorderRequests.cpp \
	playlist/PlaylistWriteController.cpp \
	playlist/PlaylistWriteMessages.cpp \
	playlist/PlaylistWriteRequests.cpp \
	playlist/PlaylistCoverController.cpp \
	playlist/PlaylistCoverMessages.cpp \
	playlist/PlaylistCoverRequests.cpp \
	playlist/PlaylistTrackListView.cpp \
	playlist/PlaylistTrackRow.cpp \
	playlist/PlaylistCacheRows.cpp \
	playlist/PlaylistEpisodeRows.cpp \
	playlist/PlaylistCacheDocument.cpp \
	playlist/PlaylistEpisode.cpp \
	playlist/PlaylistCacheFiles.cpp \
	spotify/auth/SpotifyAuth.cpp spotify/api/SpotifyApi.cpp spotify/api/ArtistApi.cpp spotify/api/ContentApi.cpp spotify/api/LibraryApi.cpp spotify/api/PlaybackApi.cpp spotify/api/PlaylistApi.cpp spotify/api/ProfileApi.cpp spotify/api/SpotifyRequestClient.cpp spotify/api/SpotifyResponse.cpp spotify/api/SpotifyUrl.cpp spotify/SpotifyCapabilities.cpp \
	discover/DiscoverCacheDocument.cpp \
	discover/DiscoverMessages.cpp \
	discover/DiscoverCacheRepository.cpp \
	discover/DiscoverLibraryChangeController.cpp \
	discover/DiscoverLibraryRequests.cpp \
	discover/DiscoverPlaylistMutationController.cpp \
	discover/DiscoverPlaylistRequests.cpp \
	settings/SettingsController.cpp
RDEFS = resources/Haify.rdef
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
