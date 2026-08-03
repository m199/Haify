#include "EpisodeWindow.h"

#include "App.h"
#include "ArtworkView.h"
#include "Messages.h"
#include "spotify/api/SpotifyApi.h"

#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <Font.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
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

EpisodeWindow::EpisodeWindow(const std::string& episodeId)
    : BWindow(BRect(160, 120, 720, 540), B_TRANSLATE("Episode"),
        B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
      fEpisodeId(episodeId)
{
    fArtwork = new ArtworkView("episodeArtwork");
    fArtwork->ShowLoading();
    fArtwork->SetExplicitMinSize(BSize(140, 140));
    fArtwork->SetExplicitMaxSize(BSize(140, 140));

    fName = new BStringView("episodeName", B_TRANSLATE("Loading…"));
    BFont titleFont(be_bold_font);
    titleFont.SetSize(be_plain_font->Size() * 1.5f);
    fName->SetFont(&titleFont);
    fShow = new BStringView("episodeShow", "");

    fDescription = new BTextView("episodeDescription");
    fDescription->MakeEditable(false);
    fDescription->MakeSelectable(true);
    fDescription->SetWordWrap(true);

    fPlay = new BButton("playEpisode", B_TRANSLATE("Play"),
        new BMessage('ePly'));
    fSave = new BButton("saveEpisode", B_TRANSLATE("Save"),
        new BMessage('eSav'));
    fQueue = new BButton("queueEpisode", B_TRANSLATE("Add to Queue"),
        new BMessage('eQue'));
    fOpenShow = new BButton("openShow", B_TRANSLATE("Open Show"),
        new BMessage('eShw'));

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_DEFAULT_SPACING)
        .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
            .Add(fArtwork)
            .AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
                .Add(fName)
                .Add(fShow)
                .AddGlue()
                .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
                    .Add(fPlay)
                    .Add(fQueue)
                    .Add(fSave)
                    .Add(fOpenShow)
                    .AddGlue()
                .End()
            .End()
        .End()
        .Add(new BScrollView("episodeDescriptionScroll", fDescription,
            0, false, true), 1.0f)
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
    api->GetEpisode(fEpisodeId, [self, episodeId](bool ok,
            const nlohmann::json& episode) {
        if (!ok || !episode.is_object()) return;
        BMessage message('eDat');
        message.AddString("name",
            EpisodeJsonString(episode, "name", "Unknown").c_str());
        message.AddString("description",
            EpisodeJsonString(episode, "description").c_str());
        message.AddString("uri", ("spotify:episode:" + episodeId).c_str());
        if (episode.contains("show") && episode["show"].is_object()) {
            message.AddString("show",
                EpisodeJsonString(episode["show"], "name").c_str());
            std::string showId = EpisodeJsonString(episode["show"], "id");
            message.AddString("show_uri", (!showId.empty()
                ? "spotify:show:" + showId
                : EpisodeJsonString(episode["show"], "uri")).c_str());
        }
        if (episode.contains("images") && episode["images"].is_array()
                && !episode["images"].empty()) {
            message.AddString("image",
                EpisodeJsonString(episode["images"][0], "url").c_str());
        }
        message.AddBool("playable", EpisodeJsonBool(episode,
            "is_playable", true));
        if (episode.contains("restrictions")
                && episode["restrictions"].is_object())
            message.AddString("restriction",
                EpisodeJsonString(episode["restrictions"], "reason").c_str());
        self.SendMessage(&message);
    });
    api->CheckLibraryItems({"spotify:episode:" + fEpisodeId},
        [self](bool ok, const nlohmann::json& data) {
            if (!ok || !data.is_array() || data.empty()) return;
            BMessage message('eSts');
            message.AddBool("saved", data[0].is_boolean()
                && data[0].get<bool>());
            self.SendMessage(&message);
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
    if (fSave)
        fSave->SetLabel(saved ? B_TRANSLATE("Remove") : B_TRANSLATE("Save"));
}

void EpisodeWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_LIBRARY_CHANGED:
        {
            std::string uri = message->GetString("uri", "");
            std::string operation = message->GetString("operation", "");
            if (uri == "spotify:episode:" + fEpisodeId
                    && (operation == "add" || operation == "remove")) {
                _UpdateSaved(operation == "add");
            }
            break;
        }

        case 'eDat':
        {
            fEpisodeUri = message->GetString("uri", "");
            fShowUri = message->GetString("show_uri", "");
            fName->SetText(message->GetString("name", "Unknown"));
            fShow->SetText(message->GetString("show", ""));
            bool playable = message->GetBool("playable", true);
            BString description(message->GetString("description", ""));
            const char* restriction = message->GetString("restriction", "");
            if (!playable && restriction[0]) {
                BString prefix(B_TRANSLATE("Unavailable: "));
                prefix << restriction << "\n\n" << description;
                description = prefix;
            }
            fDescription->SetText(description.String());
            fPlay->SetEnabled(playable);
            fQueue->SetEnabled(playable);
            fOpenShow->SetEnabled(!fShowUri.empty());
            SetTitle(message->GetString("name", B_TRANSLATE("Episode")));
            _LoadArtwork(message->GetString("image", ""));
            break;
        }
        case 'eSts':
            _UpdateSaved(message->GetBool("saved", false));
            break;
        case 'ePly':
            if (!fEpisodeUri.empty()) {
                BMessage play('play');
                play.AddString("uri", fEpisodeUri.c_str());
                be_app->PostMessage(&play);
            }
            break;
        case 'eShw':
            if (!fShowUri.empty()) {
                BMessage open('open');
                open.AddString("uri", fShowUri.c_str());
                be_app->PostMessage(&open);
            }
            break;
        case 'eQue':
        {
            App* app = dynamic_cast<App*>(be_app);
            SpotifyApi* api = app ? app->GetApi() : nullptr;
            if (api && !fEpisodeUri.empty())
                api->AddToQueue(fEpisodeUri, nullptr);
            break;
        }
        case 'eSav':
        {
            App* app = dynamic_cast<App*>(be_app);
            SpotifyApi* api = app ? app->GetApi() : nullptr;
            if (!api) break;
            bool target = !fSaved;
            BMessenger self(this);
            std::string episodeUri = "spotify:episode:" + fEpisodeId;
            auto done = [self, target, episodeUri](bool ok,
                    const nlohmann::json&) {
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
            if (target) api->SaveLibraryItems(uris, done);
            else api->RemoveLibraryItems(uris, done);
            break;
        }
        default:
            BWindow::MessageReceived(message);
            break;
    }
}
