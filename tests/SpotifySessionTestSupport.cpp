#include "SpotifySessionTestSupport.h"
#include "network/HttpClient.h"

bool gIsDebug = false;
HaifySettings sessionTestSettings;
status_t sessionTestWriteStatus = B_OK;
int sessionTestWrites = 0;

void ResetSessionTestSettings()
{
	sessionTestSettings = {};
	sessionTestWriteStatus = B_OK;
	sessionTestWrites = 0;
}

HaifySettings SettingsController::Load() { return sessionTestSettings; }
status_t SettingsController::Update(const std::function<void(HaifySettings&)>& update)
{
	++sessionTestWrites;
	if (sessionTestWriteStatus == B_OK) update(sessionTestSettings);
	return sessionTestWriteStatus;
}
std::string SettingsController::CacheFilePath(const std::string&, const std::string&, bool)
{
	return ""; // No real cache files may be touched by fixtures.
}

// Standalone substitutes: requests must use SetRequestHandler, never network I/O.
void HttpClient::Get(const std::string&, const Headers&, HttpCallback) { assert(false); }
void HttpClient::Post(const std::string&, const Headers&, const std::string&, HttpCallback) { assert(false); }
void HttpClient::Put(const std::string&, const Headers&, const std::string&, HttpCallback) { assert(false); }
void HttpClient::Delete(const std::string&, const Headers&, const std::string&, HttpCallback) { assert(false); }
