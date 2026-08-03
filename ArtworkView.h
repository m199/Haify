#pragma once

#include <Archivable.h>
#include <SupportDefs.h>
#include <View.h>

#include <string>

class BBitmap;

class ArtworkView : public BView {
public:
    enum LoadState {
        kNoCover,
        kLoading,
        kLoaded,
        kLoadFailed
    };

    explicit        ArtworkView(const char* name);
    explicit        ArtworkView(BMessage* archive);
    virtual         ~ArtworkView() override;

    static  BArchivable*    Instantiate(BMessage* data);
    virtual status_t        Archive(BMessage* data, bool deep = true) const override;

    void            SetBitmap(BBitmap* bmp);
    void            AdoptBitmap(BBitmap* bmp);
    void            ShowLoading();
    void            LoadUrl(const std::string& url);
    void            ReloadUrl();
    bool            HasBitmap() const;
    LoadState       State() const;
    const std::string& ArtworkUrl() const;

    virtual void    AttachedToWindow() override;
    virtual void    Draw(BRect update) override;
    virtual void    MessageReceived(BMessage* msg) override;
    virtual void    GetPreferredSize(float* w, float* h) override;
    virtual void    FrameResized(float width, float height) override;

    virtual bool    HasHeightForWidth() override;
    virtual void    GetHeightForWidth(float width, float* min, float* max,
                                      float* pref) override;

protected:
    virtual void    ArtworkStateChanged();

private:
    BRect           _CoverFrame() const;
    void            _InvalidateScaledBitmap();
    void            _UpdateScaledBitmap();
    void            _StartLoad(bool reload);
    void            _ClearBitmap();

    BBitmap*        fBitmap         = nullptr;
    BBitmap*        fScaledBitmap   = nullptr;
    BRect           fScaledFrame;
    std::string     fArtworkUrl;
    uint64          fLoadGeneration = 0;
    LoadState       fLoadState      = kNoCover;
    bool            fLoadPending    = false;
    bool            fReloadPending  = false;
};
