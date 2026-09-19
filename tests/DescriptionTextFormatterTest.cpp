#include "ui/DescriptionTextFormatter.h"

#include <cassert>
#include <cstdio>
#include <string>

#ifdef NDEBUG
#error Description formatter tests require assertions; compile without NDEBUG.
#endif

static void
CheckLink(const std::string& description, const std::string& text,
	const std::string& label, const std::string& url)
{
	assert(FormatMediaDescription(description) == text);
	auto links = MediaDescriptionLinks(description);
	assert(links.size() == 1);
	const auto& link = links.front();
	assert(link.start >= 0 && link.end > link.start);
	assert(static_cast<size_t>(link.end) <= text.size());
	assert(text.substr(link.start, link.end - link.start) == label);
	assert(link.url == url);
}

static void
TestEncodedEmailAddress()
{
	for (const char* at : {"&#64;", "&#x40;", "&#X40;", "@"}) {
		// The reported case encodes the only @ in both label and href.
		std::string address = std::string("kontakt.brain") + at + "gmail.com";
		CheckLink("<a href=\"mailto:" + address + "\">" + address + "</a>",
			"kontakt.brain@gmail.com", "kontakt.brain@gmail.com",
			"mailto:kontakt.brain@gmail.com");
	}
}

static void
TestMailHeadersAndScheme()
{
	CheckLink("<a href=' MAILTO:Kontakt&#64;example.com?subject=Hallo%20Haify"
		"&amp;body=Gr%C3%BC%C3%9Fe '>Mail schreiben</a>",
		"Mail schreiben", "Mail schreiben",
		"mailto:Kontakt@example.com?subject=Hallo%20Haify&body=Gr%C3%BC%C3%9Fe");
	CheckLink("<a href=mailto:kontakt&#x40;example.com>Kontakt</a>",
		"Kontakt", "Kontakt", "mailto:kontakt@example.com");
	CheckLink("<a href='mailto:kontakt%40example.com'>Kontakt</a>",
		"Kontakt", "Kontakt", "mailto:kontakt%40example.com");
	assert(MediaDescriptionLinkIsEmail("mailto:contact@example.com"));
	assert(MediaDescriptionLinkIsEmail("MaIlTo:contact@example.com?subject=Hi"));
	assert(!MediaDescriptionLinkIsEmail(""));
	assert(!MediaDescriptionLinkIsEmail("mailto"));
	assert(!MediaDescriptionLinkIsEmail("contact@example.com"));
	assert(!MediaDescriptionLinkIsEmail("https://example.com/mailto:contact@example.com"));
}

static void
TestLinkOffsetsAndWebLinks()
{
	CheckLink("  Gr&#252;&#223;e: <a href='mailto:k&#64;example.com'>"
		"k&#64;example.com</a>!  ", u8"Grüße: k@example.com!",
		"k@example.com", "mailto:k@example.com");
	CheckLink("<a href='https://example.com/?first=1&amp;second=2'>Web</a>",
		"Web", "Web", "https://example.com/?first=1&second=2");
	// An email-looking label must not override an explicit web destination.
	CheckLink("<a href='https://example.com/contact'>k&#64;example.com</a>",
		"k@example.com", "k@example.com", "https://example.com/contact");
	CheckLink("Besuch: https://example.com/path.",
		"Besuch: https://example.com/path.", "https://example.com/path",
		"https://example.com/path");
	CheckLink("www.example.com", "www.example.com", "www.example.com",
		"https://www.example.com");
}

static void
TestSinglePassDecoding()
{
	CheckLink("<a href='mailto:k&#64;example.com?subject=&amp;#64;'>Mail</a>",
		"Mail", "Mail", "mailto:k@example.com?subject=&#64;");
	CheckLink("<a href='https://example.com/?q=&lt;tag&gt;&amp;raw=%40'>Web</a>",
		"Web", "Web", "https://example.com/?q=<tag>&raw=%40");
	CheckLink("<a href='https://example.com/?q=&unknown;&amp;x=1'>Web</a>",
		"Web", "Web", "https://example.com/?q=&unknown;&x=1");
}

int
main()
{
	TestEncodedEmailAddress();
	TestMailHeadersAndScheme();
	TestLinkOffsetsAndWebLinks();
	TestSinglePassDecoding();
	std::puts("Description text formatter tests passed.");
	return 0;
}
