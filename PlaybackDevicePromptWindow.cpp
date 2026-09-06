#include "PlaybackDevicePromptWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <Window.h>

#include <string>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaybackDevicePromptWindow"

class PlaybackDevicePromptWindow : public BWindow {
public:
	PlaybackDevicePromptWindow(BMessenger target,
			const std::vector<PlaybackDeviceChoice>& devices,
			bool librespotRunning)
		:
		BWindow(BRect(0, 0, 390, 220),
			B_TRANSLATE("Choose playback device"), B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fTarget(target),
		fDevices(devices)
	{
		fList = new BListView("devices", B_SINGLE_SELECTION_LIST);
		for (const PlaybackDeviceChoice& device : fDevices) {
			std::string label = PlaybackDeviceDisplayName(device.id,
				device.name, device.type);
			if (device.active)
				label += B_TRANSLATE(" (active)");
			fList->AddItem(new BStringItem(label.c_str()));
		}
		if (!fDevices.empty())
			fList->Select(0);

		BScrollView* scroll = new BScrollView("deviceScroll", fList,
			0, false, true, B_FANCY_BORDER);
		scroll->SetExplicitMinSize(BSize(320, 92));

		BButton* local = new BButton("local",
			librespotRunning ? B_TRANSLATE("Use local librespot")
				: B_TRANSLATE("Start local librespot"),
			new BMessage(kMsgPlaybackDeviceStartLocal));
		BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));
		fUse = new BButton("use", B_TRANSLATE("Use Device"),
			new BMessage(kMsgPlaybackDeviceSelected));
		fUse->SetEnabled(!fDevices.empty());

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(new BStringView("prompt",
				B_TRANSLATE("No active Spotify device was found.")))
			.Add(scroll, 1.0f)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(local)
				.AddGlue()
				.Add(cancel)
				.Add(fUse)
			.End()
		.End();
		SetDefaultButton(fUse);
	}

	bool QuitRequested() override
	{
		BMessage closed(kMsgPlaybackDevicePromptClosed);
		closed.AddBool("cancelled", !fCommitted);
		fTarget.SendMessage(&closed);
		return true;
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kMsgPlaybackDeviceSelected:
			{
				int32 selection = fList ? fList->CurrentSelection() : -1;
				if (selection >= 0 && selection < (int32)fDevices.size()) {
					BMessage selected(kMsgPlaybackDeviceSelected);
					selected.AddString("device_id",
						fDevices[selection].id.c_str());
					fCommitted = true;
					fTarget.SendMessage(&selected);
					PostMessage(B_QUIT_REQUESTED);
				}
				return;
			}
			case kMsgPlaybackDeviceStartLocal:
				fCommitted = true;
				fTarget.SendMessage(kMsgPlaybackDeviceStartLocal);
				PostMessage(B_QUIT_REQUESTED);
				return;
			default:
				BWindow::MessageReceived(message);
				return;
		}
	}

private:
	BMessenger fTarget;
	std::vector<PlaybackDeviceChoice> fDevices;
	BListView* fList = nullptr;
	BButton* fUse = nullptr;
	bool fCommitted = false;
};


void
ShowPlaybackDevicePrompt(BMessenger target,
	const std::vector<PlaybackDeviceChoice>& devices, bool librespotRunning,
	BRect ownerFrame)
{
	PlaybackDevicePromptWindow* prompt = new PlaybackDevicePromptWindow(
		target, devices, librespotRunning);
	BRect promptFrame = prompt->Frame();
	prompt->MoveTo(ownerFrame.left
		+ (ownerFrame.Width() - promptFrame.Width()) / 2.0f,
		ownerFrame.top + 40.0f);
	prompt->Show();
}
