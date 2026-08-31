#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <Window.h>

#include <string>

class BButton;
class BCardLayout;
class BCheckBox;
class BColorControl;
class BFilePanel;
class BListView;
class BMenuField;
class BPopUpMenu;
class BSlider;
class BStringView;
class BTextControl;
class BTextView;
class PlaybackSeekBarView;
class ReplicantColorPreview;
struct HaifySettings;

class SettingsWindow : public BWindow {
public:
								SettingsWindow();
								~SettingsWindow() override;
	virtual void				MessageReceived(BMessage* message);

private:
	void						_InitLayout();
	void						_Load();
	void						_LoadCategory(int32 index,
									const HaifySettings& settings);
	void						_LoadInterfaceCategory(
									const HaifySettings& settings);
	void						_LoadPlaybackCategory(
									const HaifySettings& settings);
	void						_LoadSpotifyCategory(
									const HaifySettings& settings);
	void						_LoadLibrespotCategory(
									const HaifySettings& settings);
	void						_LoadDeviceCategory(
									const HaifySettings& settings);
	void						_LoadImageCacheCategory(
									const HaifySettings& settings);
	bool						_Save();
	void						_BrowseLibrespot();
	void						_BrowseCachePath();
	void						_UpdateAudiobookState();
	void						_LoadSpotifyProfile();
	void						_SelectCategory(int32 index);
	void						_UpdateSeekbarPreview();
	void						_BroadcastSeekbarColor();
	void						_UpdateReplicantPreview();
	void						_BroadcastReplicantAppearance();
	bool						_HandleGeneralMessage(BMessage* message);
	bool						_HandleAppearanceMessage(BMessage* message);
	bool						_HandlePathLibrespotMessage(BMessage* message);
	bool						_HandleSpotifyMessage(BMessage* message);
	void						_RevertCurrentCategory();
	void						_ResetSeekbarColor();
	void						_ResetReplicantColor();
	void						_ApplyDroppedSeekbarColor(BMessage* message);
	void						_ApplyPanelResult(BMessage* message,
									BTextControl* target);
	void						_StartLibrespotFromSettings();
	void						_RegisterLibrespotFromSettings();
	void						_ClearImageCache();
	void						_RetryAudiobookCapabilities();
	void						_ApplySpotifyProfile(BMessage* message);
	void						_OpenSpotifyProfile();

	BListView*					fCategoryList		= nullptr;
	BCardLayout*				fCardLayout		= nullptr;
	BStringView*				fCategoryTitle		= nullptr;
	BStringView*				fCategoryDescription = nullptr;
	BCheckBox*					fUseSystemSeekbarColorCheck = nullptr;
	BCheckBox*					fDeskbarReplicantCheck = nullptr;
	BColorControl*				fSeekbarColorControl = nullptr;
	PlaybackSeekBarView*		fSeekbarPreview		= nullptr;
	BCheckBox*					fUseAutomaticReplicantColorCheck = nullptr;
	BColorControl*				fReplicantColorControl = nullptr;
	ReplicantColorPreview*	fReplicantColorPreview = nullptr;

	BTextControl*				fPathControl		= nullptr;
	BButton*					fBrowseButton		= nullptr;
	BFilePanel*				fLibrespotPanel		= nullptr;
	BCheckBox*					fAlwaysStartCheck	= nullptr;
	BButton*					fOAuthButton		= nullptr;
	BTextView*					fOAuthStatusView	= nullptr;
	BCheckBox*					fDisableDiscoveryCheck = nullptr;
	BTextControl*				fCachePathControl	= nullptr;
	BButton*					fCacheBrowseButton	= nullptr;
	BFilePanel*				fCachePanel		= nullptr;
	BTextControl*				fAdditionalArgsControl = nullptr;


	BMenuField*					fBackendField		= nullptr;
	BPopUpMenu*					fBackendMenu		= nullptr;
	BMenuField*					fBitrateField		= nullptr;
	BPopUpMenu*					fBitrateMenu		= nullptr;
	BSlider*					fVolumeSlider		= nullptr;
	BCheckBox*					fAutoplayCheck		= nullptr;
	BCheckBox*					fNormalizationCheck	= nullptr;

	BTextControl*				fDeviceNameControl	= nullptr;
	BMenuField*					fDeviceTypeField	= nullptr;
	BPopUpMenu*					fDeviceTypeMenu		= nullptr;


	BTextControl*				fImageCacheLimitControl = nullptr;
	BButton*					fClearImageCacheButton = nullptr;
	BMenuField*					fAudiobookModeField = nullptr;
	BPopUpMenu*					fAudiobookModeMenu = nullptr;
	BStringView*					fAudiobookStateView = nullptr;
	BButton*					fAudiobookRetryButton = nullptr;
	BStringView*					fSpotifyAccountView = nullptr;
	BButton*					fOpenSpotifyButton = nullptr;
	std::string					fSpotifyProfileUrl;

};

#endif
