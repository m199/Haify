#include "AudiobookWindow.h"

#include "App.h"
#include "ArtworkView.h"
#include "DiscoverListView.h"
#include "MediaHeaderStyle.h"
#include "Messages.h"
#include "spotify/api/SpotifyApi.h"

#include <Alert.h>
#include <Alignment.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <View.h>

#include <cstdio>
#include <cstring>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AudiobookWindow"

static std::string
AudiobookJsonString(const nlohmann::json& object, const char* key,
	const char* fallback = "")
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_string())
		return fallback;
	return object[key].get<std::string>();
}

static int32
AudiobookJsonInt32(const nlohmann::json& object, const char* key,
	int32 fallback = 0)
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_number_integer())
		return fallback;
	return object[key].get<int32>();
}

static bool
AudiobookJsonBool(const nlohmann::json& object, const char* key,
	bool fallback = false)
{
	if (!object.is_object() || !object.contains(key)
			|| !object[key].is_boolean())
		return fallback;
	return object[key].get<bool>();
}

AudiobookWindow::AudiobookWindow(const std::string& audiobookId)
	: BWindow(BRect(170, 110, 800, 650), B_TRANSLATE("Audiobook"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS),
	  fAudiobookId(audiobookId)
{
	fArtwork = new ArtworkView("audiobookArtwork");
	fArtwork->ShowLoading();
	const float artworkSize = MediaHeaderStyle::kArtworkSize;
	fArtwork->SetExplicitMinSize(BSize(artworkSize, artworkSize));
	fArtwork->SetExplicitMaxSize(BSize(artworkSize, artworkSize));
	fArtwork->SetExplicitPreferredSize(BSize(artworkSize, artworkSize));
	fArtwork->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));

	fMenuBar = new BMenuBar("audiobookMenuBar");
	BMenu* audiobookMenu = new BMenu(B_TRANSLATE("Audiobook"));
	fSaveMenuItem = new BMenuItem(B_TRANSLATE("Add to Audiobooks"),
		new BMessage('aSav'));
	fSaveMenuItem->SetEnabled(false);
	audiobookMenu->AddItem(fSaveMenuItem);
	fMenuBar->AddItem(audiobookMenu);

	fName = new BTextView("audiobookName");
	fName->SetText(B_TRANSLATE("Loading" B_UTF8_ELLIPSIS));
	BFont titleFont(be_bold_font);
	titleFont.SetSize(be_plain_font->Size() * MediaHeaderStyle::kTitleScale);
	fName->SetFontAndColor(&titleFont);
	fName->MakeEditable(false);
	fName->MakeSelectable(false);
	fName->SetWordWrap(true);
	fName->SetInsets(0, 0, 0, 0);
	fName->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fName->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));
	fName->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 60));
	fName->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_TOP));
	fCredits = new BStringView("audiobookAuthors", "");
	fCredits->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fCredits->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_VERTICAL_CENTER));
	fNarrators = new BStringView("audiobookNarrators", "");
	fNarrators->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fNarrators->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_VERTICAL_CENTER));
	fSave = new BButton("saveAudiobook", B_TRANSLATE("Add to Audiobooks"),
		new BMessage('aSav'));
	fSave->SetExplicitMinSize(BSize(
		MediaHeaderStyle::kActionButtonMinWidth, B_SIZE_UNSET));
	fSave->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));
	fSave->SetEnabled(false);
	fDescription = new BTextView("audiobookDescription");
	fDescription->MakeEditable(false);
	fDescription->MakeSelectable(true);
	fDescription->SetWordWrap(true);

	fChapterList = new DiscoverListView("Chapters", {
		{B_TRANSLATE("Chapter"), 390, kColPlayOnDouble},
		{B_TRANSLATE("Duration"), 80, kColNone}
	}, -1);
	BScrollView* descriptionScroll = new BScrollView(
		"audiobookDescriptionScroll", fDescription, 0, false, true);
	descriptionScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 72));
	descriptionScroll->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 72));
	descriptionScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 96));

	BView* headerInfo = new BView("audiobookHeaderInfo", 0);
	headerInfo->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));
	headerInfo->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	headerInfo->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));
	BLayoutBuilder::Group<>(headerInfo, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fName, 0.0f)
		.Add(fCredits, 0.0f)
		.Add(fNarrators, 0.0f)
		.AddGlue()
		.Add(fSave, 0.0f)
	.End();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar, 0.0f)
		.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 1.0f)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0.0f)
				.Add(fArtwork, 0.0f)
				.Add(headerInfo, 1.0f)
			.End()
			.Add(descriptionScroll, 0.0f)
			.Add(fChapterList, 1.0f)
		.End()
	.End();
	SetSizeLimits(320, 100000, 280, 100000);

	_Load();
}

