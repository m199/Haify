#pragma once

#include <string>
#include <vector>
#include <cstdint>

class BTextView;

struct MediaDescriptionLink {
	// Byte offsets in formatted UTF-8 text; URL has HTML entities decoded once.
	int32_t start = 0;
	int32_t end = 0;
	std::string url;
};

std::string FormatMediaDescription(const std::string& description);
std::vector<MediaDescriptionLink> MediaDescriptionLinks(
	const std::string& description);
// Classifies the URI scheme, not an address appearing in a web URL or label.
bool MediaDescriptionLinkIsEmail(const std::string& url);
void ApplyMediaDescription(BTextView* view, const std::string& description);
