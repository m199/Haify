#pragma once

#include <Message.h>
#include <Messenger.h>
#include <Window.h>

class BTextControl;



class TextInputDialog : public BWindow {
public:
                    TextInputDialog(const char* title,
                                    const char* label,
                                    const char* initialText,
                                    BMessenger   target,
                                    BMessage     confirmMsg);

    virtual void    MessageReceived(BMessage* msg) override;

private:
    BTextControl*   fInput;
    BMessenger      fTarget;
    BMessage        fConfirmMsg;
};
