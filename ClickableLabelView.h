#pragma once

#include <View.h>
#include <string>

class ClickableLabelView : public BView {
public:
                ClickableLabelView(const char* name, uint32 msgWhat);
                ~ClickableLabelView() override = default;

    void        SetText(const char* text);
    const char* Text() const { return fText.c_str(); }
    void        UpdateColors();

    void        Draw(BRect updateRect) override;
    void        MouseDown(BPoint where) override;
    void        MouseMoved(BPoint where, uint32 transit,
                           const BMessage* drag) override;
    void        AttachedToWindow() override;
    BSize       MinSize() override;
    BSize       PreferredSize() override;
    BSize       MaxSize() override;

private:
    std::string fText;
    uint32      fMsgWhat;
};
