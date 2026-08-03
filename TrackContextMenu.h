#pragma once

#include <Messenger.h>
#include <Point.h>
#include <string>

class SpotifyApi;

void ShowPlayableItemContextMenu(
    const std::string& itemUri,
    const std::string& contextUri,
    BPoint             screenPt,
    BMessenger         windowTarget,
    SpotifyApi*        api,
    bool               libraryOnly = false,
    bool               libraryStateKnown = false,
    bool               saved = false
);

// Compatibility entry point for existing callers. It now supports tracks and
// episodes.
void ShowTrackContextMenu(
    const std::string& trackUri,
    const std::string& contextUri,
    BPoint             screenPt,
    BMessenger         windowTarget,
    SpotifyApi*        api,
    bool               libraryOnly = false
);
