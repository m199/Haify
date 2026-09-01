#include "EpisodeWindow.h"

#include "App.h"
#include "ArtworkView.h"
#include "ClickableLabelView.h"
#include "Messages.h"
#include "NowPlayingFields.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"

#include <Alignment.h>
#include <Application.h>
#include <Catalog.h>
#include <Font.h>
#include <GroupView.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <StringView.h>
#include <String.h>
#include <TextView.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "EpisodeWindow"

static std::string
EpisodeJsonString(const nlohmann::json& object, const char* key,
    const char* fallback = "")
{
    if (!object.is_object() || !object.contains(key)
            || !object[key].is_string())
        return fallback;
    return object[key].get<std::string>();
}

static bool
EpisodeJsonBool(const nlohmann::json& object, const char* key,
    bool fallback = false)
{
    if (!object.is_object() || !object.contains(key)
            || !object[key].is_boolean())
        return fallback;
    return object[key].get<bool>();
}

static void
AddEpisodeShowFields(BMessage& message, const nlohmann::json& episode)
{
    if (!episode.contains("show") || !episode["show"].is_object())
        return;

    message.AddString("show",
        EpisodeJsonString(episode["show"], "name").c_str());
    std::string showId = EpisodeJsonString(episode["show"], "id");
    message.AddString("show_uri", (!showId.empty()
        ? SpotifyUriForItemKind(kSpotifyItemShow, showId)
        : EpisodeJsonString(episode["show"], "uri")).c_str());
}

static void
AddEpisodeImageField(BMessage& message, const nlohmann::json& episode)
{
    if (episode.contains("images") && episode["images"].is_array()
            && !episode["images"].empty()) {
        message.AddString("image",
            EpisodeJsonString(episode["images"][0], "url").c_str());
    }
}

static void
AddEpisodeRestrictionField(BMessage& message, const nlohmann::json& episode)
{
    if (episode.contains("restrictions")
            && episode["restrictions"].is_object()) {
        message.AddString("restriction",
            EpisodeJsonString(episode["restrictions"], "reason").c_str());
    }
}

static void
SendEpisodeDataMessage(BMessenger self, const std::string& episodeId,
    const nlohmann::json& episode)
{
    if (!episode.is_object())
        return;

    BMessage message('eDat');
    message.AddString("name",
        EpisodeJsonString(episode, "name", "Unknown").c_str());
    message.AddString("description",
        EpisodeJsonString(episode, "description").c_str());
    std::string episodeUri = SpotifyUriForItemKind(
        kSpotifyItemEpisode, episodeId);
    message.AddString("uri", episodeUri.c_str());
    AddEpisodeShowFields(message, episode);
    AddEpisodeImageField(message, episode);
    message.AddBool("playable", EpisodeJsonBool(episode,
        "is_playable", true));
    AddEpisodeRestrictionField(message, episode);
    self.SendMessage(&message);
}

static void
SendEpisodeSavedMessage(BMessenger self, bool ok, const nlohmann::json& data)
{
    if (!ok || !data.is_array() || data.empty())
        return;

    BMessage message('eSts');
    message.AddBool("saved", data[0].is_boolean() && data[0].get<bool>());
    self.SendMessage(&message);
}

