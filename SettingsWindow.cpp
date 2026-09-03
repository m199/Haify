#include "SettingsWindow.h"

#include "App.h"
#include "Config.h"
#include "Messages.h"
#include "PlaybackSeekBarView.h"
#include "SettingsController.h"
#include "network/ImageCache.h"
#include "spotify/api/SpotifyApi.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Font.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <ScrollView.h>
#include <Size.h>
#include <Slider.h>
#include <String.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Url.h>
#include <View.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SettingsWindow"

static const uint32 kMsgSave             = 'save';
static const uint32 kMsgRevert           = 'rvrt';
static const uint32 kMsgCategorySelected = 'ctsl';
static const uint32 kMsgSeekbarColor     = 'skcl';
static const uint32 kMsgSeekbarUseSystem = 'skus';
static const uint32 kMsgSeekbarDefault   = 'skdf';
static const uint32 kMsgReplicantColor   = 'rpcl';
static const uint32 kMsgReplicantAuto    = 'rpau';
static const uint32 kMsgReplicantDefault = 'rpdf';
static const uint32 kMsgBrowse           = 'brow';
static const uint32 kMsgPanelResult      = 'pRes';
static const uint32 kMsgBrowseCache      = 'bCaP';
static const uint32 kMsgCachePanelResult = 'cPrs';
static const uint32 kMsgStartLibrespot   = 'lbSt';
static const uint32 kMsgStopLibrespot    = 'lbSp';
static const uint32 kMsgRegisterLibrespot = 'lbRg';
static const uint32 kMsgClearImageCache  = 'cImg';

enum SettingsCategory {
	kCategoryInterface = 0,
	kCategoryPlayback,
	kCategorySpotify,
	kCategoryLibrespot,
	kCategoryDevice,
	kCategoryImageCache,
	kCategoryCount
};

static const int32 kCategoryOrder[kCategoryCount] = {
	kCategoryInterface,
	kCategoryLibrespot,
	kCategoryPlayback,
	kCategorySpotify,
	kCategoryDevice,
	kCategoryImageCache
};

static const int32 kSettingsInterfaceIcon = 2100;
static const int32 kSettingsPlaybackIcon = 2101;
static const int32 kSettingsLibrespotIcon = 2102;
static const int32 kSettingsDeviceIcon = 2103;
static const int32 kSettingsImageCacheIcon = 2104;
static const int32 kHaifyAppIcon = 101;
static const float kSettingsIconSize = 20.0f;


static const char*
CategoryTitle(int32 index)
{
	switch (index) {
		case kCategoryInterface: return B_TRANSLATE("Interface");
		case kCategoryPlayback: return B_TRANSLATE("Playback");
		case kCategorySpotify: return B_TRANSLATE("Spotify");
		case kCategoryLibrespot: return B_TRANSLATE("Librespot");
		case kCategoryDevice: return B_TRANSLATE("Device");
		case kCategoryImageCache: return B_TRANSLATE("Image Cache");
		default: return "";
	}
}


static const char*
CategoryDescription(int32 index)
{
	switch (index) {
		case kCategoryInterface:
			return B_TRANSLATE("Configure graphical user interface settings");
		case kCategoryPlayback:
			return B_TRANSLATE("Configure librespot playback and audio quality");
		case kCategorySpotify:
			return B_TRANSLATE("Spotify account and feature availability");
		case kCategoryLibrespot:
			return B_TRANSLATE("Configure the local librespot service");
		case kCategoryDevice:
			return B_TRANSLATE("Configure how this device appears in Spotify");
		case kCategoryImageCache:
			return B_TRANSLATE("Configure cached artwork storage");
		default:
			return "";
	}
}


static int32
CategoryIconResource(int32 index)
{
	switch (index) {
		case kCategoryInterface: return kSettingsInterfaceIcon;
		case kCategoryPlayback: return kSettingsPlaybackIcon;
		case kCategorySpotify: return kHaifyAppIcon;
		case kCategoryLibrespot: return kSettingsLibrespotIcon;
		case kCategoryDevice: return kSettingsDeviceIcon;
		case kCategoryImageCache: return kSettingsImageCacheIcon;
		default: return -1;
	}
}


static int32
CategoryForListIndex(int32 index)
{
	if (index < 0 || index >= kCategoryCount)
		return -1;
	return kCategoryOrder[index];
}


static BBitmap*
LoadSettingsIcon(int32 resourceId)
{
	if (!be_app || resourceId < 0)
		return nullptr;

	BResources* resources = be_app->AppResources();
	if (!resources)
		return nullptr;

	size_t dataSize = 0;
	const void* data = resources->LoadResource('VICN', resourceId, &dataSize);
	if (!data || dataSize == 0)
		return nullptr;

	int32 side = (int32)kSettingsIconSize;
	BBitmap* bitmap = new BBitmap(BRect(0, 0, side - 1, side - 1), B_RGBA32);
	if (bitmap->InitCheck() != B_OK
			|| BIconUtils::GetVectorIcon((const uint8*)data, dataSize,
				bitmap) != B_OK) {
		delete bitmap;
		return nullptr;
	}
	return bitmap;
}


class SettingsCategoryItem : public BStringItem {
public:
	SettingsCategoryItem(const char* label, int32 iconResource)
		: BStringItem(label),
		  fIcon(LoadSettingsIcon(iconResource))
	{
		if (!fIcon && iconResource == kHaifyAppIcon)
			fIcon = LoadSettingsIcon(kSettingsInterfaceIcon);
	}

	~SettingsCategoryItem() override
	{
		delete fIcon;
	}

