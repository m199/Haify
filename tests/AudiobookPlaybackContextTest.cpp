#include "playback/AudiobookPlaybackContext.h"
#include "playback/AudiobookEndState.h"
#include "playback/LocalPlaybackPresentation.h"
#include "playback/PlaybackStartController.h"
#include "spotify/api/PlaybackApi.h"
#include "JsonApiTestSupport.h"

#include <cstdio>
#include <cstdlib>

static void
CheckCondition(bool condition, const char* expression, int line)
{
	if (condition)
		return;
	std::fprintf(stderr, "%s:%d: %s\n", __FILE__, line, expression);
	std::abort();
}

#define Check(condition) CheckCondition((condition), #condition, __LINE__)

static PlaybackCommand
BookCommand()
{
	PlaybackCommand command{"spotify:episode:0h50Px0zlCSl4GaoeHu0Pb"};
	command.parentKind = "audiobook";
	command.primaryOpenUri = "spotify:audiobook:book";
	command.deviceId = "local-device";
	command.nextQueueUris = {"spotify:episode:next", "spotify:episode:last"};
	return command;
}


static PlaybackMetadata
EpisodeReport()
{
	PlaybackMetadata metadata;
	metadata.title.value = "Track 33";
	metadata.itemKind.value = "episode";
	return metadata;
}


static void
CheckStartWithoutPreviewAndAdvance()
{
	auto command = BookCommand();
	AudiobookPlaybackContext context;
	LocalPlaybackPresentation presentation;
	presentation.Begin(command.uri, 1000000);
	presentation.Dispatched(command.uri, 2000000);
	Check(presentation.Active(2000000)); // No optimistic playback preview.
	context.Begin(command);
	Check(presentation.Defer("spotify:track:old", false, true, 3000000));
	Check(presentation.Defer(command.uri, false, false, 3000000));
	Check(!presentation.Defer(command.uri, false, true, 4000000));
	// First confirmed episode report has no audiobook context or previous metadata.
	auto metadata = ResolvePlaybackMetadata(EpisodeReport(), {}, {command.uri, false, true});
	context.Apply(command.uri, false, metadata);
	Check(metadata.parentKind.value == "audiobook"); // Both playback buttons lock.
	Check(metadata.parentKind.source == PlaybackMetadataSource::PlaybackCommand);
	Check(metadata.itemKind.value == "chapter" && metadata.audiobookId.value == "book");
	Check(metadata.openUri.value == command.primaryOpenUri);
	// The paused progress=0 report from the user log cannot erase book/queue.
	metadata = ResolvePlaybackMetadata(EpisodeReport(), metadata, {command.uri});
	context.Apply(command.uri, false, metadata);
	Check(metadata.parentKind.value == "audiobook");
	AudiobookEndState end;
	Check(end.Claim(command.uri, metadata.parentKind.value) == AudiobookEndAction::Complete);
	auto next = context.TakeNext(command.uri, "local-device");
	Check(next && next->uri == "spotify:episode:next");
	Check(!context.TakeNext(command.uri, "local-device"));
	JsonApiTestTransport transport;
	PlaybackApi api{transport.GetHandler(), transport.BodyHandler("PUT"),
		transport.BodyHandler("POST"), transport.CacheHandler()};
	Check(DispatchPlaybackStart(api, *next, true, {}));
	transport.ExpectJson(0, "PUT", "/me/player/play?device_id=local-device",
		{{"uris", {next->uri}}});
	Check(transport.requests.size() == 1);
	metadata = ResolvePlaybackMetadata(EpisodeReport(), metadata, {next->uri, false, true});
	context.Apply(next->uri, false, metadata);
	Check(metadata.parentKind.value == "audiobook" && metadata.audiobookId.value == "book");
	auto last = context.TakeNext(next->uri, "");
	Check(last && last->uri == "spotify:episode:last" && last->deviceId == command.deviceId);
	Check(!context.TakeNext(last->uri, "local-device"));
}


static void
CheckPendingAndReplacedCommands()
{
	auto command = BookCommand();
	AudiobookPlaybackContext context;
	context.Begin(command);
	PlaybackMetadata other;
	context.Apply("spotify:track:previous", false, other);
	context.Apply("", false, other);
	Check(other.parentKind.value.empty());
	Check(!context.TakeNext("spotify:track:previous", ""));
	auto book = EpisodeReport();
	context.Apply(command.uri, true, book);
	context.Apply("", false, other); // A preview is not playback confirmation.
	context.Apply(command.uri, false, book);
	Check(book.parentKind.value == "audiobook");
	other.parentKind.value = "show";
	context.Apply("spotify:episode:podcast", false, other);
	Check(other.parentKind.value == "show");
	Check(!context.TakeNext(command.uri, ""));
	context.Begin(command);
	context.Begin({"spotify:track:music"});
	book = EpisodeReport();
	context.Apply(command.uri, false, book);
	Check(book.parentKind.value.empty());
	Check(!context.TakeNext(command.uri, ""));
}


int
main()
{
	CheckStartWithoutPreviewAndAdvance();
	CheckPendingAndReplacedCommands();
	std::puts("Audiobook playback context tests passed");
	return 0;
}
