#pragma once

#include <Archivable.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <Rect.h>
#include <SupportDefs.h>
#include <View.h>

class PlaybackSeekBarView : public BView {
public:
    explicit        PlaybackSeekBarView(const char* name);
    explicit        PlaybackSeekBarView(BMessage* archive);

    static  BArchivable*    Instantiate(BMessage* data);
    virtual status_t        Archive(BMessage* data, bool deep = true) const override;

    void            SetDuration(bigtime_t duration);
    void            SetPosition(bigtime_t pos);
    void            SetColors(rgb_color bg, rgb_color fill);
    void            SetTransparentBackground(bool transparent);
    void            SetTarget(BMessenger target);
    void            SetLayoutScale(float scale);

    bigtime_t       Duration() const { return fDuration; }
    bigtime_t       Position() const { return fPosition; }

    virtual void    Draw(BRect updateRect) override;
    virtual void    MouseDown(BPoint where) override;
    virtual void    MouseUp(BPoint where) override;
    virtual void    MouseMoved(BPoint where, uint32 transit,
                               const BMessage* dragMessage) override;
    virtual void    AttachedToWindow() override;
    virtual void    MessageReceived(BMessage* msg) override;

private:
    void            _SeekFromPoint(BPoint where);
    void            _DrawBar(const BRect& r);
    BRect           _TrackRect() const;
    void            _InitColors();

    BMessenger      fTarget;

    bigtime_t       fDuration   = 0;
    bigtime_t       fPosition   = 0;
    bool            fTracking   = false;
    bool            fTransparentBackground = false;
    float           fLayoutScale = 1.0f;

    rgb_color       fBg;
    rgb_color       fFill;
};
