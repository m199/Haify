#pragma once

#include "ui/DescriptionTextFormatter.h"

#include <TextView.h>

#include <string>
#include <vector>


class MediaDescriptionView : public BTextView {
public:
	explicit MediaDescriptionView(const char* name);

	virtual void AttachedToWindow() override;
	virtual void FrameResized(float width, float height) override;
	virtual void MessageReceived(BMessage* message) override;
	virtual void MouseDown(BPoint where) override;
	virtual void MouseUp(BPoint where) override;
	virtual void MouseMoved(BPoint where, uint32 transit,
		const BMessage* dragMessage) override;

	void SetLinks(const std::vector<MediaDescriptionLink>& links);
	void Reflow();

private:
	void _ResetToTop();
	void _UpdateTextRect();
	const MediaDescriptionLink* _LinkAt(BPoint where) const;

	std::vector<MediaDescriptionLink> fLinks;
	std::string fPendingLinkUrl;
	BPoint fPendingLinkPoint;
	bool fPendingLink = false;
	bool fResetOnNextResize = false;
};
