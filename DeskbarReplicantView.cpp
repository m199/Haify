#include "DeskbarReplicantView.h"
#include "Config.h"

#include "Messages.h"

#include <Application.h>
#include <Archivable.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <InterfaceDefs.h>
#include <MenuItem.h>
#include <Node.h>
#include <NodeInfo.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Window.h>
#include <cstring>
#include <image.h>
#include <math.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DeskbarReplicantView"

static const char* kDeskbarItemName = "HaifyDeskbarReplicant";
static const char* kHaifySignature = HAIFY_MIME_SIG;
static const float kIconSize = 16.0f;


const char*
DeskbarReplicantView::ItemName()
{
	return kDeskbarItemName;
}


DeskbarReplicantView::DeskbarReplicantView()
	:
	BView(BRect(0, 0, kIconSize - 1, kIconSize - 1), kDeskbarItemName,
		B_FOLLOW_NONE, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init();
}


DeskbarReplicantView::DeskbarReplicantView(BMessage* archive)
	:
	BView(archive)
{
	_Init();
}


void
DeskbarReplicantView::_Init()
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(B_TRANSPARENT_COLOR);
	SetExplicitMinSize(BSize(kIconSize, kIconSize));
	SetExplicitPreferredSize(BSize(kIconSize, kIconSize));
	SetExplicitMaxSize(BSize(kIconSize, kIconSize));
	ResizeTo(kIconSize - 1, kIconSize - 1);
	_LoadAppIcon();
}


DeskbarReplicantView::~DeskbarReplicantView()
{
	delete fIcon;
}


void
DeskbarReplicantView::_LoadAppIcon()
{
	delete fIcon;
	fIcon = nullptr;

	BBitmap* icon = new BBitmap(BRect(0, 0, kIconSize - 1, kIconSize - 1),
		B_RGBA32);
	status_t status = B_ERROR;

	if (be_app) {
		app_info appInfo;
		if (be_app->GetAppInfo(&appInfo) == B_OK
				&& strcmp(appInfo.signature, kHaifySignature) == 0) {
			BNode node(&appInfo.ref);
			BNodeInfo nodeInfo(&node);
			status = nodeInfo.GetTrackerIcon(icon, B_MINI_ICON);
		}
	}

	if (status != B_OK) {
		image_info imageInfo;
		int32 cookie = 0;
		while (get_next_image_info(B_CURRENT_TEAM, &cookie, &imageInfo)
				== B_OK) {
			if (strstr(imageInfo.name, "Haify") == nullptr)
				continue;

			BNode node(imageInfo.name);
			BNodeInfo nodeInfo(&node);
			status = nodeInfo.GetTrackerIcon(icon, B_MINI_ICON);
			if (status == B_OK)
				break;
		}
	}

	if (status == B_OK)
		fIcon = icon;
	else
		delete icon;
}


BArchivable*
DeskbarReplicantView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "DeskbarReplicantView"))
		return nullptr;
	return new DeskbarReplicantView(archive);
}


status_t
DeskbarReplicantView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status != B_OK)
		return status;

	archive->AddString("add_on", kHaifySignature);
	return B_OK;
}


void
DeskbarReplicantView::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(B_TRANSPARENT_COLOR);
}


void
DeskbarReplicantView::Draw(BRect)
{
	BRect bounds = Bounds();
	if (fIcon) {
		BRect iconBounds = fIcon->Bounds();
		BPoint where(
			floorf(bounds.left + (bounds.Width() - iconBounds.Width()) / 2.0f),
			floorf(bounds.top + (bounds.Height() - iconBounds.Height()) / 2.0f));
		SetDrawingMode(B_OP_ALPHA);
		DrawBitmap(fIcon, where);
		SetDrawingMode(B_OP_COPY);
		return;
	}
}


void
DeskbarReplicantView::MouseDown(BPoint where)
{
	_ShowMenu(where);
}


void
DeskbarReplicantView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_SHOW_PLAYER_WINDOW:
		case MSG_OPEN_ARTWORK:
		case MSG_OPEN_SEARCH:
		case MSG_OPEN_BROWSER:
		case MSG_OPEN_QUEUE:
		case MSG_OPEN_SETTINGS:
		case MSG_QUIT_APP:
			_DispatchToHaify(message);
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
DeskbarReplicantView::_DispatchToHaify(BMessage* message)
{
	BMessenger target(kHaifySignature);
	if (target.IsValid() && target.SendMessage(message) == B_OK)
		return;

	if (message->what != MSG_QUIT_APP && be_roster)
		be_roster->Launch(kHaifySignature, message);
}


void
DeskbarReplicantView::_ShowMenu(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("Haify Deskbar", false, false);
	menu->SetAsyncAutoDestruct(true);
	bool haifyRunning = be_roster && be_roster->IsRunning(kHaifySignature);

	menu->AddItem(new BMenuItem(B_TRANSLATE("Player"),
		new BMessage(MSG_SHOW_PLAYER_WINDOW)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Artwork"),
		new BMessage(MSG_OPEN_ARTWORK)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Search"),
		new BMessage(MSG_OPEN_SEARCH)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Discover"),
		new BMessage(MSG_OPEN_BROWSER)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Queue"),
		new BMessage(MSG_OPEN_QUEUE)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Settings"),
		new BMessage(MSG_OPEN_SETTINGS)));
	menu->AddItem(new BMenuItem(
		haifyRunning ? B_TRANSLATE("Quit Haify") : B_TRANSLATE("Start Haify"),
		new BMessage(haifyRunning ? MSG_QUIT_APP : MSG_SHOW_PLAYER_WINDOW)));

	menu->SetTargetForItems(BMessenger(this));
	ConvertToScreen(&where);
	menu->Go(where, true, false, true);
}


BSize
DeskbarReplicantView::MinSize()
{
	return BSize(kIconSize, kIconSize);
}


BSize
DeskbarReplicantView::MaxSize()
{
	return BSize(kIconSize, kIconSize);
}


BSize
DeskbarReplicantView::PreferredSize()
{
	return BSize(kIconSize, kIconSize);
}
