#ifndef DESKBAR_REPLICANT_VIEW_H
#define DESKBAR_REPLICANT_VIEW_H

#include <Archivable.h>
#include <Messenger.h>
#include <View.h>

class BBitmap;

class DeskbarReplicantView : public BView {
public:
	static const char*		ItemName();

							DeskbarReplicantView();
							DeskbarReplicantView(BMessage* archive);
							~DeskbarReplicantView() override;

	static	BArchivable*	Instantiate(BMessage* archive);
	virtual	status_t		Archive(BMessage* archive,
								bool deep = true) const override;

	virtual	void			AttachedToWindow() override;
	virtual	void			Draw(BRect updateRect) override;
	virtual	void			MouseDown(BPoint where) override;
	virtual	void			MessageReceived(BMessage* message) override;
	virtual	BSize			MinSize() override;
	virtual	BSize			MaxSize() override;
	virtual	BSize			PreferredSize() override;

private:
	void					_Init();
	void					_LoadAppIcon();
	void					_ShowMenu(BPoint where);
	void					_DispatchToHaify(BMessage* message);

	BBitmap*				fIcon = nullptr;
};

#endif
