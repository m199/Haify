#include "playback/AudiobookEndState.h"
#include "playback/AudiobookProgress.h"
#include "playback/LibrespotEventState.h"

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

static const std::string kChapter = "spotify:episode:chapter-one";
static constexpr int64_t kStart = 1000000;
static constexpr int64_t kEnd = kStart + 1000000;

static void
CheckFinalReportBeforeTick()
{
	PlaybackTimeline timeline;
	timeline.Store({true, 89000, 90000}, kStart);
	AudiobookEndReport paused{kChapter, {false, 89000, 90000}};
	Check(!AudiobookReportReachesEnd(kChapter, timeline, paused, kStart));
	// An authoritative pause before the end wins over the old running clock.
	Check(!AudiobookReportReachesEnd(kChapter, timeline, paused, kEnd));
	AudiobookEndReport final{kChapter, {false, 90000, 90000}};
	Check(AudiobookReportReachesEnd(kChapter, timeline, final, kEnd));
	final.position.durationMs = 0;
	Check(AudiobookReportReachesEnd(kChapter, timeline, final, kEnd));
	final.optimistic = true;
	Check(!AudiobookReportReachesEnd(kChapter, timeline, final, kEnd));
	final.optimistic = false;
	final.itemUri = "spotify:episode:another";
	Check(!AudiobookReportReachesEnd(kChapter, timeline, final, kEnd));
	AudiobookEndReport noItem{"", {}, false, true, false};
	Check(!AudiobookReportReachesEnd(kChapter, timeline, noItem, kStart));
	Check(AudiobookReportReachesEnd(kChapter, timeline, noItem, kEnd));
	noItem.knownItemState = false;
	Check(!AudiobookReportReachesEnd(kChapter, timeline, noItem, kEnd));
}


static void
CheckCompletionAndReplay()
{
	PlaybackTimeline timeline;
	timeline.Store({true, 89000, 90000}, kStart);
	AudiobookEndState end;
	Check(end.Claim(kChapter, "album") == AudiobookEndAction::NotAudiobook);
	Check(end.Claim("", "audiobook") == AudiobookEndAction::NotAudiobook);
	Check(timeline.RemainingMs(kEnd) == 0);
	Check(end.Claim(kChapter, "audiobook") == AudiobookEndAction::Complete);
	auto completed = ResolveAudiobookChapterProgress({}, kChapter, 90000,
		{kChapter, timeline.DurationMs(), timeline.DurationMs()});
	Check(completed && completed->fullyPlayed);
	// The final poll and local end event following this tick cannot advance again.
	Check(end.Claim(kChapter, "audiobook") == AudiobookEndAction::AlreadyHandled);
	Check(end.Claim(kChapter, "audiobook") == AudiobookEndAction::AlreadyHandled);
	const std::string next = "spotify:episode:chapter-two";
	Check(!ResolveAudiobookChapterProgress(*completed, kChapter, 90000,
		{next, 0, 60000}));
	Check(end.Claim(next, "audiobook") == AudiobookEndAction::Complete);
	// Also deduplicate the last chapter when there is no successor.
	Check(end.Claim(next, "audiobook") == AudiobookEndAction::AlreadyHandled);
	end.Reset();
	Check(end.Claim(next, "audiobook") == AudiobookEndAction::Complete);
}


static void
CheckLocalEndIdentityAndSession()
{
	LibrespotEventState events;
	Check(!events.AcceptEnd("1", "end-one"));
	events.SetSession(1);
	Check(!events.AcceptEnd("old-session", "end-one"));
	Check(!events.AcceptEnd("1", ""));
	Check(events.RememberTrack(kChapter, "opaque-one"));
	Check(events.AcceptEnd("1", "end-one"));
	Check(events.PositionAction(false, "opaque-one", kChapter, "")
		== LibrespotPositionAction::ApplyCurrent);
	Check(events.Accept("1", "stopped-one", false));
	Check(!events.AcceptEnd("1", "end-one"));
	Check(events.PositionAction(false, "other-id", kChapter, "")
		== LibrespotPositionAction::Refresh);
	Check(events.PositionAction(false, "opaque-one", "spotify:episode:next", "")
		== LibrespotPositionAction::Refresh);
	events.SetSession(2);
	Check(!events.AcceptEnd("1", "end-one"));
	Check(events.AcceptEnd("2", "end-one"));
	Check(events.PositionAction(false, "opaque-one", kChapter, "")
		== LibrespotPositionAction::Refresh);
}


int
main()
{
	CheckFinalReportBeforeTick();
	CheckCompletionAndReplay();
	CheckLocalEndIdentityAndSession();
	std::puts("Audiobook end state tests passed");
	return 0;
}