EpisodeWindow::EpisodeWindow(const std::string& episodeId)
    : BWindow(BRect(160, 120, 720, 540), B_TRANSLATE("Episode"),
        B_DOCUMENT_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
      fEpisodeId(episodeId)
{
    fArtwork = new ArtworkView("episodeArtwork");
    fArtwork->ShowLoading();
    fArtwork->SetExplicitMinSize(BSize(140, 140));
    fArtwork->SetExplicitMaxSize(BSize(140, 140));
    fArtwork->SetExplicitPreferredSize(BSize(140, 140));

    fName = new BStringView("episodeName", B_TRANSLATE("Loading…"));
    BFont titleFont(be_bold_font);
    titleFont.SetSize(be_plain_font->Size() * 1.5f);
    fName->SetFont(&titleFont);
    fName->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));
    fName->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
    fName->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
        B_ALIGN_VERTICAL_CENTER));
    fShow = new ClickableLabelView("episodeShow", 'eShw');

    fMenuBar = new BMenuBar("episodeMenuBar");
    BMenu* episodeMenu = new BMenu(B_TRANSLATE("Episode"));
    fPlayMenuItem = new BMenuItem(B_TRANSLATE("Play"),
        new BMessage('ePly'));
    fQueueMenuItem = new BMenuItem(B_TRANSLATE("Add to Queue"),
        new BMessage('eQue'));
    fSaveMenuItem = new BMenuItem(B_TRANSLATE("Save"),
        new BMessage('eSav'));
    fOpenShowMenuItem = new BMenuItem(B_TRANSLATE("Open Show"),
        new BMessage('eShw'));
    episodeMenu->AddItem(fPlayMenuItem);
    episodeMenu->AddItem(fQueueMenuItem);
    episodeMenu->AddSeparatorItem();
    episodeMenu->AddItem(fSaveMenuItem);
    episodeMenu->AddSeparatorItem();
    episodeMenu->AddItem(fOpenShowMenuItem);
    fMenuBar->AddItem(episodeMenu);

    fDescription = new BTextView("episodeDescription");
    fDescription->MakeEditable(false);
    fDescription->MakeSelectable(true);
    fDescription->SetWordWrap(true);
    fDescription->SetExplicitMinSize(BSize(0, 120));
    fDescription->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
        B_SIZE_UNLIMITED));

    BScrollView* descriptionScroll = new BScrollView(
        "episodeDescriptionScroll", fDescription, 0, true, true);
    descriptionScroll->SetExplicitMinSize(BSize(0, 160));
    descriptionScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
        B_SIZE_UNLIMITED));

    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(0)
        .Add(fMenuBar)
        .AddGroup(B_VERTICAL, 0, 1.0f)
            .SetInsets(0)
            .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
                .SetInsets(B_USE_DEFAULT_SPACING)
                .Add(fArtwork)
                .AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
                    .Add(fName)
                    .Add(fShow)
                    .AddGlue()
                .End()
            .End()
            .Add(descriptionScroll, 1.0f)
        .End()
    .End();

    _Load();
}

void EpisodeWindow::_Load()
{
    App* app = dynamic_cast<App*>(be_app);
    SpotifyApi* api = app ? app->GetApi() : nullptr;
    if (!api) return;
    BMessenger self(this);
    std::string episodeId = fEpisodeId;
    api->Content().GetEpisode(fEpisodeId, [self, episodeId](bool ok,
            const nlohmann::json& episode) {
        if (ok)
            SendEpisodeDataMessage(self, episodeId, episode);
    });
    api->Library().CheckLibraryItems(
        {SpotifyUriForItemKind(kSpotifyItemEpisode, fEpisodeId)},
        [self](bool ok, const nlohmann::json& data) {
            SendEpisodeSavedMessage(self, ok, data);
        });
}

void EpisodeWindow::_LoadArtwork(const std::string& url)
{
    if (fArtwork)
        fArtwork->LoadUrl(url);
}

void EpisodeWindow::_UpdateSaved(bool saved)
{
    fSaved = saved;
    if (fSaveMenuItem)
        fSaveMenuItem->SetLabel(saved ? B_TRANSLATE("Remove")
            : B_TRANSLATE("Save"));
}

void EpisodeWindow::_ApplyLibraryChanged(BMessage* message)
{
    std::string uri = message->GetString("uri", "");
    std::string operation = message->GetString("operation", "");
    if (uri == SpotifyUriForItemKind(kSpotifyItemEpisode, fEpisodeId)
            && (operation == "add" || operation == "remove")) {
        _UpdateSaved(operation == "add");
    }
}

