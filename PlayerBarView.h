#pragma once

#include <Archivable.h>
#include <InterfaceDefs.h>
#include <Messenger.h>
#include <SupportDefs.h>
#include <View.h>
#include <string>

class BBitmap;
class BButton;
class BDragger;
class BMessageRunner;
class BSlider;
class BView;
class BStringView;
class PlaybackSeekBarView;
class TimeLabelView;
class TrackInfoView;
class VolumeIconView;

class PlayerBarView : public BView {
public:
    explicit        PlayerBarView();
    explicit        PlayerBarView(BMessage* archive);
                    ~PlayerBarView() override;

    static  BArchivable*    Instantiate(BMessage* data);
    virtual status_t        Archive(BMessage* data, bool deep = true) const override;

    void    SetTrack(const char* title, const char* artist);
    void    SetTrackUri(const char* trackUri);
    void    SetOpenUri(const char* uri);
    void    SetTrackIds(const char* albumId, const char* artistId);
    void    SetArtwork(BBitmap* bitmap);
    void    SetPlaying(bool playing);
    bool    IsPlaying() const { return fIsPlaying; }
    void    SetPosition(bigtime_t pos, bigtime_t duration);
    void    SetVolume(int32 volume);
    void    SetShuffle(bool on);
    void    SetRepeat(const char* mode);
    void    SetPlaybackOptionsEnabled(bool enabled);
    void    SetSeekBarColor(bool useSystemColor, rgb_color color);

    void    SetTarget(BMessenger target);

    virtual void    AttachedToWindow() override;
    virtual void    AllAttached() override;
    virtual void    DetachedFromWindow() override;
    virtual void    DoLayout() override;
    virtual void    Draw(BRect updateRect) override;
    virtual void    DrawAfterChildren(BRect updateRect) override;
    virtual void    FrameMoved(BPoint newPosition) override;
    virtual void    MouseDown(BPoint where) override;
    virtual void    MessageReceived(BMessage* msg) override;

private:
    void    _BuildUI();
    void    _ApplyTarget();
    void    _UpdatePlaybackPosition(bigtime_t pos, bigtime_t duration);
    void    _UpdateTimeLabels(bigtime_t pos, bigtime_t duration);
    void    _LoadButtonIcons();
    void    _UpdateVolumeIcon();
    void    _ShowContextMenu(BPoint screenWhere);
    void    _ForwardMessage(BMessage* message);
    bool    _HandleStateMessage(BMessage* message);
    bool    _HandleMenuMessage(BMessage* message);
    bool    _HandlePlaybackCommand(BMessage* message);
    void    _ApplyTick();
    void    _ApplyReplicantStateMessage(BMessage* message);
    void    _ApplySeekbarColorMessage(BMessage* message);
    void    _ShowReplicantMenu(BMessage* message);
    void    _ForwardAddTrackMenu();
    void    _ForwardAlbumOpen();
    void    _ForwardArtistOpen();
    void    _TogglePlayPause(BMessage* message);
    void    _ToggleShuffle(BMessage* message);
    void    _ToggleRepeat(BMessage* message);
    void    _ForwardTransportCommand(BMessage* message);
    bool    _RegisterReplicant();
    void    _UnregisterReplicant();
    bool    _UpdateReplicantAvailability();
    void    _SetHaifyUnavailable();
    void    _ApplyBackgroundColors();
    void    _ApplySeekBarColors();
    void    _LoadReplicantAppearance();
    bool    _ApplyReplicantAppearance(const BMessage* message);

    TrackInfoView*          fTrackInfoView  = nullptr;
    BButton*                fAddTrackButton = nullptr;
    BDragger*               fDragger        = nullptr;
    BButton*                fShuffleButton  = nullptr;
    BButton*                fPrevButton     = nullptr;
    BButton*                fPlayButton     = nullptr;
    BButton*                fNextButton     = nullptr;
    BButton*                fRepeatButton   = nullptr;
    TimeLabelView*          fPositionView   = nullptr;
    TimeLabelView*          fDurationView   = nullptr;
    VolumeIconView*         fVolumeLabel    = nullptr;
    PlaybackSeekBarView*    fSeekBar        = nullptr;
    BSlider*                fVolumeSlider   = nullptr;

    BBitmap*                fIcoPlay          = nullptr;
    BBitmap*                fIcoPause         = nullptr;
    BBitmap*                fIcoPrev          = nullptr;
    BBitmap*                fIcoNext          = nullptr;
    BBitmap*                fIcoShuffle       = nullptr;
    BBitmap*                fIcoShuffleActive = nullptr;
    BBitmap*                fIcoRepeat        = nullptr;
    BBitmap*                fIcoRepeatContext = nullptr;
    BBitmap*                fIcoRepeatTrack   = nullptr;
    BBitmap*                fIcoVolumeMuted   = nullptr;
    BBitmap*                fIcoVolume        = nullptr;

    BMessenger              fTarget;
    bool                    fIsPlaying      = false;
    bool                    fIsReplicant    = false;
    bool                    fRegistered     = false;
    bool                    fHaifyAvailable = true;
    bool                    fShuffleOn      = false;
    bool                    fPlaybackOptionsEnabled = true;
    bool                    fUseSystemSeekBarColor = false;
    rgb_color               fSeekBarColor = { 150, 150, 252, 255 };
    bool                    fUseAutomaticReplicantColor = true;
    rgb_color               fReplicantColor = { 255, 255, 255, 255 };
    rgb_color               fActiveReplicantColor = { 255, 255, 255, 255 };
    bool                    fReplicantColorRefreshPending = false;
    std::string             fRepeatState    = "off";
    std::string             fCurrentAlbumId;
    std::string             fCurrentArtistId;
    std::string             fCurrentTrackUri;
    std::string             fCurrentOpenUri;
    BMessageRunner*         fTickTimer      = nullptr;
    bigtime_t               fLastSyncUs     = 0;
    bigtime_t               fLastPosUs      = 0;
    bigtime_t               fDurationUs     = 0;
};
