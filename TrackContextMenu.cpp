#include "TrackContextMenu.h"
#include "Messages.h"
#include "MessageContracts.h"
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
                addMessage->AddString(MessageFields::PlaylistId, playlist.first.c_str());
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
    SpotifyApi* api, bool libraryOnly, const BMessage* commandContext)
{
    BMessage context;
    if (commandContext)
        context.AddMessage(MessageFields::CommandContext, commandContext);
    api->Library().CheckLibraryItems({itemUri}, [itemUri, contextUri,
        screenPt, win, libraryOnly, context](bool ok, const nlohmann::json& data) {
        BMessage result(context);
        result.what = 'iCmR';
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
    playMsg->AddString(MessageFields::TrackUri, itemUri.c_str());
    menu->AddItem(new BMenuItem(B_TRANSLATE("Play"), playMsg));
    if (itemKind == kSpotifyItemEpisode) {
        BMessage* openMsg = new BMessage('open');
        openMsg->AddString(MessageFields::Uri, itemUri.c_str());
        menu->AddItem(new BMenuItem(B_TRANSLATE("Open Details"), openMsg));
    }
    if (api) {
        BMessage* queueMsg = new BMessage(MessageContracts::MakeQueueCommand({itemUri}));
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
        case MSG_QUEUE_ITEM:
        {
            MessageContracts::QueueCommand command;
            if (api && MessageContracts::ReadQueueCommand(*message, command))
                api->Playback().AddToQueue(command.uri, nullptr);
            break;
        }
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
                const char* playlistId = message->GetString(MessageFields::PlaylistId, "");
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
    SpotifyApi* api, bool libraryOnly, bool libraryStateKnown, bool saved,
    const BMessage* commandContext)
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
            libraryOnly, commandContext);
        return;
    }

    BPopUpMenu* menu = BuildPlayableItemContextMenu(itemUri, contextUri,
        itemKind, api, libraryOnly, saved);
    BMenuItem* selected = menu->Go(screenPt, false, true);
    if (selected && selected->Message()) {
        if (commandContext) {
            // The caller owns these commands and checks their context before
            // executing side effects. Copy before destroying the popup menu.
            BMessage command(*selected->Message());
            command.AddMessage(MessageFields::CommandContext, commandContext);
            win.SendMessage(&command);
        } else {
            HandlePlayableMenuSelection(selected->Message(), itemUri, win, api);
        }
    }
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