void EpisodeWindow::_ApplyEpisodeData(BMessage* message)
{
    fEpisodeUri = message->GetString("uri", "");
    fShowName = message->GetString("show", "");
    fShowUri = message->GetString("show_uri", "");
    fName->SetText(message->GetString("name", "Unknown"));
    fShow->SetText(fShowName.c_str());
    bool playable = message->GetBool("playable", true);
    BString description(message->GetString("description", ""));
    const char* restriction = message->GetString("restriction", "");
    if (!playable && restriction[0]) {
        BString prefix(B_TRANSLATE("Unavailable: "));
        prefix << restriction << "\n\n" << description;
        description = prefix;
    }
    fDescription->SetText(description.String());
    fPlayMenuItem->SetEnabled(playable);
    fQueueMenuItem->SetEnabled(playable);
    fOpenShowMenuItem->SetEnabled(!fShowUri.empty());
    SetTitle(message->GetString("name", B_TRANSLATE("Episode")));
    _LoadArtwork(message->GetString("image", ""));
}

void EpisodeWindow::_PlayEpisode()
{
    if (fEpisodeUri.empty())
        return;

    BMessage play('play');
    play.AddString("uri", fEpisodeUri.c_str());
    play.AddString("title", fName->Text());
    if (!fShowName.empty())
        play.AddString("artist", fShowName.c_str());
    if (!fShowUri.empty()) {
        play.AddString("context_uri", fShowUri.c_str());
        play.AddString(kNowPlayingPrimaryOpenUriField, fShowUri.c_str());
        play.AddString(kNowPlayingParentUriField, fShowUri.c_str());
        play.AddString(kNowPlayingParentKindField, "show");
        if (SpotifyItemKindForUri(fShowUri) == kSpotifyItemShow) {
            play.AddString(kNowPlayingShowIdField,
                SpotifyItemIdForUri(fShowUri).c_str());
        }
    }
    play.AddString(kNowPlayingItemKindField, "episode");
    be_app->PostMessage(&play);
}

void EpisodeWindow::_OpenShow()
{
    if (fShowUri.empty())
        return;

    BMessage open('open');
    open.AddString("uri", fShowUri.c_str());
    be_app->PostMessage(&open);
}

void EpisodeWindow::_QueueEpisode()
{
    App* app = dynamic_cast<App*>(be_app);
    SpotifyApi* api = app ? app->GetApi() : nullptr;
    if (api && !fEpisodeUri.empty())
        api->Playback().AddToQueue(fEpisodeUri, nullptr);
}

void EpisodeWindow::_ToggleSaved()
{
    App* app = dynamic_cast<App*>(be_app);
    SpotifyApi* api = app ? app->GetApi() : nullptr;
    if (!api)
        return;

    bool target = !fSaved;
    BMessenger self(this);
    std::string episodeUri = SpotifyUriForItemKind(
        kSpotifyItemEpisode, fEpisodeId);
    auto done = [self, target, episodeUri](bool ok, const nlohmann::json&) {
        if (!ok) return;
        BMessage state('eSts');
        state.AddBool("saved", target);
        self.SendMessage(&state);
        BMessage changed(MSG_LIBRARY_CHANGED);
        changed.AddString("operation", target ? "add" : "remove");
        changed.AddString("uri", episodeUri.c_str());
        be_app->PostMessage(&changed);
    };
    std::vector<std::string> uris = {episodeUri};
    if (target)
        api->Library().SaveLibraryItems(uris, done);
    else
        api->Library().RemoveLibraryItems(uris, done);
}

void EpisodeWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_LIBRARY_CHANGED:
            _ApplyLibraryChanged(message);
            break;

        case 'eDat':
            _ApplyEpisodeData(message);
            break;

        case 'eSts':
            _UpdateSaved(message->GetBool("saved", false));
            break;

        case 'ePly':
            _PlayEpisode();
            break;

        case 'eShw':
            _OpenShow();
            break;

        case 'eQue':
            _QueueEpisode();
            break;

        case 'eSav':
            _ToggleSaved();
            break;

        default:
            BWindow::MessageReceived(message);
            break;
    }
}
