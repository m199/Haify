#pragma once

#include <Window.h>

#include <string>

class ArtworkView;
class BButton;
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

    std::string fEpisodeId;
    std::string fEpisodeUri;
    std::string fShowName;
    std::string fShowUri;
    bool fSaved = false;

    ArtworkView* fArtwork = nullptr;
    BStringView* fName = nullptr;
    BStringView* fShow = nullptr;
    BTextView* fDescription = nullptr;
    BButton* fPlay = nullptr;
    BButton* fSave = nullptr;
    BButton* fQueue = nullptr;
    BButton* fOpenShow = nullptr;
};
