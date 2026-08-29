#include "AudiobookWindow.h"

#include "App.h"
#include "ArtworkView.h"
#include "DescriptionTextFormatter.h"
#include "DiscoverListView.h"
#include "MediaDescriptionView.h"
#include "MediaHeaderStyle.h"
#include "Messages.h"
#include "NowPlayingFields.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"

#include <Alert.h>
#include <Alignment.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <View.h>

#include <cctype>
#include <cstdio>

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

static std::string
StripLeadingAudiobookCredits(const std::string& description)
{
	size_t start = 0;
	while (start < description.size()) {
		size_t lineEnd = description.find('\n', start);
		std::string line = description.substr(start,
			lineEnd == std::string::npos ? std::string::npos
				: lineEnd - start);
		if (line.rfind("Author(s):", 0) != 0
				&& line.rfind("Narrator(s):", 0) != 0) {
			break;
		}
		start = lineEnd == std::string::npos
			? description.size() : lineEnd + 1;
	}
	while (start < description.size()
			&& (description[start] == '\n' || description[start] == '\r'
				|| description[start] == ' ' || description[start] == '\t')) {
		start++;
	}
	return description.substr(start);
}

static bool
AudiobookNeedsSpaceAfter(char character)
{
	return character == '.' || character == '!' || character == '?'
		|| character == ':' || character == ';';
}

static bool
AudiobookCanInsertSpaceBefore(char character)
{
	return character != '\0' && character != ' ' && character != '\t'
		&& character != '\n' && character != '\r' && character != '.'
		&& character != ',' && character != ';' && character != ':'
		&& character != '!' && character != '?' && character != ')'
		&& character != ']' && character != '}';
}

static bool
AudiobookAsciiLower(char character)
{
	return character >= 'a' && character <= 'z';
}

static bool
AudiobookAsciiUpper(char character)
{
	return character >= 'A' && character <= 'Z';
}

static std::string
NormalizeAudiobookDescriptionSpacing(const std::string& description)
{
	std::string normalized;
	normalized.reserve(description.size() + 16);
	for (size_t index = 0; index < description.size(); index++) {
		char current = description[index];
		normalized += current;
		if (index + 1 < description.size()
				&& AudiobookAsciiLower(current)
				&& AudiobookAsciiUpper(description[index + 1])) {
			normalized += ' ';
			continue;
		}
		if (!AudiobookNeedsSpaceAfter(current) || index + 1 >= description.size())
			continue;

		char next = description[index + 1];
		if (!AudiobookCanInsertSpaceBefore(next))
			continue;
		if (current == '.' && isdigit((unsigned char)next))
			continue;
		if (index > 0 && current == ':'
				&& description[index - 1] == '/'
				&& next == '/') {
			continue;
		}
		normalized += ' ';
	}
	return normalized;
}

static int32
WrappedTitleLineCount(const BFont& font, const std::string& text, float width)
{
	if (width <= 0)
		return 1;

	int32 lines = 1;
	std::string line;
	size_t index = 0;
	while (index < text.size()) {
		while (index < text.size()
				&& (text[index] == ' ' || text[index] == '\t')) {
			index++;
		}
		if (index >= text.size())
			break;
		if (text[index] == '\n') {
			lines++;
			line.clear();
			index++;
			continue;
		}

		size_t end = index;
		while (end < text.size() && text[end] != ' ' && text[end] != '\t'
				&& text[end] != '\n') {
			end++;
		}
		std::string word = text.substr(index, end - index);
		std::string candidate = line.empty() ? word : line + " " + word;
		if (!line.empty() && font.StringWidth(candidate.c_str()) > width) {
			lines++;
			line = word;
		} else {
			line = candidate;
		}
		index = end;
	}
	return lines;
}

static float
FontLineHeight(const BFont& font)
{
	font_height height;
	font.GetHeight(&height);
	return height.ascent + height.descent + height.leading;
}

