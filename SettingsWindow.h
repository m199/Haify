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
