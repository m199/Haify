NAME = Haify
TYPE = APP
APP_MIME_SIG = application/x-vnd.Haify
SRCS = App.cpp PlayerWindow.cpp NowPlayingItem.cpp NowPlayingItemMapper.cpp PlaylistWindow.cpp DiscoverWindow.cpp ArtistWindow.cpp EpisodeWindow.cpp AudiobookWindow.cpp QueueWindow.cpp SearchWindow.cpp SettingsWindow.cpp \
	playback/LibrespotArguments.cpp \
	navigation/SpotifyNavigation.cpp navigation/SpotifyNavigationMessages.cpp \
	spotify/session/SpotifyAccountSession.cpp spotify/session/SpotifyCredentialStore.cpp \
	spotify/session/SpotifySessionMessages.cpp \
	DeskbarReplicantView.cpp \
	ArtworkReplicantView.cpp ArtworkWindow.cpp \
	PlayerBarView.cpp PlaybackSeekBarView.cpp PlaybackDevicePromptWindow.cpp PlaybackDeviceResolver.cpp IconButtonView.cpp ClickableLabelView.cpp \
	ArtworkView.cpp MediaDescriptionView.cpp DescriptionTextFormatter.cpp DiscoverListView.cpp TrackContextMenu.cpp TextInputDialog.cpp \
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
