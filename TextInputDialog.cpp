#include "TextInputDialog.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TextInputDialog"

static const uint32 kMsgOK     = 'tiOK';
static const uint32 kMsgCancel = 'tiCn';


TextInputDialog::TextInputDialog(const char* title,
                                 const char* label,
                                 const char* initialText,
                                 BMessenger   target,
                                 BMessage     confirmMsg)
    : BWindow(BRect(0, 0, 320, 80), title,
              B_MODAL_WINDOW,
              B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
      fTarget(target),
      fConfirmMsg(confirmMsg)
{
    fInput = new BTextControl("input", label, initialText ? initialText : "",
                              nullptr);
    fInput->SetModificationMessage(nullptr);

    BButton* ok     = new BButton("ok",     B_TRANSLATE("OK"),     new BMessage(kMsgOK));
    BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"), new BMessage(kMsgCancel));
    SetDefaultButton(ok);

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_DEFAULT_SPACING)
        .Add(fInput)
        .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
            .AddGlue()
            .Add(cancel)
            .Add(ok)
        .End()
    .End();

    CenterOnScreen();


    fInput->MakeFocus(true);
    fInput->TextView()->SelectAll();
}


void
TextInputDialog::MessageReceived(BMessage* msg)
{
    switch (msg->what) {
        case kMsgOK:
        {
            const char* text = fInput->Text();
            if (text && *text) {
                BMessage reply(fConfirmMsg);
                reply.AddString("name", text);
                fTarget.SendMessage(&reply);
            }
            Quit();
            break;
        }
        case kMsgCancel:
            Quit();
            break;
        default:
            BWindow::MessageReceived(msg);
            break;
    }
}
