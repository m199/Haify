#include "TrackContextMenu.h"
#include "Messages.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"

#include <Application.h>
#include <Catalog.h>
#include <Menu.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <utility>
#include <vector>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TrackContextMenu"

class PlaylistTargetMenu : public BMenu {
public:
    PlaylistTargetMenu(SpotifyApi* api, const std::string& itemUri)
        : BMenu(B_TRANSLATE("Add to Playlist")),
          fApi(api), fItemUri(itemUri)
    {
    }

    virtual void AttachedToWindow()
    {
        if (!fBuilt) {
            fBuilt = true;
            auto playlists = fApi ? fApi->Playlists().GetCachedPlaylists()
                : std::vector<std::pair<std::string, std::string>>();
            for (const auto& playlist : playlists) {
                BMessage* addMessage = new BMessage('addP');
                addMessage->AddString("trackUri", fItemUri.c_str());
                addMessage->AddString("playlistId", playlist.first.c_str());
                AddItem(new BMenuItem(playlist.second.c_str(), addMessage));
            }
            if (playlists.empty()) {
                BMenuItem* empty = new BMenuItem(
                    B_TRANSLATE("No writable playlists"), nullptr);
                empty->SetEnabled(false);
                AddItem(empty);
            }
        }
        BMenu::AttachedToWindow();
    }

private:
    SpotifyApi* fApi;
    std::string fItemUri;
    bool fBuilt = false;
};

static bool
HasExplicitSavedContext(SpotifyItemKind itemKind,
    const std::string& contextUri)
{
    return (itemKind == kSpotifyItemTrack
            && contextUri == "spotify:collection")
        || (itemKind == kSpotifyItemEpisode
            && contextUri == "spotify:saved-episodes");
}

static void
RequestPlayableLibraryState(const std::string& itemUri,
    const std::string& contextUri, BPoint screenPt, BMessenger win,
    SpotifyApi* api, bool libraryOnly)
{
    api->Library().CheckLibraryItems({itemUri}, [itemUri, contextUri,
        screenPt, win, libraryOnly](bool ok, const nlohmann::json& data) {
        BMessage result('iCmR');
        result.AddString("uri", itemUri.c_str());
        result.AddString("context_uri", contextUri.c_str());
        result.AddPoint("screen_point", screenPt);
        result.AddBool("library_only", libraryOnly);
        result.AddBool("saved", ok && data.is_array() && !data.empty()
            && data[0].is_boolean() && data[0].get<bool>());
        win.SendMessage(&result);
    });
}

static void
AddPlayableMenuItems(BPopUpMenu* menu, const std::string& itemUri,
    SpotifyItemKind itemKind, SpotifyApi* api)
{
    BMessage* playMsg = new BMessage('tply');
    playMsg->AddString("trackUri", itemUri.c_str());
    menu->AddItem(new BMenuItem(B_TRANSLATE("Play"), playMsg));
    if (itemKind == kSpotifyItemEpisode) {
        BMessage* openMsg = new BMessage('open');
        openMsg->AddString("uri", itemUri.c_str());
        menu->AddItem(new BMenuItem(B_TRANSLATE("Open Details"), openMsg));
    }
    if (api) {
        BMessage* queueMsg = new BMessage('addQ');
        queueMsg->AddString("trackUri", itemUri.c_str());
        menu->AddItem(new BMenuItem(B_TRANSLATE("Add to Queue"), queueMsg));
    }
    menu->AddSeparatorItem();
}

static const char*
LibraryMenuLabel(SpotifyItemKind itemKind, bool saved)
{
    if (itemKind == kSpotifyItemTrack)
        return saved ? B_TRANSLATE("Remove from Liked Songs")
            : B_TRANSLATE("Add to Liked Songs");
    return saved ? B_TRANSLATE("Remove from Saved Episodes")
        : B_TRANSLATE("Save Episode");
}

static void
AddLibraryMenuItem(BPopUpMenu* menu, const std::string& itemUri,
    SpotifyItemKind itemKind, bool saved)
{
    BMessage* message = new BMessage(saved ? 'remL' : 'likT');
    message->AddString("trackUri", itemUri.c_str());
    menu->AddItem(new BMenuItem(LibraryMenuLabel(itemKind, saved), message));
}

