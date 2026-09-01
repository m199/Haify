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
	void _UpdateResumeControl();
	bool _FindResumeChapter(std::string& uri, std::string& title,
		int32& startPositionMs) const;
	void _AddFollowingChapterQueue(BMessage& play,
		const std::string& currentUri) const;
	void _ApplyLibraryChanged(BMessage* message);
	void _ApplyAudiobookData(BMessage* message);
	void _ApplySavedState(BMessage* message);
	void _ApplyChapters(BMessage* message);
	void _PlayChapter(BMessage* message);
	void _PlayChapterUri(const std::string& uri, const char* title,
		int32 startPositionMs);
	void _ResumeAudiobook();
	void _ShowChapterContextMenu(BMessage* message);
	void _ShowPlayableContextMenu(BMessage* message);
	void _RemoveChapterFromLibrary(BMessage* message);
	void _ToggleSaved();
	void _ApplyCapabilitiesChanged();

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
	BButton* fResume = nullptr;
	BButton* fSave = nullptr;
	BColumnListView* fChapterList = nullptr;
};
