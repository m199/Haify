#include "spotify/auth/SpotifyClientId.h"
#include "ui/dialogs/SetupAssistantSteps.h"

#include <cstdio>
#include <cstdlib>

static void
CheckCondition(bool condition, const char* expression, const char* function, int line)
{
	if (condition)
		return;
	std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, line, function, expression);
	std::abort();
}

#define Check(condition) CheckCondition((condition), #condition, __func__, __LINE__)

static const std::string kOwn = "0123456789abcdef0123456789ABCDEF";
static const std::string kBuiltIn = "006877f3073c4796a6e7dedc31aadd46";

static void
CheckClientIdFormat()
{
	Check(IsValidSpotifyClientId(kOwn));
	Check(!IsValidSpotifyClientId(""));
	Check(!IsValidSpotifyClientId(kOwn.substr(1)));
	Check(!IsValidSpotifyClientId(kOwn + "0"));
	Check(!IsValidSpotifyClientId("0123456789abcdef0123456789abcdeg"));
	// A paste from the dashboard may carry spaces or a line break.
	Check(NormalizeSpotifyClientId(" " + kOwn + "\r\n") == kOwn);
	Check(IsValidSpotifyClientId(NormalizeSpotifyClientId("\t" + kOwn)));
}

// The dashboard address of an app ends in its Client ID.
static void
CheckClientIdFromDashboardAddress()
{
	const std::string dashboard = "https://developer.spotify.com/dashboard/";
	Check(ExtractSpotifyClientId(kBuiltIn) == kBuiltIn);
	Check(ExtractSpotifyClientId(dashboard + kBuiltIn) == kBuiltIn);
	Check(ExtractSpotifyClientId(dashboard + kBuiltIn + "/settings") == kBuiltIn);
	Check(ExtractSpotifyClientId(dashboard + kBuiltIn + "?tab=users") == kBuiltIn);
	Check(ExtractSpotifyClientId(" developer.spotify.com/dashboard/" + kBuiltIn
		+ "\n") == kBuiltIn);
	// Not an app page, or an ID of the wrong length.
	Check(ExtractSpotifyClientId(dashboard).empty());
	Check(ExtractSpotifyClientId(dashboard + "create").empty());
	Check(ExtractSpotifyClientId(dashboard + kBuiltIn + "0").empty());
	Check(ExtractSpotifyClientId(dashboard + kBuiltIn.substr(1)).empty());
	// Other text on the clipboard, e.g. the Redirect URI from step 2.
	Check(ExtractSpotifyClientId("http://127.0.0.1:8765/callback").empty());
	Check(ExtractSpotifyClientId("https://example.com/dashboard/" + kBuiltIn)
		.empty());
}

static void
CheckClientIdResolution()
{
	// An own registration always wins.
	Check(ResolveSpotifyClientId(kOwn, false, kBuiltIn) == kOwn);
	Check(ResolveSpotifyClientId(kOwn, true, kBuiltIn) == kOwn);
	// New users without a session must run the assistant.
	Check(ResolveSpotifyClientId("", false, kBuiltIn).empty());
	// Existing sessions keep the ID that issued their tokens.
	Check(ResolveSpotifyClientId("", true, kBuiltIn) == kBuiltIn);
	Check(ResolveSpotifyClientId("broken", true, kBuiltIn) == kBuiltIn);
	// Builds without a built-in ID always need the assistant.
	Check(ResolveSpotifyClientId("", true, "").empty());
}

static void
CheckPageNavigation()
{
	SetupAssistantSteps steps;
	Check(steps.Page() == SetupAssistantPage::Welcome);
	Check(!steps.CanGoBack() && !steps.Back());
	Check(steps.Next(false) && steps.Next(false) && steps.Next(false));
	Check(steps.Page() == SetupAssistantPage::EnterClientId);
	// The Client ID page is the only gate.
	Check(!steps.CanGoNext(false) && !steps.Next(false));
	Check(steps.Page() == SetupAssistantPage::EnterClientId);
	Check(steps.Next(true) && steps.IsLast());
	Check(!steps.CanGoNext(true) && !steps.Next(true));
	Check(steps.CanFinish(true) && !steps.CanFinish(false));
	Check(steps.Back() && steps.Page() == SetupAssistantPage::EnterClientId);
	Check(!steps.CanFinish(true));
}

int
main()
{
	CheckClientIdFormat();
	CheckClientIdFromDashboardAddress();
	CheckClientIdResolution();
	CheckPageNavigation();
	std::puts("SpotifySetupAssistantTest passed");
	return 0;
}
