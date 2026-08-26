#include "DescriptionTextFormatter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <Font.h>
#include <InterfaceDefs.h>
#include <string>
#include <TextView.h>
#include <vector>


namespace {

struct StyleState {
	bool bold = false;
	bool italic = false;
	bool underline = false;
	bool link = false;

	bool operator==(const StyleState& other) const
	{
		return bold == other.bold && italic == other.italic
			&& underline == other.underline && link == other.link;
	}
};


struct StyledRun {
	int32 offset;
	StyleState style;
};

struct OpenLink {
	int32 start;
	std::string url;
};


struct StyledText {
	std::string text;
	std::vector<StyledRun> runs;
	std::vector<MediaDescriptionLink> links;
};

bool
IsSpace(char character)
{
	return character == ' ' || character == '\t' || character == '\n'
		|| character == '\r' || character == '\f' || character == '\v';
}


std::string
LowercaseAscii(const std::string& text)
{
	std::string result;
	result.reserve(text.size());
	for (char character : text)
		result += (char)tolower((unsigned char)character);
	return result;
}


std::string
TrimAscii(const std::string& text)
{
	size_t start = 0;
	while (start < text.size() && IsSpace(text[start]))
		start++;
	size_t end = text.size();
	while (end > start && IsSpace(text[end - 1]))
		end--;
	return text.substr(start, end - start);
}


bool
StartsWith(const std::string& text, const char* prefix)
{
	size_t index = 0;
	while (prefix[index] != '\0') {
		if (index >= text.size() || text[index] != prefix[index])
			return false;
		index++;
	}
	return true;
}


std::string
TagName(const std::string& tag, bool& closing)
{
	std::string lower = LowercaseAscii(TrimAscii(tag));
	closing = !lower.empty() && lower[0] == '/';
	if (closing)
		lower = TrimAscii(lower.substr(1));
	size_t space = lower.find(' ');
	if (space != std::string::npos)
		lower = lower.substr(0, space);
	if (!lower.empty() && lower.back() == '/')
		lower.resize(lower.size() - 1);
	return lower;
}


std::string
TagAttribute(const std::string& tag, const char* attribute)
{
	std::string lower = LowercaseAscii(tag);
	std::string needle = std::string(attribute) + "=";
	size_t pos = lower.find(needle);
	if (pos == std::string::npos)
		return "";
	pos += needle.size();
	while (pos < tag.size() && IsSpace(tag[pos]))
		pos++;
	if (pos >= tag.size())
		return "";

	char quote = tag[pos];
	if (quote == '"' || quote == '\'') {
		size_t end = tag.find(quote, pos + 1);
		if (end == std::string::npos)
			return "";
		return tag.substr(pos + 1, end - pos - 1);
	}

	size_t end = pos;
	while (end < tag.size() && !IsSpace(tag[end]) && tag[end] != '>')
		end++;
	return tag.substr(pos, end - pos);
}


void
AppendUtf8(std::string& output, unsigned long codepoint)
{
	if (codepoint <= 0x7f) {
		output += (char)codepoint;
		return;
	}
	if (codepoint <= 0x7ff) {
		output += (char)(0xc0 | ((codepoint >> 6) & 0x1f));
		output += (char)(0x80 | (codepoint & 0x3f));
		return;
	}
	if (codepoint <= 0xffff) {
		output += (char)(0xe0 | ((codepoint >> 12) & 0x0f));
		output += (char)(0x80 | ((codepoint >> 6) & 0x3f));
		output += (char)(0x80 | (codepoint & 0x3f));
		return;
	}
	if (codepoint <= 0x10ffff) {
		output += (char)(0xf0 | ((codepoint >> 18) & 0x07));
		output += (char)(0x80 | ((codepoint >> 12) & 0x3f));
		output += (char)(0x80 | ((codepoint >> 6) & 0x3f));
		output += (char)(0x80 | (codepoint & 0x3f));
	}
}


void
AppendRunIfNeeded(StyledText& output, const StyleState& style)
{
	int32 offset = (int32)output.text.size();
	if (!output.runs.empty() && output.runs.back().offset == offset) {
		output.runs.back().style = style;
		return;
	}
	if (output.runs.empty() || !(output.runs.back().style == style))
		output.runs.push_back({offset, style});
}


StyleState
StyleAt(const StyledText& output, int32 offset)
{
	StyleState style;
	for (const StyledRun& run : output.runs) {
		if (run.offset > offset)
			break;
		style = run.style;
	}
	return style;
}


void
AddStyleBoundary(StyledText& output, int32 offset, const StyleState& style)
{
	output.runs.push_back({offset, style});
}


bool
OverlapsExistingLink(const StyledText& output, int32 start, int32 end)
{
	for (const MediaDescriptionLink& link : output.links) {
		if (start < link.end && end > link.start)
			return true;
	}
	return false;
}


bool
IsUrlTail(char character)
{
	return character == '.' || character == ',' || character == ';'
		|| character == ':' || character == '!' || character == '?'
		|| character == ')' || character == ']';
}


void
AddPlainUrlLinks(StyledText& output)
{
	for (size_t index = 0; index < output.text.size(); index++) {
		bool hasScheme = output.text.compare(index, 7, "http://") == 0
			|| output.text.compare(index, 8, "https://") == 0;
		bool hasWww = output.text.compare(index, 4, "www.") == 0;
		if (!hasScheme && !hasWww)
			continue;

		size_t end = index;
		while (end < output.text.size() && !IsSpace(output.text[end]))
			end++;
		while (end > index && IsUrlTail(output.text[end - 1]))
			end--;
		if (end <= index)
			continue;

		int32 startOffset = (int32)index;
		int32 endOffset = (int32)end;
		if (OverlapsExistingLink(output, startOffset, endOffset))
			continue;

		std::string url = output.text.substr(index, end - index);
		if (hasWww)
			url = "https://" + url;
		output.links.push_back({startOffset, endOffset, url});
		index = end - 1;
	}
}


void
ApplyLinkStyles(StyledText& output)
{
	AddPlainUrlLinks(output);
	for (const MediaDescriptionLink& link : output.links) {
		StyleState normalStyle = StyleAt(output, link.start);
		StyleState afterStyle = StyleAt(output, link.end);
		StyleState linkStyle = normalStyle;
		linkStyle.underline = true;
		linkStyle.link = true;
		AddStyleBoundary(output, link.start, linkStyle);
		AddStyleBoundary(output, link.end, afterStyle);
	}
	std::stable_sort(output.runs.begin(), output.runs.end(),
		[](const StyledRun& left, const StyledRun& right) {
			return left.offset < right.offset;
		});
	std::vector<StyledRun> normalized;
	for (const StyledRun& run : output.runs) {
		if (!normalized.empty() && normalized.back().offset == run.offset) {
			normalized.back().style = run.style;
			continue;
		}
		if (normalized.empty() || !(normalized.back().style == run.style))
			normalized.push_back(run);
	}
	output.runs = normalized;
}


void
AppendNewline(StyledText& output, const StyleState& style,
	int desiredCount = 1)
{
	while (!output.text.empty() && output.text.back() == ' ')
		output.text.pop_back();

	int currentCount = 0;
	for (size_t index = output.text.size(); index > 0
			&& output.text[index - 1] == '\n'; index--) {
		currentCount++;
	}
	while (currentCount < desiredCount && currentCount < 2) {
		AppendRunIfNeeded(output, style);
		output.text += '\n';
		currentCount++;
	}
}


void
AppendTextChar(StyledText& output, const StyleState& style, char character)
{
	if (character == '\r')
		return;
	if (character == '\n') {
		AppendNewline(output, style);
		return;
	}
	if (character == '\t' || character == '\f' || character == '\v')
		character = ' ';
	if (character == ' ' && (output.text.empty() || output.text.back() == ' '
			|| output.text.back() == '\n')) {
		return;
	}
	AppendRunIfNeeded(output, style);
	output.text += character;
}


bool
AppendDecodedEntity(const std::string& entity, StyledText& output,
	const StyleState& style)
{
	std::string lower = LowercaseAscii(entity);
	AppendRunIfNeeded(output, style);
	if (lower == "amp") output.text += '&';
	else if (lower == "lt") output.text += '<';
	else if (lower == "gt") output.text += '>';
	else if (lower == "quot") output.text += '"';
	else if (lower == "apos") output.text += '\'';
	else if (lower == "nbsp") AppendTextChar(output, style, ' ');
	else if (lower == "ndash" || lower == "mdash") output.text += '-';
	else if (lower == "hellip") output.text += "...";
	else if (StartsWith(lower, "#x")) {
		char* end = nullptr;
		unsigned long codepoint = strtoul(lower.c_str() + 2, &end, 16);
		if (!end || *end != '\0')
			return false;
		AppendUtf8(output.text, codepoint);
	} else if (StartsWith(lower, "#")) {
		char* end = nullptr;
		unsigned long codepoint = strtoul(lower.c_str() + 1, &end, 10);
		if (!end || *end != '\0')
			return false;
		AppendUtf8(output.text, codepoint);
	} else
		return false;
	return true;
}


void
ApplyTag(const std::string& tag, StyledText& output, StyleState& style,
	std::vector<OpenLink>& openLinks)
{
	bool closing = false;
	std::string lower = TagName(tag, closing);

	if (lower == "b" || lower == "strong")
		style.bold = !closing;
	else if (lower == "i" || lower == "em")
		style.italic = !closing;
	else if (lower == "u")
		style.underline = !closing;
	else if (lower == "a") {
		style.underline = !closing;
		style.link = !closing;
		if (!closing) {
			std::string href = TagAttribute(tag, "href");
			if (!href.empty())
				openLinks.push_back({(int32)output.text.size(), href});
		} else if (!openLinks.empty()) {
			OpenLink link = openLinks.back();
			openLinks.pop_back();
			int32 end = (int32)output.text.size();
			if (end > link.start)
				output.links.push_back({link.start, end, link.url});
		}
	}
	else if (lower.size() == 2 && lower[0] == 'h' && lower[1] >= '1'
			&& lower[1] <= '6') {
		style.bold = !closing;
		AppendNewline(output, style, 2);
	} else if (lower == "br")
		AppendNewline(output, style);
	else if (lower == "p" || lower == "div")
		AppendNewline(output, style, 2);
	else if (lower == "li") {
		AppendNewline(output, style);
		if (!closing)
			output.text += "- ";
	}
}


std::string
TrimFormattedText(const std::string& text)
{
	size_t start = 0;
	while (start < text.size() && IsSpace(text[start]))
		start++;
	size_t end = text.size();
	while (end > start && IsSpace(text[end - 1]))
		end--;
	return text.substr(start, end - start);
}


StyledText
ParseMediaDescription(const std::string& description)
{
	StyledText output;
	output.text.reserve(description.size());
	StyleState style;
	std::vector<OpenLink> openLinks;
	output.runs.push_back({0, style});

	for (size_t index = 0; index < description.size(); index++) {
		char character = description[index];
		if (character == '<') {
			size_t close = description.find('>', index + 1);
			if (close != std::string::npos) {
				ApplyTag(description.substr(index + 1, close - index - 1),
					output, style, openLinks);
				index = close;
				continue;
			}
		}
		if (character == '&') {
			size_t close = description.find(';', index + 1);
			if (close != std::string::npos && close - index <= 16) {
				std::string entity = description.substr(index + 1,
					close - index - 1);
				if (AppendDecodedEntity(entity, output, style)) {
					index = close;
					continue;
				}
			}
		}
		AppendTextChar(output, style, character);
	}

	size_t trimmedSize = TrimFormattedText(output.text).size();
	size_t leading = 0;
	while (leading < output.text.size() && IsSpace(output.text[leading]))
		leading++;
	output.text = output.text.substr(leading, trimmedSize);
	std::vector<StyledRun> trimmedRuns;
	for (StyledRun& run : output.runs) {
		run.offset = std::max<int32>(0, run.offset - (int32)leading);
		if (run.offset <= (int32)output.text.size()
				&& (trimmedRuns.empty()
					|| trimmedRuns.back().offset != run.offset
					|| !(trimmedRuns.back().style == run.style))) {
			trimmedRuns.push_back(run);
		}
	}
	if (trimmedRuns.empty() || trimmedRuns.front().offset != 0)
		trimmedRuns.insert(trimmedRuns.begin(), {0, StyleState()});
	output.runs = trimmedRuns;
	for (MediaDescriptionLink& link : output.links) {
		link.start = std::max<int32>(0, link.start - (int32)leading);
		link.end = std::max<int32>(0, link.end - (int32)leading);
	}
	ApplyLinkStyles(output);
	return output;
}

}