	void DrawItem(BView* owner, BRect frame, bool complete) override
	{
		rgb_color background = IsSelected()
			? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: owner->ViewColor();
		if (complete || IsSelected()) {
			owner->SetHighColor(background);
			owner->FillRect(frame);
		}
		owner->SetLowColor(background);

		float iconWidth = 0.0f;
		if (fIcon) {
			iconWidth = fIcon->Bounds().Width() + 1.0f;
			BPoint iconPoint(frame.left + 6.0f,
				frame.top + std::floor((frame.Height() - fIcon->Bounds().Height())
					/ 2.0f));
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->DrawBitmap(fIcon, iconPoint);
			owner->SetDrawingMode(B_OP_COPY);
		}

		owner->SetHighColor(IsSelected()
			? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR));
		font_height height;
		owner->GetFontHeight(&height);
		float baseline = frame.top
			+ std::floor((frame.Height() - height.ascent - height.descent) / 2.0f)
			+ height.ascent;
		owner->DrawString(Text(), BPoint(frame.left + 12.0f + iconWidth,
			baseline));
	}

private:
	BBitmap* fIcon;
};


class ReplicantColorPreview : public BView {
public:
	ReplicantColorPreview()
		: BView("replicantColorPreview", B_WILL_DRAW)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(180.0f, 58.0f));
		SetExplicitPreferredSize(BSize(260.0f, 58.0f));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 58.0f));
	}

	void SetAppearance(bool automatic, rgb_color color)
	{
		fAutomatic = automatic;
		fColor = color;
		Invalidate();
	}

	void Draw(BRect) override
	{
		rgb_color desktop = ui_color(B_DESKTOP_COLOR);
		SetHighColor(desktop);
		FillRect(Bounds());

		rgb_color color = fColor;
		if (fAutomatic) {
			color = desktop.Brightness() <= 127
				? (rgb_color) { 255, 255, 255, 255 }
				: (rgb_color) { 0, 0, 0, 255 };
		}

		BRect frame = Bounds().InsetByCopy(5.0f, 5.0f);
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(color);
		StrokeRect(frame);
		const char* text = B_TRANSLATE("No Cover");
		font_height height;
		GetFontHeight(&height);
		BPoint where(frame.left + (frame.Width() - StringWidth(text)) / 2.0f,
			frame.top + (frame.Height() + height.ascent - height.descent) / 2.0f);
		DrawString(text, where);
		SetDrawingMode(B_OP_COPY);
	}

private:
	bool fAutomatic = true;
	rgb_color fColor = { 255, 255, 255, 255 };
};


static BStringView*
SectionLabel(const char* name, const char* text)
{
	BStringView* label = new BStringView(name, text);
	BFont font(be_plain_font);
	font.SetFace(B_BOLD_FACE);
	label->SetFont(&font);
	return label;
}


static rgb_color
StoredSeekbarColor(const HaifySettings& settings)
{
	return (rgb_color) {
		(uint8)std::clamp(settings.seekBarColorRed, 0, 255),
		(uint8)std::clamp(settings.seekBarColorGreen, 0, 255),
		(uint8)std::clamp(settings.seekBarColorBlue, 0, 255),
		(uint8)std::clamp(settings.seekBarColorAlpha, 0, 255)
	};
}


static rgb_color
StoredReplicantColor(const HaifySettings& settings)
{
	return (rgb_color) {
		(uint8)std::clamp(settings.replicantColorRed, 0, 255),
		(uint8)std::clamp(settings.replicantColorGreen, 0, 255),
		(uint8)std::clamp(settings.replicantColorBlue, 0, 255),
		(uint8)std::clamp(settings.replicantColorAlpha, 0, 255)
	};
}


SettingsWindow::SettingsWindow()
	: BWindow(BRect(100, 100, 840, 680), B_TRANSLATE("Settings"),
		B_DOCUMENT_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS)
{
	_InitLayout();
	_Load();
	CenterOnScreen();
}


SettingsWindow::~SettingsWindow()
{
	delete fLibrespotPanel;
	delete fCachePanel;

	if (!fCategoryList)
		return;
	while (fCategoryList->CountItems() > 0)
		delete fCategoryList->RemoveItem((int32)0);
}


