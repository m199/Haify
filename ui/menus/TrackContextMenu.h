#pragma once

#include <Messenger.h>
#include <Point.h>
#include <string>

class SpotifyApi;
class BMessage;

void ShowPlayableItemContextMenu(
    const std::string& itemUri,
    const std::string& contextUri,
    BPoint             screenPt,
    BMessenger         windowTarget,
    SpotifyApi*        api,
    bool               libraryOnly = false,
    bool               libraryStateKnown = false,
    bool               saved = false,
    const BMessage*    commandContext = nullptr
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