void
AudiobookWindow::_Load()
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api || !app->GetCapabilities()->AudiobooksEnabled())
		return;
	BMessenger self(this);
	std::string audiobookId = fAudiobookId;
	api->GetAudiobook(fAudiobookId, [self, audiobookId](bool ok,
			const nlohmann::json& book) {
		if (!ok || !book.is_object()) return;
		BMessage message('aDat');
		message.AddString("name", AudiobookJsonString(book, "name", "Unknown").c_str());
		message.AddString("description", AudiobookJsonString(book,
			"description").c_str());
		message.AddString("uri", ("spotify:audiobook:" + audiobookId).c_str());
		auto addPeople = [&message, &book](const char* source, const char* target) {
			if (!book.contains(source) || !book[source].is_array()) return;
			for (const auto& person : book[source]) {
				if (person.is_object())
					message.AddString(target,
						AudiobookJsonString(person, "name").c_str());
			}
		};
		addPeople("authors", "author");
		addPeople("narrators", "narrator");
		if (book.contains("images") && book["images"].is_array()
				&& !book["images"].empty())
			message.AddString("image",
				AudiobookJsonString(book["images"][0], "url").c_str());
		self.SendMessage(&message);
	});
	api->CheckSavedAudiobook(fAudiobookId,
		[self](bool ok, const nlohmann::json& data) {
			BMessage message('aSts');
			bool valid = ok && data.is_array() && !data.empty()
				&& data[0].is_boolean();
			message.AddBool("ok", valid);
			if (valid)
				message.AddBool("saved", data[0].get<bool>());
			self.SendMessage(&message);
		});
	_LoadChapters(0);
}

void
AudiobookWindow::_LoadChapters(int32 offset)
{
	if (fLoadingChapters) return;
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api) return;
	fLoadingChapters = true;
	BMessenger self(this);
	api->GetAudiobookChapters(fAudiobookId, offset, 50,
		[self, offset](bool ok, const nlohmann::json& data) {
			BMessage message('aChp');
			message.AddBool("ok", ok);
			message.AddInt32("offset", offset);
			if (ok && data.contains("items") && data["items"].is_array()) {
				for (const auto& chapter : data["items"]) {
					if (!chapter.is_object()) continue;
					std::string chapterId = AudiobookJsonString(chapter, "id");
					std::string chapterUri = AudiobookJsonString(chapter, "uri");
					if (chapterUri.empty() && !chapterId.empty())
						chapterUri = "spotify:episode:" + chapterId;
					message.AddString("uri", chapterUri.c_str());
					message.AddString("title",
						AudiobookJsonString(chapter, "name", "Unknown").c_str());
					message.AddInt32("duration_ms",
						AudiobookJsonInt32(chapter, "duration_ms"));
					message.AddBool("playable",
						AudiobookJsonBool(chapter, "is_playable", true));
					std::string reason;
					if (chapter.contains("restrictions")
							&& chapter["restrictions"].is_object())
						reason = AudiobookJsonString(chapter["restrictions"],
							"reason");
					message.AddString("restriction", reason.c_str());
				}
				message.AddInt32("next_offset", offset + (int32)data["items"].size());
				message.AddInt32("total", AudiobookJsonInt32(data, "total"));
			}
			self.SendMessage(&message);
		});
}

void
AudiobookWindow::_LoadArtwork(const std::string& url)
{
	if (fArtwork)
		fArtwork->LoadUrl(url);
}

void
AudiobookWindow::_UpdateSaved(bool saved)
{
	fSaved = saved;
	fSavedKnown = true;
	fSavePending = false;
	_UpdateSavedControls();
}

void
AudiobookWindow::_UpdateSavedControls()
{
	fSave->SetLabel(fSaved ? B_TRANSLATE("Remove from Audiobooks")
		: B_TRANSLATE("Add to Audiobooks"));
	bool enabled = fSavedKnown && !fSavePending;
	fSave->SetEnabled(enabled);
	fSaveMenuItem->SetLabel(fSaved ? B_TRANSLATE("Remove from Audiobooks")
		: B_TRANSLATE("Add to Audiobooks"));
	fSaveMenuItem->SetEnabled(enabled);
}

