#!/bin/sh
# Prepared for the final Phase 3 verification on Haiku. Do not run before
# build/test permission; this script compiles fixtures but never starts Haify.
set -eu
cd "$(dirname "$0")/.."
test_output="${HAIFY_TEST_OUTPUT:-${TMPDIR:-/tmp}/haify-phase3-tests}"
mkdir -p "$test_output"
cxx="${CXX:-c++}"

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
"$cxx" -std=c++17 -Wall -Wextra -Werror -Wno-multichar -I. tests/MessageContractsTest.cpp -lbe -o "$test_output/message-contracts"
"$test_output/message-contracts"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. -Inetwork tests/SpotifyRequestClientTest.cpp spotify/api/SpotifyRequestClient.cpp -lbe -o "$test_output/request-client"
"$test_output/request-client"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/HttpRequestCompletionTest.cpp -o "$test_output/http-completion"
"$test_output/http-completion"
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. tests/PlaybackApiTest.cpp spotify/api/PlaybackApi.cpp spotify/api/SpotifyUrl.cpp -o "$test_output/playback-api"
"$test_output/playback-api"
# PlaylistApiTest supplies an unavailable cache path; real user files are never touched.
"$cxx" -std=c++17 -Wall -Wextra -Werror -I. -Isettings tests/PlaylistApiTest.cpp spotify/api/PlaylistApi.cpp spotify/api/SpotifyUrl.cpp -lbe -o "$test_output/playlist-api"
"$test_output/playlist-api"