void
SettingsWindow::_InitLayout()
{
	fCategoryList = new BListView("categories", B_SINGLE_SELECTION_LIST);
	fCategoryList->SetSelectionMessage(new BMessage(kMsgCategorySelected));
	fCategoryList->SetTarget(this);
	for (int32 i = 0; i < kCategoryCount; i++) {
		int32 category = CategoryForListIndex(i);
		SettingsCategoryItem* item = new SettingsCategoryItem(
			CategoryTitle(category), CategoryIconResource(category));
		fCategoryList->AddItem(item);
		item->SetHeight(std::max(item->Height() + 8.0f,
			kSettingsIconSize + 6.0f));
	}

	BScrollView* categoryScroll = new BScrollView("categoryScroll",
		fCategoryList, 0, false, true, B_FANCY_BORDER);
	categoryScroll->SetExplicitMinSize(BSize(155.0f, 360.0f));
	categoryScroll->SetExplicitPreferredSize(BSize(170.0f, 500.0f));
	categoryScroll->SetExplicitMaxSize(BSize(190.0f, B_SIZE_UNLIMITED));

	fCategoryTitle = new BStringView("categoryTitle", "");
	BFont titleFont(be_plain_font);
	titleFont.SetFace(B_BOLD_FACE);
	titleFont.SetSize(be_plain_font->Size() + 1.0f);
	fCategoryTitle->SetFont(&titleFont);
	fCategoryTitle->SetHighColor((rgb_color) { 180, 35, 25, 255 });
	fCategoryTitle->SetViewUIColor(B_TOOL_TIP_BACKGROUND_COLOR);

	fCategoryDescription = new BStringView("categoryDescription", "");
	fCategoryDescription->SetViewUIColor(B_TOOL_TIP_BACKGROUND_COLOR);

	BBox* headerBox = new BBox("categoryHeader");
	headerBox->SetBorder(B_FANCY_BORDER);
	headerBox->SetViewUIColor(B_TOOL_TIP_BACKGROUND_COLOR);
	BLayoutBuilder::Group<>(headerBox, B_VERTICAL, 2.0f)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
		.Add(fCategoryTitle)
		.Add(fCategoryDescription)
	.End();

	BView* cardContainer = new BView("settingsCards", B_SUPPORTS_LAYOUT);
	fCardLayout = new BCardLayout();
	cardContainer->SetLayout(fCardLayout);
	BBox* contentBox = new BBox("settingsContent");
	contentBox->SetBorder(B_PLAIN_BORDER);
	BLayoutBuilder::Group<>(contentBox, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(cardContainer)
	.End();

	{
		fDeskbarReplicantCheck = new BCheckBox("deskbarReplicant",
			B_TRANSLATE("Show Haify in Deskbar"), nullptr);
		fUseSystemSeekbarColorCheck = new BCheckBox("useSystemSeekbarColor",
			B_TRANSLATE("Use the system accent color"),
			new BMessage(kMsgSeekbarUseSystem));
		fSeekbarColorControl = new BColorControl(BPoint(0, 0),
			B_CELLS_32x8, 8.0f, "seekbarColor",
			new BMessage(kMsgSeekbarColor));
		fSeekbarColorControl->SetTarget(this);
		BButton* seekbarDefaultButton = new BButton("seekbarDefault",
			B_TRANSLATE("Default"), new BMessage(kMsgSeekbarDefault));

		fSeekbarPreview = new PlaybackSeekBarView("seekbarPreview");
		fSeekbarPreview->SetDuration(1000000LL);
		fSeekbarPreview->SetPosition(620000LL);
		fSeekbarPreview->SetExplicitPreferredSize(BSize(300.0f, 20.0f));
		fSeekbarPreview->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 20.0f));

		fUseAutomaticReplicantColorCheck = new BCheckBox(
			"useAutomaticReplicantColor",
			B_TRANSLATE("Use automatic contrast color"),
			new BMessage(kMsgReplicantAuto));
		fReplicantColorControl = new BColorControl(BPoint(0, 0),
			B_CELLS_32x8, 8.0f, "replicantColor",
			new BMessage(kMsgReplicantColor));
		fReplicantColorControl->SetTarget(this);
		BButton* replicantDefaultButton = new BButton("replicantDefault",
			B_TRANSLATE("Default"), new BMessage(kMsgReplicantDefault));
		fReplicantColorPreview = new ReplicantColorPreview();

		BView* page = new BView("interfacePage", B_SUPPORTS_LAYOUT);
		BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(SectionLabel("deskbarSection", B_TRANSLATE("Deskbar")))
			.Add(fDeskbarReplicantCheck)
			.AddStrut(B_USE_SMALL_SPACING)
			.Add(SectionLabel("seekbarSection",
				B_TRANSLATE("Seekbar and volume color")))
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(fUseSystemSeekbarColorCheck)
				.AddGlue()
				.Add(seekbarDefaultButton)
			.End()
			.AddGroup(B_HORIZONTAL)
				.AddStrut(B_USE_DEFAULT_SPACING)
				.Add(fSeekbarColorControl)
				.AddGlue()
			.End()
			.Add(SectionLabel("seekbarPreviewLabel", B_TRANSLATE("Preview")))
			.Add(fSeekbarPreview)
			.AddStrut(B_USE_SMALL_SPACING)
			.Add(SectionLabel("replicantColorSection",
				B_TRANSLATE("Desktop Replicants frame and text color")))
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(fUseAutomaticReplicantColorCheck)
				.AddGlue()
				.Add(replicantDefaultButton)
			.End()
			.AddGroup(B_HORIZONTAL)
				.AddStrut(B_USE_DEFAULT_SPACING)
				.Add(fReplicantColorControl)
				.AddGlue()
			.End()
			.Add(fReplicantColorPreview)
			.AddGlue()
		.End();
		fCardLayout->AddView(page);
	}

	{
		fBackendMenu = new BPopUpMenu("backend");
		fBackendMenu->AddItem(new BMenuItem("sdl", nullptr));
		fBackendField = new BMenuField("backendField",
			B_TRANSLATE("Audio Backend:"), fBackendMenu);

		fBitrateMenu = new BPopUpMenu("bitrate");
		fBitrateMenu->AddItem(new BMenuItem("96 kbit/s", nullptr));
		fBitrateMenu->AddItem(new BMenuItem("160 kbit/s", nullptr));
		fBitrateMenu->AddItem(new BMenuItem("320 kbit/s", nullptr));
		fBitrateField = new BMenuField("bitrateField",
			B_TRANSLATE("Bitrate:"), fBitrateMenu);

		fVolumeSlider = new BSlider("volume",
			B_TRANSLATE("Initial Volume:"), nullptr, 0, 100, B_HORIZONTAL);
		fVolumeSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		fVolumeSlider->SetHashMarkCount(11);
		fVolumeSlider->SetLimitLabels("0", "100");

		fAutoplayCheck = new BCheckBox("autoplay",
			B_TRANSLATE("Use librespot autoplay"), nullptr);
		fNormalizationCheck = new BCheckBox("normalization",
			B_TRANSLATE("Enable volume normalization"), nullptr);

		BView* page = new BView("playbackPage", B_SUPPORTS_LAYOUT);
		BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(fBackendField)
			.Add(fBitrateField)
			.Add(fVolumeSlider)
			.Add(fAutoplayCheck)
			.Add(fNormalizationCheck)
			.AddGlue()
		.End();
		fCardLayout->AddView(page);
	}

	{
		fAudiobookModeMenu = new BPopUpMenu("audiobookMode");
		fAudiobookModeMenu->AddItem(new BMenuItem(B_TRANSLATE("Auto"), nullptr));
		fAudiobookModeMenu->AddItem(new BMenuItem(B_TRANSLATE("Enabled"), nullptr));
		fAudiobookModeMenu->AddItem(new BMenuItem(B_TRANSLATE("Disabled"), nullptr));
		fAudiobookModeField = new BMenuField("audiobookModeField",
			B_TRANSLATE("Audiobooks:"), fAudiobookModeMenu);
		fAudiobookStateView = new BStringView("audiobookState", "");
		fAudiobookRetryButton = new BButton("audiobookRetry",
			B_TRANSLATE("Check Again"), new BMessage('abRp'));
		fSpotifyAccountView = new BStringView("spotifyAccount",
			B_TRANSLATE("Connected account: loading" B_UTF8_ELLIPSIS));
		fOpenSpotifyButton = new BButton("openSpotifyProfile",
			B_TRANSLATE("Open Spotify Profile"), new BMessage('spOp'));
		fOpenSpotifyButton->SetEnabled(false);

		BView* page = new BView("spotifyPage", B_SUPPORTS_LAYOUT);
		BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(SectionLabel("accountSection", B_TRANSLATE("Account")))
			.Add(fSpotifyAccountView)
			.AddGroup(B_HORIZONTAL)
				.Add(fOpenSpotifyButton)
				.AddGlue()
			.End()
			.AddStrut(B_USE_DEFAULT_SPACING)
			.Add(SectionLabel("featuresSection", B_TRANSLATE("Features")))
			.Add(fAudiobookModeField)
			.Add(fAudiobookStateView)
			.AddGroup(B_HORIZONTAL)
				.Add(fAudiobookRetryButton)
				.AddGlue()
			.End()
			.AddGlue()
		.End();
		fCardLayout->AddView(page);
	}

	{
		fPathControl = new BTextControl("path",
			B_TRANSLATE("librespot Path:"), "", nullptr);
		fPathControl->SetExplicitMinSize(BSize(260, B_SIZE_UNSET));
		fBrowseButton = new BButton("browse",
			B_TRANSLATE("Browse" B_UTF8_ELLIPSIS), new BMessage(kMsgBrowse));
		fAlwaysStartCheck = new BCheckBox("alwaysStart",
			B_TRANSLATE("Always start librespot on launch"), nullptr);
		fOAuthButton = new BButton("oauth",
			B_TRANSLATE("Register librespot" B_UTF8_ELLIPSIS),
			new BMessage(kMsgRegisterLibrespot));
		fOAuthButton->SetToolTip(B_TRANSLATE(
			"Restart librespot in OAuth mode and open Spotify in your browser."));
		fOAuthStatusView = new BTextView("oauthStatus");
		fOAuthStatusView->MakeEditable(false);
		fOAuthStatusView->MakeSelectable(false);
		fOAuthStatusView->SetWordWrap(true);
		fOAuthStatusView->SetInsets(0, 0, 0, 0);
		fOAuthStatusView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		fOAuthStatusView->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
		fOAuthStatusView->SetExplicitMinSize(BSize(260.0f, 36.0f));
		fOAuthStatusView->SetExplicitPreferredSize(BSize(360.0f, 36.0f));
		fOAuthStatusView->SetExplicitMaxSize(
			BSize(B_SIZE_UNLIMITED, 36.0f));
		fDisableDiscoveryCheck = new BCheckBox("disableDiscovery",
			B_TRANSLATE("Disable Zeroconf/mDNS discovery"), nullptr);
		fCachePathControl = new BTextControl("cachePath",
			B_TRANSLATE("Cache Path:"), "", nullptr);
		fCachePathControl->SetExplicitMinSize(BSize(260, B_SIZE_UNSET));
		fCachePathControl->SetToolTip(
			B_TRANSLATE("Leave empty to use the default cache location"));
		fCacheBrowseButton = new BButton("browseCache",
			B_TRANSLATE("Browse" B_UTF8_ELLIPSIS),
			new BMessage(kMsgBrowseCache));
		fAdditionalArgsControl = new BTextControl("additionalArgs",
			B_TRANSLATE("Additional Arguments:"), "", nullptr);

		BButton* startButton = new BButton("start",
			B_TRANSLATE("Start librespot"), new BMessage(kMsgStartLibrespot));
		BButton* stopButton = new BButton("stop",
			B_TRANSLATE("Stop librespot"), new BMessage(kMsgStopLibrespot));

		BView* page = new BView("librespotPage", B_SUPPORTS_LAYOUT);
		BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
				.AddTextControl(fPathControl, 0, 0, B_ALIGN_RIGHT)
				.Add(fBrowseButton, 2, 0)
				.AddTextControl(fCachePathControl, 0, 1, B_ALIGN_RIGHT)
				.Add(fCacheBrowseButton, 2, 1)
				.AddTextControl(fAdditionalArgsControl, 0, 2, B_ALIGN_RIGHT)
				.SetColumnWeight(1, 1.0f)
			.End()
			.Add(fAlwaysStartCheck)
			.Add(fDisableDiscoveryCheck)
			.Add(SectionLabel("oauthSection",
				B_TRANSLATE("Spotify")))
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(fOAuthButton)
				.AddGlue()
			.End()
			.Add(fOAuthStatusView)
			.AddGlue()
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(startButton)
				.Add(stopButton)
				.AddGlue()
			.End()
		.End();
		fCardLayout->AddView(page);
	}

	{
		fDeviceNameControl = new BTextControl("deviceName",
			B_TRANSLATE("Device Name:"), "", nullptr);
		fDeviceNameControl->SetToolTip(
			B_TRANSLATE("Leave empty to use the default device name"));

		fDeviceTypeMenu = new BPopUpMenu("deviceType");
		const char* types[] = {
			"computer", "tablet", "smartphone", "speaker", "tv", "avr",
			"stb", "audio_dongle", "game_console", "cast_video",
			"cast_audio", "automobile", nullptr
		};
		for (int i = 0; types[i]; i++)
			fDeviceTypeMenu->AddItem(new BMenuItem(types[i], nullptr));
		fDeviceTypeField = new BMenuField("deviceTypeField",
			B_TRANSLATE("Device Type:"), fDeviceTypeMenu);

		BView* page = new BView("devicePage", B_SUPPORTS_LAYOUT);
		BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(fDeviceNameControl)
			.Add(fDeviceTypeField)
			.AddGlue()
		.End();
		fCardLayout->AddView(page);
	}

	{
		fImageCacheLimitControl = new BTextControl("imageCacheLimit",
			B_TRANSLATE("Image Cache Limit (MB):"), "", nullptr);
		fImageCacheLimitControl->SetToolTip(
			B_TRANSLATE("Use 0 for unlimited image cache size"));
		fClearImageCacheButton = new BButton("clearImageCache",
			B_TRANSLATE("Clear image cache"),
			new BMessage(kMsgClearImageCache));

		BView* page = new BView("imageCachePage", B_SUPPORTS_LAYOUT);
		BLayoutBuilder::Group<>(page, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(fImageCacheLimitControl)
			.AddGroup(B_HORIZONTAL)
				.Add(fClearImageCacheButton)
				.AddGlue()
			.End()
			.AddGlue()
		.End();
		fCardLayout->AddView(page);
	}

	BButton* revertButton = new BButton("revert",
		B_TRANSLATE("Revert"), new BMessage(kMsgRevert));
	BButton* cancelButton = new BButton("cancel",
		B_TRANSLATE("Discard"), new BMessage(B_QUIT_REQUESTED));
	BButton* saveButton = new BButton("save",
		B_TRANSLATE("Save"), new BMessage(kMsgSave));
	SetDefaultButton(saveButton);

	BView* rightPanel = new BView("rightPanel", B_SUPPORTS_LAYOUT);
	BLayoutBuilder::Group<>(rightPanel, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(headerBox)
		.Add(contentBox, 1.0f)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(revertButton)
			.AddGlue()
			.Add(cancelButton)
			.Add(saveButton)
		.End()
	.End();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 1.0f)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(categoryScroll)
			.Add(rightPanel, 1.0f)
		.End()
	.End();

	SetSizeLimits(680.0f, 1400.0f, 520.0f, 1000.0f);
	fCategoryList->Select(0);
	_SelectCategory(0);
}