void
AudiobookWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_LIBRARY_CHANGED:
		{
			std::string uri = message->GetString("uri", "");
			std::string operation = message->GetString("operation", "");
			if (uri == "spotify:audiobook:" + fAudiobookId
					&& (operation == "add" || operation == "remove")) {
				_UpdateSaved(operation == "add");
			}
			break;
		}

		case 'aDat':
		{
			fAudiobookUri = message->GetString("uri", "");
			const char* name = message->GetString("name", B_TRANSLATE("Audiobook"));
			fName->SetText(name);
			BString windowTitle(B_TRANSLATE("Audiobook: "));
			windowTitle << name;
			SetTitle(windowTitle.String());
			fDescription->SetText(message->GetString("description", ""));
			BString authors;
			const char* person = nullptr;
			for (int32 i = 0; message->FindString("author", i, &person) == B_OK; i++) {
				if (!person || !person[0])
					continue;
				if (!authors.IsEmpty()) authors << ", ";
				authors << person;
			}
			BString authorLine;
			if (!authors.IsEmpty())
				authorLine << B_TRANSLATE("Author(s): ") << authors;
			fCredits->SetText(authorLine.String());

			BString narrators;
			for (int32 i = 0; message->FindString("narrator", i, &person) == B_OK; i++) {
				if (!narrators.IsEmpty()) narrators << ", ";
				narrators << person;
			}
			BString narratorLine;
			if (!narrators.IsEmpty())
				narratorLine << B_TRANSLATE("Narrator(s): ") << narrators;
			fNarrators->SetText(narratorLine.String());
			_LoadArtwork(message->GetString("image", ""));
			break;
		}
		case 'aSts':
		{
			bool ok = message->GetBool("ok", true);
			if (ok) {
				_UpdateSaved(message->GetBool("saved", false));
				if (message->GetBool("changed", false)) {
					BMessage changed(MSG_LIBRARY_CHANGED);
					changed.AddString("operation", fSaved ? "add" : "remove");
					changed.AddString("uri",
						("spotify:audiobook:" + fAudiobookId).c_str());
					be_app->PostMessage(&changed);
				}
			} else {
				bool showError = message->GetBool("show_error", false);
				fSavePending = false;
				_UpdateSavedControls();
				if (showError) {
					BAlert* alert = new BAlert(B_TRANSLATE("Audiobook"),
						B_TRANSLATE("The Audiobooks library could not be updated."),
						B_TRANSLATE("OK"));
					alert->Go();
				}
			}
			break;
		}
		case 'aChp':
		{
			fLoadingChapters = false;
			if (!message->GetBool("ok", false)) break;
			const char* uri = nullptr;
			for (int32 i = 0; message->FindString("uri", i, &uri) == B_OK; i++) {
				const char* title = nullptr;
				int32 durationMs = 0;
				bool playable = true;
				message->FindString("title", i, &title);
				message->FindInt32("duration_ms", i, &durationMs);
				message->FindBool("playable", i, &playable);
				std::string chapterUri = uri ? uri : "";
				std::string chapterTitle = title ? title : "Unknown";
				int32 seconds = durationMs / 1000;
				char duration[32];
				snprintf(duration, sizeof(duration), "%d:%02d", seconds / 60,
					seconds % 60);
				std::string displayTitle = chapterTitle;
				if (!playable) displayTitle += B_TRANSLATE(" (Unavailable)");
				fChapterList->AddRow(new DiscoverRow(
					{displayTitle, duration},
					{playable ? chapterUri : "", ""},
					{chapterTitle, ""}));
			}
			int32 next = message->GetInt32("next_offset", 0);
			if (next > 0 && next < message->GetInt32("total", 0))
				_LoadChapters(next);
			break;
		}
		case 'play':
		{
			const char* uri = message->GetString("uri", "");
			if (*uri) {
				BMessage play('play');
				play.AddString("uri", uri);
				be_app->PostMessage(&play);
			}
			break;
		}
		case 'rClk':
		{
			const char* uri = message->GetString("uri", "");
			if (strncmp(uri, "spotify:episode:", 16) != 0) break;
			App* app = dynamic_cast<App*>(be_app);
			SpotifyApi* api = app ? app->GetApi() : nullptr;
			if (!api) break;
			BMessenger self(this);
			api->GetChapter(std::string(uri).substr(16),
				[self](bool ok, const nlohmann::json& chapter) {
					if (!ok || !chapter.is_object()) return;
					BMessage detail('cDtl');
					detail.AddString("name", chapter.value("name", "Chapter").c_str());
					detail.AddString("description",
						chapter.value("description", "").c_str());
					self.SendMessage(&detail);
				});
			break;
		}
		case 'cDtl':
		{
			BAlert* alert = new BAlert(message->GetString("name", "Chapter"),
				message->GetString("description", ""), B_TRANSLATE("OK"));
			alert->Go();
			break;
		}
		case 'aSav':
		{
			if (!fSavedKnown || fSavePending)
				break;
			App* app = dynamic_cast<App*>(be_app);
			SpotifyApi* api = app ? app->GetApi() : nullptr;
			if (!api) break;
			bool target = !fSaved;
			fSavePending = true;
			_UpdateSavedControls();
			BMessenger self(this);
			auto done = [self, target](bool ok, const nlohmann::json&) {
				BMessage state('aSts');
				state.AddBool("ok", ok);
				state.AddBool("saved", target);
				state.AddBool("changed", ok);
				state.AddBool("show_error", !ok);
				self.SendMessage(&state);
			};
			if (target) api->SaveAudiobook(fAudiobookId, done);
			else api->RemoveSavedAudiobook(fAudiobookId, done);
			break;
		}
		case MSG_SPOTIFY_CAPABILITIES_CHANGED:
		{
			App* app = dynamic_cast<App*>(be_app);
			if (!app || !app->GetCapabilities()->AudiobooksEnabled())
				PostMessage(B_QUIT_REQUESTED);
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
