#pragma once

#include <Window.h>

class ArtworkReplicantView;

class ArtworkWindow : public BWindow {
public:
                            ArtworkWindow();
    virtual                 ~ArtworkWindow() override;

    virtual bool            QuitRequested() override;
    virtual void            FrameResized(float width, float height) override;
    void                    SaveOpenState(bool open);

private:
    void                    _SaveFrame(bool open);
    void                    _EnforceSquareSize(float width, float height);
    void                    _EnsureOnScreen();

    ArtworkReplicantView*   fArtworkView = nullptr;
    bool                    fAdjustingSize = false;
    float                   fLastWidth = 0.0f;
    float                   fLastHeight = 0.0f;
};