void
SettingsWindow::_Load()
{
	HaifySettings settings = SettingsController::Load();
	for (int32 i = 0; i < kCategoryCount; i++)
		_LoadCategory(i, settings);
	_LoadSpotifyProfile();
}


void
SettingsWindow::_LoadCategory(int32 index, const HaifySettings& settings)
{
	switch (index) {
		case kCategoryInterface:
			_LoadInterfaceCategory(settings);
			break;

		case kCategoryPlayback:
			_LoadPlaybackCategory(settings);
			break;

		case kCategorySpotify:
			_LoadSpotifyCategory(settings);
			break;

		case kCategoryLibrespot:
			_LoadLibrespotCategory(settings);
			break;

		case kCategoryDevice:
			_LoadDeviceCategory(settings);
			break;

		case kCategoryImageCache:
			_LoadImageCacheCategory(settings);
			break;
	}
}


void
SettingsWindow::_LoadInterfaceCategory(const HaifySettings& settings)
{
	fDeskbarReplicantCheck->SetValue(settings.deskbarReplicantEnabled
		? B_CONTROL_ON : B_CONTROL_OFF);
	fUseAutomaticReplicantColorCheck->SetValue(
		settings.replicantUseAutomaticColor ? B_CONTROL_ON : B_CONTROL_OFF);
	fReplicantColorControl->SetValue(StoredReplicantColor(settings));
	fUseSystemSeekbarColorCheck->SetValue(settings.seekBarUseSystemColor
		? B_CONTROL_ON : B_CONTROL_OFF);
	fSeekbarColorControl->SetValue(StoredSeekbarColor(settings));
	_UpdateSeekbarPreview();
	_UpdateReplicantPreview();
}


