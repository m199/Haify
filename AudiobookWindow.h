#pragma once

#include <Window.h>

#include <string>

class ArtworkView;
class BButton;
class BColumnListView;
class BMenuBar;
class BMenuItem;
class BStringView;
class BTextView;
class MediaDescriptionView;

class AudiobookWindow : public BWindow {
public:
	explicit AudiobookWindow(const std::string& audiobookId);

	const std::string& GetAudiobookId() const { return fAudiobookId; }
	virtual void FrameResized(float width, float height) override;
	virtual void MessageReceived(BMessage* message) override;

private:
	void _Load();
	void _LoadChapters(int32 offset);
	void _LoadArtwork(const std::string& url);
	void _ApplyTitleText();
	void _UpdateSaved(bool saved);
	void _UpdateSavedControls();
	std::string _NextPlayableChapterUri(const std::string& currentUri) const;

	std::string fAudiobookId;
	std::string fAudiobookUri;
	std::string fAudiobookName;
	bool fSaved = false;
	bool fSavedKnown = false;
	bool fSavePending = false;
	bool fLoadingChapters = false;

	ArtworkView* fArtwork = nullptr;
	BMenuBar* fMenuBar = nullptr;
	BMenuItem* fSaveMenuItem = nullptr;
	BTextView* fName = nullptr;
	BStringView* fCredits = nullptr;
	BStringView* fNarrators = nullptr;
	MediaDescriptionView* fDescription = nullptr;
	BButton* fSave = nullptr;
	BColumnListView* fChapterList = nullptr;
};
