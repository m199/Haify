#pragma once

#include <string>

// Pure classification of one HTTP request received by the local OAuth
// redirect listener. Only "GET /callback" ends the sign-in flow; browsers
// also open idle speculative connections or ask for /favicon.ico, and those
// must be ignored instead of failing the flow.
struct OAuthCallbackRequest {
	bool			isCallback = false;
	std::string		code;
	std::string		state;
	std::string		error;
};

OAuthCallbackRequest	ParseOAuthCallbackRequest(const std::string& request);