void
SettingsWindow::_LoadPlaybackCategory(const HaifySettings& settings)
{
	BMenuItem* backend = fBackendMenu->FindItem(
		settings.librespotBackend.c_str());
	if (!backend || settings.librespotBackend.empty())
		backend = fBackendMenu->ItemAt(0);
	if (backend)
		backend->SetMarked(true);

	const char* bitrateLabel = "320 kbit/s";
	if (settings.librespotBitrate == 96)
		bitrateLabel = "96 kbit/s";
	else if (settings.librespotBitrate == 160)
		bitrateLabel = "160 kbit/s";
	BMenuItem* bitrate = fBitrateMenu->FindItem(bitrateLabel);
	if (bitrate)
		bitrate->SetMarked(true);

	fVolumeSlider->SetValue(settings.librespotVolume);
	fAutoplayCheck->SetValue(settings.librespotAutoplay
		? B_CONTROL_ON : B_CONTROL_OFF);
	fNormalizationCheck->SetValue(settings.librespotNormalization
		? B_CONTROL_ON : B_CONTROL_OFF);
}


void
SettingsWindow::_LoadSpotifyCategory(const HaifySettings& settings)
{
	int audiobookIndex = settings.audiobookMode;
	if (audiobookIndex < 0
			|| audiobookIndex >= fAudiobookModeMenu->CountItems()) {
		audiobookIndex = 0;
	}
	BMenuItem* item = fAudiobookModeMenu->ItemAt(audiobookIndex);
	if (item)
		item->SetMarked(true);
	_UpdateAudiobookState();
}


void
SettingsWindow::_LoadLibrespotCategory(const HaifySettings& settings)
{
	std::string path = settings.librespotPath.empty()
		? SettingsController::FindLibrespotPath()
		: settings.librespotPath;
	fPathControl->SetText(path.c_str());
	fAlwaysStartCheck->SetValue(settings.librespotAlwaysStart
		? B_CONTROL_ON : B_CONTROL_OFF);
	fOAuthStatusView->SetText(SettingsController::CredentialsExist(settings)
		? B_TRANSLATE("Account registered")
		: B_TRANSLATE("No account registered"));
	fDisableDiscoveryCheck->SetValue(settings.librespotDisableDiscovery
		? B_CONTROL_ON : B_CONTROL_OFF);
	std::string cachePath = settings.librespotCachePath.empty()
		? SettingsController::DefaultCachePath()
		: settings.librespotCachePath;
	fCachePathControl->SetText(cachePath.c_str());
	fAdditionalArgsControl->SetText(
		settings.librespotAdditionalArgs.c_str());
}


