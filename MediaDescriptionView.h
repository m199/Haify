#pragma once

#include "DescriptionTextFormatter.h"

#include <TextView.h>

#include <vector>


class MediaDescriptionView : public BTextView {
public:
	explicit MediaDescriptionView(const char* name);

	virtual void AttachedToWindow() override;
	virtual void FrameResized(float width, float height) override;
	virtual void MouseDown(BPoint where) override;
	virtual void MouseMoved(BPoint where, uint32 transit,
		const BMessage* message) override;

	void SetLinks(const std::vector<MediaDescriptionLink>& links);
	void Reflow();

private:
	void _UpdateTextRect();
	const MediaDescriptionLink* _LinkAt(BPoint where) const;

	std::vector<MediaDescriptionLink> fLinks;
	bool fOverLink = false;
};