AudiobookWindow::AudiobookWindow(const std::string& audiobookId)
	: BWindow(BRect(170, 110, 800, 650), B_TRANSLATE("Audiobook"),
		B_DOCUMENT_WINDOW, B_ASYNCHRONOUS_CONTROLS),
	  fAudiobookId(audiobookId)
{
	fArtwork = new ArtworkView("audiobookArtwork");
	fArtwork->ShowLoading();
	const float artworkSize = MediaHeaderStyle::kArtworkSize;
	fArtwork->SetExplicitMinSize(BSize(artworkSize, artworkSize));
	fArtwork->SetExplicitMaxSize(BSize(artworkSize, artworkSize));
	fArtwork->SetExplicitPreferredSize(BSize(artworkSize, artworkSize));
	fArtwork->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_TOP));

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
	fName->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 34));
	fName->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 36));
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
	fDescription = new MediaDescriptionView("audiobookDescription");

	fChapterList = new DiscoverListView("Chapters", {
		{B_TRANSLATE("Chapter"), 390, kColPlayOnDouble},
		{B_TRANSLATE("Duration"), 90, kColNone}
	}, -1, true);
	fChapterList->SetExplicitMinSize(BSize(B_SIZE_UNSET, 96));
	fChapterList->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 136));
	BScrollView* descriptionScroll = new BScrollView(
		"audiobookDescriptionScroll", fDescription, 0, false, true);
	descriptionScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120));
	descriptionScroll->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 260));
	descriptionScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		B_SIZE_UNLIMITED));

	BView* headerInfo = new BView("audiobookHeaderInfo", 0);
	headerInfo->SetExplicitMinSize(BSize(0, artworkSize));
	headerInfo->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, artworkSize));
	headerInfo->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, artworkSize));
	headerInfo->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));
	BLayoutBuilder::Group<>(headerInfo, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fName, 0.0f)
		.AddGroup(B_VERTICAL, 0, 0.0f)
			.Add(fCredits, 0.0f)
			.Add(fNarrators, 0.0f)
		.End()
		.AddGlue()
		.Add(fSave, 0.0f)
	.End();

	BSplitView* contentSplit = new BSplitView(B_VERTICAL,
		B_USE_SMALL_SPACING);
	BLayoutBuilder::Split<>(contentSplit)
		.Add(descriptionScroll, 1.0f)
		.Add(fChapterList, 0.0f)
		.SetCollapsible(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar, 0.0f)
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.SetInsets(0)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0.0f)
				.SetInsets(B_USE_DEFAULT_SPACING)
				.Add(fArtwork, 0.0f)
				.Add(headerInfo, 1.0f)
			.End()
			.Add(contentSplit, 1.0f)
		.End()
	.End();
	SetSizeLimits(420, 100000, 360, 100000);

	_Load();
}

