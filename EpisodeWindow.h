#pragma once

#include <Window.h>

#include <string>

class ArtworkView;
class ClickableLabelView;
class BMenuBar;
class BMenuItem;
class BStringView;
class BTextView;

class EpisodeWindow : public BWindow {
public:
    explicit EpisodeWindow(const std::string& episodeId);

    const std::string& GetEpisodeId() const { return fEpisodeId; }
    virtual void MessageReceived(BMessage* message);

private:
    void _Load();
    void _LoadArtwork(const std::string& url);
    void _UpdateSaved(bool saved);
    void _ApplyLibraryChanged(BMessage* message);
    void _ApplyEpisodeData(BMessage* message);
    void _PlayEpisode();
    void _OpenShow();
    void _QueueEpisode();
    void _ToggleSaved();

    std::string fEpisodeId;
    std::string fEpisodeUri;
    std::string fShowName;
    std::string fShowUri;
    bool fSaved = false;

    ArtworkView* fArtwork = nullptr;
    BMenuBar* fMenuBar = nullptr;
    BMenuItem* fPlayMenuItem = nullptr;
    BMenuItem* fSaveMenuItem = nullptr;
    BMenuItem* fQueueMenuItem = nullptr;
    BMenuItem* fOpenShowMenuItem = nullptr;
    BStringView* fName = nullptr;
    ClickableLabelView* fShow = nullptr;
    BTextView* fDescription = nullptr;
};