void
SettingsWindow::_LoadDeviceCategory(const HaifySettings& settings)
{
	fDeviceNameControl->SetText(settings.librespotDeviceName.empty()
		? LIBRESPOT_DEVICE_NAME : settings.librespotDeviceName.c_str());
	BMenuItem* deviceType = fDeviceTypeMenu->FindItem(
		settings.librespotDeviceType.c_str());
	if (!deviceType)
		deviceType = fDeviceTypeMenu->ItemAt(0);
	if (deviceType)
		deviceType->SetMarked(true);
}


void
SettingsWindow::_LoadImageCacheCategory(const HaifySettings& settings)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%d", settings.imageCacheLimitMB);
	fImageCacheLimitControl->SetText(buffer);
}


void
SettingsWindow::_LoadSpotifyProfile()
{
	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;
	BMessenger self(this);
	api->Profile().GetCurrentUserProfile([self](bool ok,
			const nlohmann::json& profile) {
		BMessage message('spPf');
		message.AddBool("ok", ok);
		if (ok && profile.is_object()) {
			message.AddString("account_id", profile.value("account_id",
				profile.value("id", "")).c_str());
			message.AddString("name", profile.value("display_name",
				profile.value("account_id", profile.value("id", ""))).c_str());
			if (profile.contains("external_urls")
					&& profile["external_urls"].is_object()) {
				message.AddString("url",
					profile["external_urls"].value("spotify", "").c_str());
			}
		}
		self.SendMessage(&message);
	});
}


void
SettingsWindow::_UpdateAudiobookState()
{
	if (!fAudiobookStateView)
		return;
	App* app = dynamic_cast<App*>(be_app);
	if (!app || !app->GetCapabilities()) {
		fAudiobookStateView->SetText(B_TRANSLATE("Availability: unknown"));
		return;
	}
	const char* text = B_TRANSLATE("Availability: unknown");
	switch (app->GetCapabilities()->AudiobookState()) {
		case kAudiobookAvailable:
			text = B_TRANSLATE("Availability: available"); break;
		case kAudiobookUnavailable:
			text = B_TRANSLATE("Availability: unavailable in this account market"); break;
		case kAudiobookForbidden:
			text = B_TRANSLATE("Availability: forbidden by Spotify"); break;
		case kAudiobookTemporaryError:
			text = B_TRANSLATE("Availability: temporarily unknown"); break;
		default:
			break;
	}
	BString label(text);
	time_t checked = app->GetCapabilities()->LastAudiobookCheck();
	if (checked > 0) {
		char timeText[64];
		struct tm localTime;
		localtime_r(&checked, &localTime);
		if (strftime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M",
				&localTime) > 0) {
			label << " - " << B_TRANSLATE("last checked ") << timeText;
		}
	}
	fAudiobookStateView->SetText(label.String());
}


