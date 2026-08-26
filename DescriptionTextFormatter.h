#pragma once

#include <string>
#include <vector>
#include <cstdint>

class BTextView;

struct MediaDescriptionLink {
	int32_t start;
	int32_t end;
	std::string url;
};

std::string FormatMediaDescription(const std::string& description);
std::vector<MediaDescriptionLink> MediaDescriptionLinks(
	const std::string& description);
void ApplyMediaDescription(BTextView* view, const std::string& description);