void
AudiobookWindow::FrameResized(float width, float height)
{
	BWindow::FrameResized(width, height);
	_ApplyTitleText();
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
	api->Content().GetAudiobook(fAudiobookId, [self, audiobookId](bool ok,
			const nlohmann::json& book) {
		if (!ok || !book.is_object()) return;
		BMessage message('aDat');
		message.AddString("name", AudiobookJsonString(book, "name", "Unknown").c_str());
		std::string description = AudiobookJsonString(book, "description");
		if (description.empty())
			description = AudiobookJsonString(book, "html_description");
		description = StripLeadingAudiobookCredits(description);
		description = NormalizeAudiobookDescriptionSpacing(description);
		message.AddString("description", description.c_str());
		std::string audiobookUri = SpotifyUriForItemKind(
			kSpotifyItemAudiobook, audiobookId);
		message.AddString("uri", audiobookUri.c_str());
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
	api->Library().CheckSavedAudiobook(fAudiobookId,
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
	api->Content().GetAudiobookChapters(fAudiobookId, offset, 50,
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
						chapterUri = SpotifyUriForItemKind(
							kSpotifyItemEpisode, chapterId);
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
AudiobookWindow::_ApplyTitleText()
{
	if (!fName || fAudiobookName.empty())
		return;

	float titleWidth = fName->Bounds().Width();
	if (titleWidth <= 0 && fName->Parent())
		titleWidth = fName->Parent()->Bounds().Width();
	if (titleWidth <= 0)
		titleWidth = 360.0f;

	const float baseSize = be_plain_font->Size();
	const float maxSize = baseSize * MediaHeaderStyle::kTitleScale;
	const float minSize = baseSize;
	const float maxHeight = 34.0f;

	BFont titleFont(be_bold_font);
	for (float size = maxSize; size >= minSize; size -= 1.0f) {
		titleFont.SetSize(size);
		int32 lines = WrappedTitleLineCount(titleFont, fAudiobookName,
			titleWidth);
		if (lines <= 2 && FontLineHeight(titleFont) * lines <= maxHeight)
			break;
	}
	fName->SetFontAndColor(&titleFont);
	fName->SetText(fAudiobookName.c_str());
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

std::string
AudiobookWindow::_NextPlayableChapterUri(const std::string& currentUri) const
{
	if (!fChapterList || currentUri.empty())
		return "";

	for (int32 index = 0; index < fChapterList->CountRows(); index++) {
		DiscoverRow* row = dynamic_cast<DiscoverRow*>(
			fChapterList->RowAt(index));
		if (!row || row->fUris.empty() || row->fUris[0] != currentUri)
			continue;

		for (int32 next = index + 1; next < fChapterList->CountRows();
				next++) {
			DiscoverRow* nextRow = dynamic_cast<DiscoverRow*>(
				fChapterList->RowAt(next));
			if (nextRow && !nextRow->fUris.empty()
					&& !nextRow->fUris[0].empty()) {
				return nextRow->fUris[0];
			}
		}
		break;
	}
	return "";
}

void
AudiobookWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_LIBRARY_CHANGED:
		{
			std::string uri = message->GetString("uri", "");
			std::string operation = message->GetString("operation", "");
			if (uri == SpotifyUriForItemKind(kSpotifyItemAudiobook,
					fAudiobookId)
					&& (operation == "add" || operation == "remove")) {
				_UpdateSaved(operation == "add");
			}
			break;
		}

		case 'aDat':
		{
			fAudiobookUri = message->GetString("uri", "");
			const char* name = message->GetString("name", B_TRANSLATE("Audiobook"));
			fAudiobookName = name;
			_ApplyTitleText();
			BString windowTitle(B_TRANSLATE("Audiobook: "));
			windowTitle << name;
			SetTitle(windowTitle.String());
			std::string description = message->GetString("description", "");
			ApplyMediaDescription(fDescription, description);
			fDescription->Reflow();
			fDescription->SetLinks(MediaDescriptionLinks(description));
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
					std::string audiobookUri = SpotifyUriForItemKind(
						kSpotifyItemAudiobook, fAudiobookId);
					changed.AddString("uri", audiobookUri.c_str());
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
				std::string audiobookUri = fAudiobookUri.empty()
					? SpotifyUriForItemKind(kSpotifyItemAudiobook,
						fAudiobookId) : fAudiobookUri;
				BMessage play('play');
				play.AddString("uri", uri);
				play.AddString("title", message->GetString("title", ""));
				play.AddString("artist", fName->Text());
				play.AddString(kNowPlayingItemKindField, "chapter");
				play.AddString(kNowPlayingPrimaryOpenUriField,
					audiobookUri.c_str());
				play.AddString(kNowPlayingParentUriField,
					audiobookUri.c_str());
				play.AddString(kNowPlayingParentKindField, "audiobook");
				play.AddString(kNowPlayingAudiobookIdField,
					fAudiobookId.c_str());
				std::string nextUri = _NextPlayableChapterUri(uri);
				if (!nextUri.empty())
					play.AddString("next_queue_uri", nextUri.c_str());
				be_app->PostMessage(&play);
			}
			break;
		}
		case 'rClk':
		{
			const char* uri = message->GetString("uri", "");
			std::string chapterUri = uri ? uri : "";
			if (SpotifyItemKindForUri(chapterUri) != kSpotifyItemEpisode)
				break;
			App* app = dynamic_cast<App*>(be_app);
			SpotifyApi* api = app ? app->GetApi() : nullptr;
			if (!api) break;
			BMessenger self(this);
			api->Content().GetChapter(SpotifyItemIdForUri(chapterUri),
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
			if (target) api->Library().SaveAudiobook(fAudiobookId, done);
			else api->Library().RemoveSavedAudiobook(fAudiobookId, done);
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
