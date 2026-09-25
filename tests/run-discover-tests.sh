#!/bin/sh
# Phase 3/4/5/6 regression tests on Haiku. Do not run before
# build/test permission; this script compiles fixtures but never starts Haify.
set -eu
cd "$(dirname "$0")/.."
test_output="${HAIFY_TEST_OUTPUT:-${TMPDIR:-/tmp}/haify-phase3-tests}"
mkdir -p "$test_output"
cxx="${CXX:-c++}"

# Phase 6: version formatting uses native metadata types, no application or file I/O.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/AppVersionTest.cpp -o "$test_output/app-version"
"$test_output/app-version"

"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/DiscoverTabPolicyTest.cpp -o "$test_output/tab-policy"
"$test_output/tab-policy"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/DiscoverRowFactoryTest.cpp discover/DiscoverRowFactory.cpp -o "$test_output/row-factory"
"$test_output/row-factory"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/DiscoverStateTest.cpp discover/DiscoverLibraryChangeController.cpp discover/DiscoverPlaylistMutationController.cpp -o "$test_output/state"
"$test_output/state"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/DiscoverCacheDocumentTest.cpp discover/DiscoverCacheDocument.cpp -o "$test_output/cache-document"
"$test_output/cache-document"
# Multi-character literals are the existing Haiku BMessage wire codes.
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/DiscoverMessagesTest.cpp discover/DiscoverMessages.cpp -lbe -o "$test_output/discover-messages"
"$test_output/discover-messages"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/MessageContractsTest.cpp playback/PlaybackDeviceResolver.cpp -lbe -llocalestub -o "$test_output/message-contracts"
"$test_output/message-contracts"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. -Inetwork tests/SpotifyRequestClientTest.cpp spotify/api/SpotifyRequestClient.cpp -lbe -o "$test_output/request-client"
"$test_output/request-client"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/HttpRequestCompletionTest.cpp -o "$test_output/http-completion"
"$test_output/http-completion"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaybackApiTest.cpp spotify/api/PlaybackApi.cpp spotify/api/SpotifyUrl.cpp -o "$test_output/playback-api"
"$test_output/playback-api"
# Phase 5a: start policy and asynchronous shuffle/play sequences, without a window.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaybackStartControllerTest.cpp playback/PlaybackStartController.cpp spotify/api/PlaybackApi.cpp spotify/api/SpotifyUrl.cpp -o "$test_output/playback-start-controller"
"$test_output/playback-start-controller"
# Local startup: stale event files, track identity and transfer-before-play.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/LocalPlaybackStateTest.cpp playback/PlaybackStartController.cpp spotify/api/PlaybackApi.cpp spotify/api/SpotifyUrl.cpp -o "$test_output/local-playback-state"
"$test_output/local-playback-state"
# Phase 5b: discovery/idle/transfer ordering and strict native completion messages.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/LibrespotTransferControllerTest.cpp playback/LibrespotTransferController.cpp spotify/api/PlaybackApi.cpp spotify/api/SpotifyUrl.cpp -o "$test_output/librespot-transfer-controller"
"$test_output/librespot-transfer-controller"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/LibrespotTransferMessagesTest.cpp playback/LibrespotTransferMessages.cpp playback/LibrespotTransferController.cpp spotify/api/PlaybackApi.cpp spotify/api/SpotifyUrl.cpp -lbe -o "$test_output/librespot-transfer-messages"
"$test_output/librespot-transfer-messages"
# Librespot option mapping without settings I/O or a process.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/LibrespotArgumentsTest.cpp playback/LibrespotArguments.cpp -o "$test_output/librespot-arguments"
"$test_output/librespot-arguments"
# Phase 5c: navigation decisions, owned probe results and native wire contracts.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/SpotifyNavigationTest.cpp navigation/SpotifyNavigation.cpp spotify/api/ContentApi.cpp spotify/api/SpotifyUrl.cpp -o "$test_output/spotify-navigation"
"$test_output/spotify-navigation"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/SpotifyNavigationMessagesTest.cpp navigation/SpotifyNavigationMessages.cpp navigation/SpotifyNavigation.cpp spotify/api/ContentApi.cpp spotify/api/SpotifyUrl.cpp -lbe -o "$test_output/spotify-navigation-messages"
"$test_output/spotify-navigation-messages"
# Phase 5d: actual API/cache services with in-memory settings and forbidden network I/O.
session_api_sources="spotify/api/SpotifyApi.cpp spotify/api/SpotifyRequestClient.cpp spotify/api/ArtistApi.cpp spotify/api/ContentApi.cpp spotify/api/LibraryApi.cpp spotify/api/PlaybackApi.cpp spotify/api/PlaylistApi.cpp spotify/api/ProfileApi.cpp spotify/api/SpotifyResponse.cpp spotify/api/SpotifyUrl.cpp"
session_sources="spotify/session/SpotifyAccountSession.cpp spotify/session/SpotifyCredentialStore.cpp spotify/SpotifyCapabilities.cpp tests/SpotifySessionTestSupport.cpp"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. -Isettings -Inetwork tests/SpotifyAccountSessionTest.cpp $session_sources $session_api_sources -lbe -o "$test_output/spotify-account-session"
"$test_output/spotify-account-session"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. -Isettings -Inetwork tests/SpotifyCapabilitiesTest.cpp $session_sources $session_api_sources -lbe -o "$test_output/spotify-capabilities"
"$test_output/spotify-capabilities"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. -Isettings -Inetwork tests/SpotifySessionMessagesTest.cpp spotify/session/SpotifySessionMessages.cpp $session_sources $session_api_sources -lbe -o "$test_output/spotify-session-messages"
"$test_output/spotify-session-messages"
# PlaylistApiTest supplies an unavailable cache path; real user files are never touched.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. -Isettings tests/PlaylistApiTest.cpp spotify/api/PlaylistApi.cpp spotify/api/SpotifyUrl.cpp -lbe -o "$test_output/playlist-api"
"$test_output/playlist-api"
# Phase 4: pure page mapping/routing and the existing Haiku row wire contract.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistPageControllerTest.cpp playlist/PlaylistPageController.cpp playlist/PlaylistContent.cpp -o "$test_output/playlist-page-controller"
"$test_output/playlist-page-controller"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/PlaylistPageMessagesTest.cpp playlist/PlaylistPageMessages.cpp -lbe -o "$test_output/playlist-page-messages"
"$test_output/playlist-page-messages"
# Phase 4b: metadata reads, ownership state and typed message validation.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistMetadataControllerTest.cpp playlist/PlaylistMetadataController.cpp -o "$test_output/playlist-metadata-controller"
"$test_output/playlist-metadata-controller"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/PlaylistMetadataMessagesTest.cpp playlist/PlaylistMetadataMessages.cpp -lbe -o "$test_output/playlist-metadata-messages"
"$test_output/playlist-metadata-messages"
# Phase 4c: paging/search transitions without a window, timer or live API.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistPageStateTest.cpp playlist/PlaylistPageState.cpp playlist/PlaylistPageController.cpp playlist/PlaylistContent.cpp -o "$test_output/playlist-page-state"
"$test_output/playlist-page-state"
# Description parsing and link routing; no window, browser or mail app is opened.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/DescriptionTextFormatterTest.cpp ui/DescriptionTextFormatter.cpp -lbe -o "$test_output/description-text-formatter"
"$test_output/description-text-formatter"
# Phase 4d: removal state/dispatch and typed completion, without live writes.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistRemovalControllerTest.cpp playlist/PlaylistRemovalController.cpp playlist/PlaylistPageState.cpp playlist/PlaylistPageController.cpp playlist/PlaylistContent.cpp -o "$test_output/playlist-removal-controller"
"$test_output/playlist-removal-controller"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/PlaylistRemovalMessagesTest.cpp playlist/PlaylistRemovalMessages.cpp -lbe -o "$test_output/playlist-removal-messages"
"$test_output/playlist-removal-messages"
# Phase 4d.2: reorder policy/state/rollback and strict result messages.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistReorderControllerTest.cpp playlist/PlaylistReorderController.cpp -o "$test_output/playlist-reorder-controller"
"$test_output/playlist-reorder-controller"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/PlaylistReorderMessagesTest.cpp playlist/PlaylistReorderMessages.cpp -lbe -o "$test_output/playlist-reorder-messages"
"$test_output/playlist-reorder-messages"
# Phase 4d.3: clear/add state and strict results; no live playlist changes.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistWriteControllerTest.cpp playlist/PlaylistWriteController.cpp -o "$test_output/playlist-write-controller"
"$test_output/playlist-write-controller"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/PlaylistWriteMessagesTest.cpp playlist/PlaylistWriteMessages.cpp -lbe -o "$test_output/playlist-write-messages"
"$test_output/playlist-write-messages"
# Phase 4e: cover preparation/upload outcomes and presentation policy.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistCoverControllerTest.cpp playlist/PlaylistCoverController.cpp -o "$test_output/playlist-cover-controller"
"$test_output/playlist-cover-controller"
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/PlaylistCoverMessagesTest.cpp playlist/PlaylistCoverMessages.cpp -lbe -o "$test_output/playlist-cover-messages"
"$test_output/playlist-cover-messages"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistPresentationTest.cpp playlist/PlaylistContent.cpp -o "$test_output/playlist-presentation"
"$test_output/playlist-presentation"
# Server coordinates remain correct when unavailable items are omitted from UI.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaylistReorderPositionsTest.cpp playlist/PlaylistReorderController.cpp -o "$test_output/playlist-reorder-positions"
"$test_output/playlist-reorder-positions"
# Phase 7a: font scaling and shared player geometry, without a Haiku runtime.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/UiMetricsTest.cpp -o "$test_output/ui-metrics"
"$test_output/ui-metrics"
# Phase 7d: native text style/selection preservation; requires Haiku app_server.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/MediaWindowScaleTest.cpp -lbe -o "$test_output/media-window-scale"
"$test_output/media-window-scale"
# Phase 7e: ColumnListView row identity/rollback, requires Haiku app_server.
# The fixture supplies SettingsController::Load() with in-memory defaults.
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. -I/boot/system/develop/headers/private/interface tests/ListRowScaleTest.cpp ui/views/FontScaledListView.cpp ui/views/DiscoverListView.cpp playlist/PlaylistTrackRow.cpp -lbe -lcolumnlistview -o "$test_output/list-row-scale"
"$test_output/list-row-scale"