static bool
PlaylistContainsContext(SpotifyApi* api, const std::string& contextUri)
{
    if (!api || SpotifyItemKindForUri(contextUri) != kSpotifyItemPlaylist)
        return false;

    std::string playlistId = SpotifyItemIdForUri(contextUri);
    for (const auto& playlist : api->Playlists().GetCachedPlaylists()) {
        if (playlist.first == playlistId)
            return true;
    }
    return false;
}

static void
AddPlaylistRemovalItem(BPopUpMenu* menu, const std::string& itemUri)
{
    BMessage* removeMessage = new BMessage('remT');
    removeMessage->AddString("trackUri", itemUri.c_str());
    menu->AddItem(new BMenuItem(B_TRANSLATE("Remove from Playlist"),
        removeMessage));
}

static BPopUpMenu*
BuildPlayableItemContextMenu(const std::string& itemUri,
    const std::string& contextUri, SpotifyItemKind itemKind, SpotifyApi* api,
    bool libraryOnly, bool saved)
{
    BPopUpMenu* menu = new BPopUpMenu("playableItem", false, false);
    if (!libraryOnly)
        AddPlayableMenuItems(menu, itemUri, itemKind, api);
    AddLibraryMenuItem(menu, itemUri, itemKind, saved);
    if (PlaylistContainsContext(api, contextUri))
        AddPlaylistRemovalItem(menu, itemUri);
    if (api) {
        menu->AddSeparatorItem();
        menu->AddItem(new PlaylistTargetMenu(api, itemUri));
    }
    return menu;
}

static void
NotifyLibraryAdd(const std::string& itemUri, bool ok)
{
    if (!ok)
        return;
    BMessage changed(MSG_LIBRARY_CHANGED);
    changed.AddString("operation", "add");
    changed.AddString("uri", itemUri.c_str());
    be_app->PostMessage(&changed);
}

static void
HandlePlayableMenuSelection(BMessage* message, const std::string& itemUri,
    BMessenger win, SpotifyApi* api)
{
    if (!message)
        return;

    switch (message->what) {
        case 'addQ':
            if (api)
                api->Playback().AddToQueue(itemUri, nullptr);
            break;
        case 'likT':
            if (api) {
                api->Library().SaveLibraryItems({itemUri}, [itemUri](
                        bool ok, const nlohmann::json&) {
                    NotifyLibraryAdd(itemUri, ok);
                });
            }
            break;
        case 'addP':
            if (api) {
                const char* playlistId = message->GetString("playlistId", "");
                if (*playlistId) {
                    api->Playlists().AddTrackToPlaylist(playlistId, itemUri,
                        nullptr);
                }
            }
            break;
        default:
            win.SendMessage(message);
            break;
    }
}

void
ShowPlayableItemContextMenu(const std::string& itemUri,
    const std::string& contextUri, BPoint screenPt, BMessenger win,
    SpotifyApi* api, bool libraryOnly, bool libraryStateKnown, bool saved)
{
    if (itemUri.empty()) return;
    SpotifyItemKind itemKind = SpotifyItemKindForUri(itemUri);
    if (!SpotifyItemIsPlayable(itemKind)) return;

    if (HasExplicitSavedContext(itemKind, contextUri)) {
        libraryStateKnown = true;
        saved = true;
    }
    if (!libraryStateKnown && api) {
        RequestPlayableLibraryState(itemUri, contextUri, screenPt, win, api,
            libraryOnly);
        return;
    }

    BPopUpMenu* menu = BuildPlayableItemContextMenu(itemUri, contextUri,
        itemKind, api, libraryOnly, saved);
    BMenuItem* selected = menu->Go(screenPt, false, true);
    if (selected)
        HandlePlayableMenuSelection(selected->Message(), itemUri, win, api);
    delete menu;
}

void
ShowTrackContextMenu(const std::string& trackUri,
    const std::string& contextUri, BPoint screenPt, BMessenger windowTarget,
    SpotifyApi* api, bool libraryOnly)
{
    ShowPlayableItemContextMenu(trackUri, contextUri, screenPt, windowTarget,
        api, libraryOnly, false, false);
}
