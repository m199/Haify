#pragma once

#include "ArtworkView.h"

#include <Messenger.h>
#include <String.h>

class BDragger;
class BMessageRunner;

class ArtworkReplicantView : public ArtworkView {
public:
                            ArtworkReplicantView();
                            ArtworkReplicantView(BMessage* archive);
    virtual                 ~ArtworkReplicantView() override;

    static  BArchivable*    Instantiate(BMessage* archive);
    virtual status_t        Archive(BMessage* archive,
                                bool deep = true) const override;

    void                    SetRegisterForUpdates(bool enabled);
    void                    SetArtworkUrl(const char* url);

    virtual void            AttachedToWindow() override;
    virtual void            DetachedFromWindow() override;
    virtual void            Draw(BRect updateRect) override;
    virtual void            DrawAfterChildren(BRect updateRect) override;
    virtual void            FrameResized(float width, float height) override;
    virtual void            MouseDown(BPoint where) override;
    virtual void            MessageReceived(BMessage* message) override;
    virtual BSize           MinSize() override;
    virtual BSize           MaxSize() override;
    virtual BSize           PreferredSize() override;

private:
    void                    _Init(bool useDefaultSize);
    void                    _Register();
    void                    _Unregister();
    void                    _LayoutDragger();
    void                    _KeepInsideParent();
    void                    _ApplyBackground();
    void                    _LoadAppearanceSettings();
    bool                    _ApplyAppearance(const BMessage* message);
    rgb_color               _AppearanceColor();
    void                    _ShowContextMenu(BPoint screenWhere);
    void                    _RequestArtwork(bool reload);
    void                    _ForwardMessage(BMessage* message);
    void                    _ApplyReplicantStateMessage(BMessage* message);
    void                    _RetryRegister();
    virtual void            ArtworkStateChanged() override;

    BDragger*               fDragger = nullptr;
    BMessageRunner*         fRegisterTimer = nullptr;
    BMessenger              fTarget;
    BString                 fArtworkUrl;
    BString                 fTitle;
    BString                 fArtist;
    BString                 fOpenUri;
    bool                    fIsReplicant = false;
    bool                    fRegisterForUpdates = false;
    bool                    fRegistered = false;
    bool                    fUseAutomaticColor = true;
    rgb_color               fCustomColor = { 255, 255, 255, 255 };
};