bool
SettingsWindow::_Save()
{
	int imageCacheLimitMB = 0;
	bool deskbarEnabled
		= fDeskbarReplicantCheck->Value() == B_CONTROL_ON;
	bool deskbarChanged = SettingsController::Load().deskbarReplicantEnabled
		!= deskbarEnabled;
	status_t status = SettingsController::Update([&](HaifySettings& settings) {
		settings.deskbarReplicantEnabled = deskbarEnabled;
		settings.replicantUseAutomaticColor
			= fUseAutomaticReplicantColorCheck->Value() == B_CONTROL_ON;
		rgb_color replicantColor = fReplicantColorControl->ValueAsColor();
		settings.replicantColorRed = replicantColor.red;
		settings.replicantColorGreen = replicantColor.green;
		settings.replicantColorBlue = replicantColor.blue;
		settings.replicantColorAlpha = replicantColor.alpha;
		settings.seekBarUseSystemColor
			= fUseSystemSeekbarColorCheck->Value() == B_CONTROL_ON;
		rgb_color color = fSeekbarColorControl->ValueAsColor();
		settings.seekBarColorRed = color.red;
		settings.seekBarColorGreen = color.green;
		settings.seekBarColorBlue = color.blue;
		settings.seekBarColorAlpha = color.alpha;

		settings.librespotPath = fPathControl->Text();
		settings.librespotAlwaysStart
			= fAlwaysStartCheck->Value() == B_CONTROL_ON;
		BMenuItem* backend = fBackendMenu->FindMarked();
		settings.librespotBackend = backend ? backend->Label() : "sdl";

		BMenuItem* bitrate = fBitrateMenu->FindMarked();
		settings.librespotBitrate = 320;
		if (bitrate) {
			std::string label = bitrate->Label();
			if (label.find("96") != std::string::npos)
				settings.librespotBitrate = 96;
			else if (label.find("160") != std::string::npos)
				settings.librespotBitrate = 160;
		}

		settings.librespotVolume = fVolumeSlider->Value();
		settings.librespotAutoplay
			= fAutoplayCheck->Value() == B_CONTROL_ON;
		settings.librespotNormalization
			= fNormalizationCheck->Value() == B_CONTROL_ON;
		settings.librespotDeviceName = fDeviceNameControl->Text();
		BMenuItem* deviceType = fDeviceTypeMenu->FindMarked();
		settings.librespotDeviceType
			= deviceType ? deviceType->Label() : "computer";
		settings.librespotDisableDiscovery
			= fDisableDiscoveryCheck->Value() == B_CONTROL_ON;
		settings.librespotCachePath = fCachePathControl->Text();
		settings.librespotAdditionalArgs = fAdditionalArgsControl->Text();
		settings.imageCacheLimitMB = std::atoi(fImageCacheLimitControl->Text());
		if (settings.imageCacheLimitMB < 0)
			settings.imageCacheLimitMB = 0;
		imageCacheLimitMB = settings.imageCacheLimitMB;

		BMenuItem* audiobookItem = fAudiobookModeMenu->FindMarked();
		settings.audiobookMode = audiobookItem
			? fAudiobookModeMenu->IndexOf(audiobookItem) : kAudiobookAuto;
	});
	if (status != B_OK) {
		BAlert* alert = new BAlert("Haify",
			B_TRANSLATE("The settings could not be saved."),
			B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		return false;
	}

	ImageCache::SetMaxCacheBytes((int64)imageCacheLimitMB * 1024LL * 1024LL);
	if (deskbarChanged) {
		BMessage deskbarChangedMessage(MSG_DESKBAR_REPLICANT_CHANGED);
		deskbarChangedMessage.AddBool("enabled", deskbarEnabled);
		be_app->PostMessage(&deskbarChangedMessage);
	}
	_BroadcastReplicantAppearance();
	_BroadcastSeekbarColor();
	BMessage capabilities(MSG_SPOTIFY_CAPABILITIES_CHANGED);
	capabilities.AddBool("force", true);
	be_app->PostMessage(&capabilities);
	return true;
}


void
SettingsWindow::_SelectCategory(int32 index)
{
	int32 category = CategoryForListIndex(index);
	if (category < 0)
		return;
	fCardLayout->SetVisibleItem(category);
	fCategoryTitle->SetText(CategoryTitle(category));
	fCategoryDescription->SetText(CategoryDescription(category));
}


void
SettingsWindow::_UpdateSeekbarPreview()
{
	bool useSystem = fUseSystemSeekbarColorCheck->Value() == B_CONTROL_ON;
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color fill = useSystem
		? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
		: fSeekbarColorControl->ValueAsColor();
	fSeekbarPreview->SetColors(panel, fill);
}


void
SettingsWindow::_BroadcastSeekbarColor()
{
	rgb_color color = fSeekbarColorControl->ValueAsColor();
	BMessage changed(MSG_SEEKBAR_COLOR_CHANGED);
	changed.AddBool("use_system",
		fUseSystemSeekbarColorCheck->Value() == B_CONTROL_ON);
	changed.AddInt32("red", color.red);
	changed.AddInt32("green", color.green);
	changed.AddInt32("blue", color.blue);
	changed.AddInt32("alpha", color.alpha);
	be_app->PostMessage(&changed);
}


void
SettingsWindow::_UpdateReplicantPreview()
{
	bool automatic
		= fUseAutomaticReplicantColorCheck->Value() == B_CONTROL_ON;
	fReplicantColorPreview->SetAppearance(automatic,
		fReplicantColorControl->ValueAsColor());
}


void
SettingsWindow::_BroadcastReplicantAppearance()
{
	rgb_color color = fReplicantColorControl->ValueAsColor();
	BMessage changed(MSG_REPLICANT_APPEARANCE_CHANGED);
	changed.AddBool("appearance_automatic",
		fUseAutomaticReplicantColorCheck->Value() == B_CONTROL_ON);
	changed.AddInt32("appearance_red", color.red);
	changed.AddInt32("appearance_green", color.green);
	changed.AddInt32("appearance_blue", color.blue);
	changed.AddInt32("appearance_alpha", color.alpha);
	changed.AddBool("automatic",
		fUseAutomaticReplicantColorCheck->Value() == B_CONTROL_ON);
	changed.AddInt32("red", color.red);
	changed.AddInt32("green", color.green);
	changed.AddInt32("blue", color.blue);
	changed.AddInt32("alpha", color.alpha);
	BMessenger app(HAIFY_MIME_SIG);
	if (!app.IsValid() || app.SendMessage(&changed) != B_OK)
		be_app->PostMessage(&changed);
}


void
SettingsWindow::_BrowseLibrespot()
{
	if (!fLibrespotPanel) {
		fLibrespotPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			nullptr, B_FILE_NODE, false, new BMessage(kMsgPanelResult));
	}
	fLibrespotPanel->Show();
}


void
SettingsWindow::_BrowseCachePath()
{
	if (!fCachePanel) {
		fCachePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			nullptr, B_DIRECTORY_NODE, false,
			new BMessage(kMsgCachePanelResult));
	}
	fCachePanel->Show();
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	if (_HandleGeneralMessage(message) || _HandleAppearanceMessage(message)
			|| _HandlePathLibrespotMessage(message)
			|| _HandleSpotifyMessage(message)) {
		return;
	}

	BWindow::MessageReceived(message);
}


bool
SettingsWindow::_HandleGeneralMessage(BMessage* message)
{
	switch (message->what) {
		case kMsgSave:
			if (_Save())
				Quit();
			return true;

		case kMsgRevert:
			_RevertCurrentCategory();
			return true;

		case kMsgCategorySelected:
			_SelectCategory(fCategoryList->CurrentSelection());
			return true;

		case kMsgClearImageCache:
			_ClearImageCache();
			return true;

		default:
			return false;
	}
}


bool
SettingsWindow::_HandleAppearanceMessage(BMessage* message)
{
	switch (message->what) {
		case kMsgSeekbarColor:
			fUseSystemSeekbarColorCheck->SetValue(B_CONTROL_OFF);
			_UpdateSeekbarPreview();
			return true;

		case kMsgSeekbarUseSystem:
			_UpdateSeekbarPreview();
			return true;

		case kMsgSeekbarDefault:
			_ResetSeekbarColor();
			return true;

		case kMsgReplicantColor:
			fUseAutomaticReplicantColorCheck->SetValue(B_CONTROL_OFF);
			_UpdateReplicantPreview();
			return true;

		case kMsgReplicantAuto:
			_UpdateReplicantPreview();
			return true;

		case kMsgReplicantDefault:
			_ResetReplicantColor();
			return true;

		case MSG_SEEKBAR_COLOR_DROPPED:
			_ApplyDroppedSeekbarColor(message);
			return true;

		case B_COLORS_UPDATED:
			_UpdateSeekbarPreview();
			_UpdateReplicantPreview();
			BWindow::MessageReceived(message);
			return true;

		default:
			return false;
	}
}


