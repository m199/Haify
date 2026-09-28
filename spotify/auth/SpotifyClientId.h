#pragma once

#include <string>

// Rules for the Spotify app registration ("Client ID") Haify signs in with.
//
// Spotify limits an app in development mode to a few allowlisted users and
// no longer grants extended quota to individuals (verified 2026-09-28 at
// developer.spotify.com/documentation/web-api/concepts/quota-modes). Every
// user therefore registers an own app; PKCE means no client secret is needed.

// Removes whitespace a paste from the dashboard may carry along.
inline std::string
NormalizeSpotifyClientId(const std::string& value)
{
	std::string result;
	for (char character : value) {
		if (character != ' ' && character != '\t' && character != '\r'
				&& character != '\n') {
			result += character;
		}
	}
	return result;
}

// Spotify Client IDs are 32 hexadecimal characters.
inline bool
IsValidSpotifyClientId(const std::string& value)
{
	if (value.size() != 32)
		return false;
	for (char character : value) {
		bool digit = character >= '0' && character <= '9';
		bool lower = character >= 'a' && character <= 'f';
		bool upper = character >= 'A' && character <= 'F';
		if (!digit && !lower && !upper)
			return false;
	}
	return true;
}

// Finds a Client ID in pasted or copied text: the ID itself, or the address
// of the app's dashboard page, whose last path segment is the Client ID
// (https://developer.spotify.com/dashboard/<client id>[/settings]).
// Returns "" when the text contains neither.
inline std::string
ExtractSpotifyClientId(const std::string& text)
{
	std::string normalized = NormalizeSpotifyClientId(text);
	if (IsValidSpotifyClientId(normalized))
		return normalized;

	static const std::string kMarker = "developer.spotify.com/dashboard/";
	size_t marker = normalized.find(kMarker);
	if (marker == std::string::npos)
		return "";
	std::string candidate = normalized.substr(marker + kMarker.size(), 32);
	size_t end = marker + kMarker.size() + 32;
	bool delimited = end >= normalized.size() || normalized[end] == '/'
		|| normalized[end] == '?' || normalized[end] == '#';
	return delimited && IsValidSpotifyClientId(candidate) ? candidate : "";
}

// The ID used for sign-in and token refresh, or "" when the setup assistant
// has to run first. The built-in ID only keeps sessions working that were
// created with it before per-user registrations existed.
inline std::string
ResolveSpotifyClientId(const std::string& configured, bool hasStoredSession,
	const std::string& builtIn)
{
	std::string normalized = NormalizeSpotifyClientId(configured);
	if (IsValidSpotifyClientId(normalized))
		return normalized;
	if (hasStoredSession && IsValidSpotifyClientId(builtIn))
		return builtIn;
	return "";
}
