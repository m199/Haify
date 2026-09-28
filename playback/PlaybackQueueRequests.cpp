#include "playback/PlaybackQueueRequests.h"

#include "messages/Messages.h"
#include "spotify/api/PlaybackApi.h"

#include <Message.h>


void
AddToPlaybackQueue(PlaybackApi& api, const std::string& uri,
	BMessenger application)
{
	api.AddToQueue(uri, [application](bool ok, const nlohmann::json&) {
		if (!ok)
			return;
		BMessage changed(MSG_PLAYBACK_QUEUE_CHANGED);
		application.SendMessage(&changed);
	});
}
