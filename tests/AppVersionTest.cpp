#include "app/AppVersion.h"

#include <cassert>
#include <cstdio>
#include <limits>

#ifdef NDEBUG
#error Version tests require assertions; compile without NDEBUG.
#endif

static void TestNumbersAndFallback()
{
	version_info info{};
	assert(FormatAppVersion(nullptr, "9.8") == "Version 9.8");
	info.variety = B_BETA_VERSION;
	info.internal = 123;
	assert(FormatAppVersion(&info, "9.8") == "Version 9.8");
	info.major = 1;
	info.middle = 2;
	info.variety = B_FINAL_VERSION;
	assert(FormatAppVersion(&info, "9.8") == "Version 1.2");
	info.minor = 3;
	assert(FormatAppVersion(&info, "9.8") == "Version 1.2.3");
	info.major = info.middle = 0;
	assert(FormatAppVersion(&info, "9.8") == "Version 0.0.3");
	info.major = std::numeric_limits<uint32>::max();
	assert(FormatAppVersion(&info, "9.8") == "Version 4294967295.0.3");
}

static void TestVarietiesAndTranslation()
{
	const struct { uint32 variety = 0; const char* expected = nullptr; } cases[] = {
		{B_DEVELOPMENT_VERSION, "Version 1.2-development"},
		{B_ALPHA_VERSION, "Version 1.2-alpha"},
		{B_BETA_VERSION, "Version 1.2-beta"},
		{B_GAMMA_VERSION, "Version 1.2-gamma"},
		{B_GOLDEN_MASTER_VERSION, "Version 1.2-gold master"},
		{B_FINAL_VERSION, "Version 1.2"}, {999, "Version 1.2"}
	};
	version_info info{};
	info.major = 1;
	info.middle = 2;
	for (const auto& item : cases) {
		info.variety = item.variety;
		assert(FormatAppVersion(&info, "9.8") == item.expected);
	}
	AppVersionLabels translated{"Ausgabe", "Entwicklung", "Alpha", "Beta", "Gamma", "Freigabe"};
	info.variety = B_DEVELOPMENT_VERSION;
	assert(FormatAppVersion(&info, "9.8", translated) == "Ausgabe 1.2-Entwicklung");
	assert(FormatAppVersion(nullptr, "9.8", translated) == "Ausgabe 9.8");
}

int main()
{
	TestNumbersAndFallback();
	TestVarietiesAndTranslation();
	std::puts("App version tests passed.");
}