bool
SettingsWindow::_HandlePathLibrespotMessage(BMessage* message)
{
	switch (message->what) {
		case kMsgBrowse:
			_BrowseLibrespot();
			return true;

		case kMsgBrowseCache:
			_BrowseCachePath();
			return true;

		case kMsgPanelResult:
			_ApplyPanelResult(message, fPathControl);
			return true;

		case kMsgCachePanelResult:
			_ApplyPanelResult(message, fCachePathControl);
			return true;

		case kMsgStartLibrespot:
			_StartLibrespotFromSettings();
			return true;

		case kMsgRegisterLibrespot:
			_RegisterLibrespotFromSettings();
			return true;

		case 'lbOk':
			fOAuthStatusView->SetText(B_TRANSLATE("Account registered"));
			return true;

		case kMsgStopLibrespot:
			be_app->PostMessage(kMsgStopLibrespot);
			return true;

		default:
			return false;
	}
}


bool
SettingsWindow::_HandleSpotifyMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_SPOTIFY_CAPABILITIES_CHANGED:
			_UpdateAudiobookState();
			return true;

		case 'abRp':
			_RetryAudiobookCapabilities();
			return true;

		case 'spPf':
			_ApplySpotifyProfile(message);
			return true;

		case 'spOp':
			_OpenSpotifyProfile();
			return true;

		default:
			return false;
	}
}


void
SettingsWindow::_RevertCurrentCategory()
{
	int32 category = CategoryForListIndex(fCategoryList->CurrentSelection());
	_LoadCategory(category, SettingsController::Load());
}


void
SettingsWindow::_ResetSeekbarColor()
{
	fUseSystemSeekbarColorCheck->SetValue(B_CONTROL_OFF);
	fSeekbarColorControl->SetValue((rgb_color) {
		(uint8)kDefaultSeekBarColorRed,
		(uint8)kDefaultSeekBarColorGreen,
		(uint8)kDefaultSeekBarColorBlue,
		(uint8)kDefaultSeekBarColorAlpha
	});
	_UpdateSeekbarPreview();
}


void
SettingsWindow::_ResetReplicantColor()
{
	fUseAutomaticReplicantColorCheck->SetValue(B_CONTROL_ON);
	fReplicantColorControl->SetValue((rgb_color) {
		(uint8)kDefaultReplicantColorRed,
		(uint8)kDefaultReplicantColorGreen,
		(uint8)kDefaultReplicantColorBlue,
		(uint8)kDefaultReplicantColorAlpha
	});
	_UpdateReplicantPreview();
}


void
SettingsWindow::_ApplyDroppedSeekbarColor(BMessage* message)
{
	const rgb_color* color = nullptr;
	ssize_t colorSize = 0;
	if (message->FindData("color", B_RGB_COLOR_TYPE, (const void**)&color,
			&colorSize) != B_OK || colorSize != sizeof(rgb_color)) {
		return;
	}

	fUseSystemSeekbarColorCheck->SetValue(B_CONTROL_OFF);
	fSeekbarColorControl->SetValue(*color);
	_UpdateSeekbarPreview();
}


void
SettingsWindow::_ApplyPanelResult(BMessage* message, BTextControl* target)
{
	entry_ref ref;
	if (message->FindRef("refs", &ref) != B_OK)
		return;

	BEntry entry(&ref);
	BPath path;
	if (entry.GetPath(&path) == B_OK)
		target->SetText(path.Path());
}


void
SettingsWindow::_StartLibrespotFromSettings()
{
	if (!_Save())
		return;

	BMessage start(kMsgStartLibrespot);
	start.AddBool("restart", true);
	be_app->PostMessage(&start);
}


void
SettingsWindow::_RegisterLibrespotFromSettings()
{
	if (!_Save())
		return;

	be_app->PostMessage(kMsgRegisterLibrespot);
	fOAuthStatusView->SetText(
		B_TRANSLATE("Complete registration in your browser"));
}


void
SettingsWindow::_ClearImageCache()
{
	status_t status = ImageCache::Clear();
	if (status == B_OK) {
		BAlert* alert = new BAlert("Haify",
			B_TRANSLATE("Image cache cleared."), B_TRANSLATE("OK"));
		alert->Go();
		return;
	}

	BAlert* alert = new BAlert("Haify",
		B_TRANSLATE("Could not clear the image cache."), B_TRANSLATE("OK"),
		nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
}


void
SettingsWindow::_RetryAudiobookCapabilities()
{
	BMessage capabilities(MSG_SPOTIFY_CAPABILITIES_CHANGED);
	capabilities.AddBool("force", true);
	be_app->PostMessage(&capabilities);
}


void
SettingsWindow::_ApplySpotifyProfile(BMessage* message)
{
	if (!message->GetBool("ok", false)) {
		fSpotifyAccountView->SetText(
			B_TRANSLATE("Connected account: unavailable"));
		return;
	}

	BString label(B_TRANSLATE("Connected account: "));
	const char* name = message->GetString("name", "Spotify");
	const char* accountId = message->GetString("account_id", "");
	label << name;
	if (accountId[0] && strcmp(name, accountId) != 0)
		label << " (" << accountId << ")";
	fSpotifyAccountView->SetText(label.String());
	fSpotifyProfileUrl = message->GetString("url", "");
	fOpenSpotifyButton->SetEnabled(!fSpotifyProfileUrl.empty());
}


void
SettingsWindow::_OpenSpotifyProfile()
{
	if (fSpotifyProfileUrl.empty())
		return;

	BUrl url(fSpotifyProfileUrl.c_str(), false);
	url.OpenWithPreferredApplication(false);
}
