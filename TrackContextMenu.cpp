#include "TrackContextMenu.h"
#include "Messages.h"
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
            auto playlists = fApi ? fApi->GetCachedPlaylists()
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

void
ShowPlayableItemContextMenu(const std::string& itemUri,
    const std::string& contextUri, BPoint screenPt, BMessenger win,
    SpotifyApi* api, bool libraryOnly, bool libraryStateKnown, bool saved)
{
    if (itemUri.empty()) return;
    bool isTrack = itemUri.find("spotify:track:") == 0;
    bool isEpisode = itemUri.find("spotify:episode:") == 0;
    if (!isTrack && !isEpisode) return;

    bool explicitSavedContext = (isTrack && contextUri == "spotify:collection")
        || (isEpisode && contextUri == "spotify:saved-episodes");
    if (explicitSavedContext) {
        libraryStateKnown = true;
        saved = true;
    }
    if (!libraryStateKnown && api) {
        api->CheckLibraryItems({itemUri}, [itemUri, contextUri, screenPt, win,
            libraryOnly](bool ok, const nlohmann::json& data) {
            BMessage result('iCmR');
            result.AddString("uri", itemUri.c_str());
            result.AddString("context_uri", contextUri.c_str());
            result.AddPoint("screen_point", screenPt);
            result.AddBool("library_only", libraryOnly);
            result.AddBool("saved", ok && data.is_array() && !data.empty()
                && data[0].is_boolean() && data[0].get<bool>());
            win.SendMessage(&result);
        });
        return;
    }

    BPopUpMenu* menu = new BPopUpMenu("playableItem", false, false);
    if (!libraryOnly) {
        BMessage* playMsg = new BMessage('tply');
        playMsg->AddString("trackUri", itemUri.c_str());
        menu->AddItem(new BMenuItem(B_TRANSLATE("Play"), playMsg));
        if (isEpisode) {
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

    if (saved) {
        BMessage* removeMessage = new BMessage('remL');
        removeMessage->AddString("trackUri", itemUri.c_str());
        menu->AddItem(new BMenuItem(isTrack
            ? B_TRANSLATE("Remove from Liked Songs")
            : B_TRANSLATE("Remove from Saved Episodes"), removeMessage));
    } else {
        BMessage* saveMessage = new BMessage('likT');
        saveMessage->AddString("trackUri", itemUri.c_str());
        menu->AddItem(new BMenuItem(isTrack
            ? B_TRANSLATE("Add to Liked Songs")
            : B_TRANSLATE("Save Episode"), saveMessage));
    }

    if (api && contextUri.find("spotify:playlist:") == 0) {
        std::string playlistId = contextUri.substr(17);
        for (const auto& playlist : api->GetCachedPlaylists()) {
            if (playlist.first == playlistId) {
                BMessage* removeMessage = new BMessage('remT');
                removeMessage->AddString("trackUri", itemUri.c_str());
                menu->AddItem(new BMenuItem(
                    B_TRANSLATE("Remove from Playlist"), removeMessage));
                break;
            }
        }
    }

    if (api) {
        menu->AddSeparatorItem();
        menu->AddItem(new PlaylistTargetMenu(api, itemUri));
    }

    BMenuItem* selected = menu->Go(screenPt, false, true);
    if (selected && selected->Message()) {
        BMessage* message = selected->Message();
        switch (message->what) {
            case 'addQ':
                if (api) api->AddToQueue(itemUri, nullptr);
                break;
            case 'likT':
                if (api) {
                    api->SaveLibraryItems({itemUri}, [itemUri](bool ok,
                            const nlohmann::json&) {
                        if (!ok)
                            return;
                        BMessage changed(MSG_LIBRARY_CHANGED);
                        changed.AddString("operation", "add");
                        changed.AddString("uri", itemUri.c_str());
                        be_app->PostMessage(&changed);
                    });
                }
                break;
            case 'addP':
                if (api) {
                    const char* playlistId = message->GetString("playlistId", "");
                    if (*playlistId)
                        api->AddTrackToPlaylist(playlistId, itemUri, nullptr);
                }
                break;
            default:
                win.SendMessage(message);
                break;
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
