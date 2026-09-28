#include "playback/AudiobookProgress.h"

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

static const std::string kChapter = "spotify:episode:chapter-one";

static void
CheckPlaybackAndSeek()
{
	AudiobookChapterProgress saved{true, false, 20000};
	auto live = ResolveAudiobookChapterProgress(saved, kChapter, 90000,
		{kChapter, 25000, 90000});
	Check(live && live->known && !live->fullyPlayed && live->positionMs == 25000);
	Check(live->source == AudiobookProgressSource::PlaybackSnapshot);
	// Same position while paused, then seeking backward must not keep a maximum.
	auto paused = ResolveAudiobookChapterProgress(*live, kChapter, 90000,
		{kChapter, 25000, 90000});
	Check(paused && paused->positionMs == 25000);
	auto sought = ResolveAudiobookChapterProgress(*paused, kChapter, 90000,
		{kChapter, 5000, 90000});
	Check(sought && sought->positionMs == 5000);
	Check(saved.positionMs == 20000 && saved.source == AudiobookProgressSource::ResumePoint);
}


static void
CheckIdentityAndMissingProgress()
{
	AudiobookChapterProgress saved{true, false, 20000};
	Check(!ResolveAudiobookChapterProgress(saved, kChapter, 90000,
		{"spotify:episode:other-book", 25000, 90000}));
	Check(!ResolveAudiobookChapterProgress(saved, "", 90000,
		{"", 25000, 90000}));
	Check(!ResolveAudiobookChapterProgress(saved, kChapter, 90000, {}));
	Check(!ResolveAudiobookChapterProgress(saved, kChapter, 90000,
		{kChapter, -1, 90000}));
	// A snapshot retained while chapter pages load applies once its row arrives.
	AudiobookPlaybackPosition beforeRows{kChapter, 35000, 90000};
	auto afterLoad = ResolveAudiobookChapterProgress({}, kChapter, 90000, beforeRows);
	Check(afterLoad && afterLoad->known && afterLoad->positionMs == 35000);
}


static void
CheckDurationAndCompletion()
{
	auto result = ResolveAudiobookChapterProgress({}, kChapter, 90000,
		{kChapter, 100000, 0});
	Check(result && result->positionMs == 90000 && result->fullyPlayed);
	result = ResolveAudiobookChapterProgress({}, kChapter, 90000,
		{kChapter, 100000, 80000});
	Check(result && result->positionMs == 80000 && result->fullyPlayed);
	result = ResolveAudiobookChapterProgress({}, kChapter, 0, {kChapter, 100000, 0});
	Check(result && result->positionMs == 100000 && !result->fullyPlayed);
	AudiobookChapterProgress completed{true, true, 90000};
	result = ResolveAudiobookChapterProgress(completed, kChapter, 90000,
		{kChapter, 90000, 90000});
	Check(result && result->fullyPlayed);
	result = ResolveAudiobookChapterProgress(completed, kChapter, 0,
		{kChapter, 90000, 0});
	Check(result && result->fullyPlayed);
	result = ResolveAudiobookChapterProgress(completed, kChapter, 90000,
		{kChapter, 0, 90000});
	Check(result && result->known && !result->fullyPlayed && result->positionMs == 0);
}


int
main()
{
	CheckPlaybackAndSeek();
	CheckIdentityAndMissingProgress();
	CheckDurationAndCompletion();
	std::puts("Audiobook progress tests passed");
	return 0;
}
