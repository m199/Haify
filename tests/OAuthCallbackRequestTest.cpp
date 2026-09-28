#include "network/OAuthCallbackRequest.h"

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

static void
CheckAuthorizationCode()
{
	OAuthCallbackRequest request = ParseOAuthCallbackRequest(
		"GET /callback?code=abc%2D1&state=s+1 HTTP/1.1\r\nHost: x\r\n\r\n");
	Check(request.isCallback);
	Check(request.code == "abc-1");
	Check(request.state == "s 1");
	Check(request.error.empty());
}

static void
CheckProviderError()
{
	OAuthCallbackRequest request = ParseOAuthCallbackRequest(
		"GET /callback?error=access_denied&state=s HTTP/1.1\r\n\r\n");
	Check(request.isCallback && request.code.empty());
	Check(request.error == "access_denied" && request.state == "s");

	OAuthCallbackRequest empty = ParseOAuthCallbackRequest(
		"GET /callback HTTP/1.1\r\n\r\n");
	Check(empty.isCallback && empty.error == "missing_authorization_code");
}

// Regression: these used to end the sign-in flow with an error.
static void
CheckUnrelatedConnectionsAreIgnored()
{
	Check(!ParseOAuthCallbackRequest("").isCallback);
	Check(!ParseOAuthCallbackRequest("GET /favicon.ico HTTP/1.1\r\n\r\n").isCallback);
	Check(!ParseOAuthCallbackRequest("POST /callback?code=x HTTP/1.1\r\n\r\n").isCallback);
	Check(!ParseOAuthCallbackRequest("GET /callbackx?code=x HTTP/1.1\r\n\r\n").isCallback);
	Check(!ParseOAuthCallbackRequest("GARBAGE\r\nHost: a b c\r\n\r\n").isCallback);
}

int
main()
{
	CheckAuthorizationCode();
	CheckProviderError();
	CheckUnrelatedConnectionsAreIgnored();
	std::puts("OAuthCallbackRequestTest passed");
	return 0;
}