std::string
FormatMediaDescription(const std::string& description)
{
	return ParseMediaDescription(description).text;
}


std::vector<MediaDescriptionLink>
MediaDescriptionLinks(const std::string& description)
{
	return ParseMediaDescription(description).links;
}


void
ApplyParsedDescription(BTextView* view, StyledText styled)
{
	if (!view)
		return;

	if (styled.runs.empty())
		styled.runs.push_back({0, StyleState()});

	size_t bytes = sizeof(text_run_array)
		+ (styled.runs.size() - 1) * sizeof(text_run);
	text_run_array* runs = (text_run_array*)malloc(bytes);
	if (!runs) {
		view->SetText(styled.text.c_str());
		return;
	}

	runs->count = (int32)styled.runs.size();
	rgb_color color = ui_color(B_PANEL_TEXT_COLOR);
	for (size_t index = 0; index < styled.runs.size(); index++) {
		uint16 face = 0;
		if (styled.runs[index].style.bold)
			face |= B_BOLD_FACE;
		if (styled.runs[index].style.italic)
			face |= B_ITALIC_FACE;
		if (styled.runs[index].style.underline)
			face |= B_UNDERSCORE_FACE;
		if (face == 0)
			face = B_REGULAR_FACE;

		BFont font(be_plain_font);
		font.SetFace(face);
		runs->runs[index].offset = styled.runs[index].offset;
		runs->runs[index].font = font;
		runs->runs[index].color = styled.runs[index].style.link
			? (rgb_color){0, 72, 176, 255} : color;
	}

	view->SetStylable(true);
	view->SetText(styled.text.c_str(), runs);
	free(runs);
}


void
ApplyMediaDescription(BTextView* view, const std::string& description)
{
	ApplyParsedDescription(view, ParseMediaDescription(description));
}
