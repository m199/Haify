#pragma once

#include "DescriptionTextFormatter.h"

#include <TextView.h>

#include <string>
#include <vector>


class MediaDescriptionView : public BTextView {
public:
	explicit MediaDescriptionView(const char* name);

	virtual void AttachedToWindow() override;
	virtual void FrameResized(float width, float height) override;
	virtual void MouseDown(BPoint where) override;
	virtual void MouseUp(BPoint where) override;

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
