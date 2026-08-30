#include "PlayerWindow.h"
#include "DiscoverWindow.h"
#include "PlaylistWindow.h"
#include "PlayerBarView.h"
#include "SettingsController.h"
#include "TrackContextMenu.h"
#include "Messages.h"
#include "NowPlayingItem.h"
#include "App.h"
#include "HaifyDebug.h"
#include "Config.h"
#include "UiLogic.h"
#include "spotify/SpotifyUri.h"
#include "spotify/api/SpotifyApi.h"
#include "spotify/api/SpotifyResponse.h"
#include <nlohmann/json.hpp>

#include <AboutWindow.h>
#include <Application.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Catalog.h>
#include <OS.h>
#include <Size.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlayerWindow"

static const float kMinPlayerWindowWidth = 520.0f;
static const bigtime_t kPlaybackStartupPollInterval = 1000000LL;
static const bigtime_t kPlaybackStartupPollLimit = 5000000LL;
static const bigtime_t kPlaybackStartupEmptyItemRetryLimit = 30000000LL;
static const bigtime_t kPlaybackActivePollInterval = 5000000LL;
static const bigtime_t kPlaybackIdlePollInterval = 15000000LL;
static const bigtime_t kPlaybackErrorPollLimit = 30000000LL;
static const bigtime_t kLocalPlaybackInterval = 1000000LL;
static const bigtime_t kVerifyPollDelay = 1500000LL;
static const bigtime_t kOptimisticTrackGuard = 15000000LL;
static const bigtime_t kSeekGuard = 5000000LL;
static const bigtime_t kVolumeGuard = 5000000LL;
static const int32 kSeekProgressToleranceMs = 2000;
static const int32 kQueuePrefetchRemainingMs = 30000;
static const uint32 kMsgVerifyPoll = 'vpol';
static const uint32 kMsgPlaybackTick = 'ptik';
static const uint32 kMsgPlaybackPollResult = 'pbrs';
static const uint32 kMsgAudiobookContextResult = 'abcr';
static const uint32 kMsgApplyMuteToggle = 'amte';


static bool
_IsLikelyHexIdentifier(const std::string& value)
{
	if (value.size() < 16)
		return false;

	for (unsigned char character : value) {
		if (!std::isxdigit(character))
			return false;
	}
	return true;
}


static std::string
_SpotifyDeviceDisplayName(const std::string& id, const std::string& name,
	const std::string& type)
{
	if (!name.empty() && name != id && !_IsLikelyHexIdentifier(name))
		return name;

	std::string display = type.empty() ? B_TRANSLATE("Device") : type;
	std::string shortId = id.empty() ? name : id;
	if (!shortId.empty()) {
		const size_t idLength = std::min<size_t>(8, shortId.size());
		display += " (";
		display += shortId.substr(0, idLength);
		display += ")";
	}
	return display;
}


static bool
_FillTrackMessage(BMessage& message, const nlohmann::json& item,
	const std::string& fallbackType)
{
	if (!item.is_object())
		return false;

	NowPlayingItem::FromSpotifyItem(item, fallbackType).AddToMessage(message);
	return true;
}

static void
_FillPlaybackPollMessage(BMessage& message, bool ok,
	const nlohmann::json& data)
{
	bool valid = ok && data.is_object() && !data.is_null();
	message.AddBool("poll_ok", valid);
	if (!valid && data.is_object()) {
		message.AddInt32("http_status", data.value("status", -1));
		message.AddInt32("retry_after", data.value("retry_after", -1));
	}
	if (!valid) return;
	message.AddBool("is_playing", data.value("is_playing", false));
	message.AddInt32("progress_ms", (int32)data.value("progress_ms", 0));
	message.AddString("repeat_state", data.value("repeat_state", "off").c_str());
	message.AddBool("shuffle_state", data.value("shuffle_state", false));
	if (data.contains("device") && data["device"].is_object()) {
		const auto& device = data["device"];
		if (device.contains("volume_percent")
				&& !device["volume_percent"].is_null())
			message.AddInt32("volume_percent",
				(int32)device.value("volume_percent", 0));
	}
	bool hasItem = data.contains("item") && data["item"].is_object();
	message.AddBool("has_item", hasItem);
	if (hasItem)
		_FillTrackMessage(message, data["item"],
			data.value("currently_playing_type", ""));
}


static bool
_JsonBoolOrDefault(const nlohmann::json& object, const char* key,
	bool fallback)
{
	if (!object.contains(key) || !object[key].is_boolean())
		return fallback;
	return object[key].get<bool>();
}


static void
_AddMuteToggleDeviceFields(BMessage& message, const nlohmann::json& data)
{
	if (!data.is_object() || !data.contains("device")
			|| !data["device"].is_object()) {
		return;
	}

	const auto& device = data["device"];
	if (device.contains("id") && device["id"].is_string()) {
		std::string deviceId = device["id"].get<std::string>();
		message.AddString("device_id", deviceId.c_str());
	}
	if (device.contains("volume_percent")
			&& device["volume_percent"].is_number()) {
		message.AddInt32("volume_percent",
			(int32)device["volume_percent"].get<int>());
	}
	message.AddBool("supports_volume",
		_JsonBoolOrDefault(device, "supports_volume", true));
	message.AddBool("is_restricted",
		_JsonBoolOrDefault(device, "is_restricted", false));
}


PlayerWindow::PlayerWindow()
	: BWindow(BRect(200, 100,
		200 + kDefaultPlayerWindowWidth,
		100 + kDefaultPlayerWindowHeight), "Haify",
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS)
{
	HaifySettings s = SettingsController::Load();

	_InitMenu();
	_InitLayout();
	rgb_color seekBarColor = {
		(uint8)s.seekBarColorRed,
		(uint8)s.seekBarColorGreen,
		(uint8)s.seekBarColorBlue,
		(uint8)s.seekBarColorAlpha
	};
	if (fPlayerBar)
		fPlayerBar->SetSeekBarColor(s.seekBarUseSystemColor, seekBarColor);

	if (s.playerWindowW > 0) {
		MoveTo(s.playerWindowX, s.playerWindowY);
		ResizeTo(s.playerWindowW, s.playerWindowH);
		_ApplySizeLimits();
	}
}


bool
PlayerWindow::QuitRequested()
{
	BRect f = Frame();
	SettingsController::Update([&](HaifySettings& s) {
		s.playerWindowX = f.left;  s.playerWindowY = f.top;
		s.playerWindowW = f.Width(); s.playerWindowH = f.Height();
	});

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
PlayerWindow::_PollPlayback()
{
	if (fPlaybackRequestPending)
		return;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api) {
		fPlaybackPollFailures++;
		int32 shift = std::min(fPlaybackPollFailures - 1, (int32)5);
		bigtime_t delay = kPlaybackStartupPollInterval << shift;
		_SchedulePlaybackPoll(std::min(delay, fHasPlaybackState
			? kPlaybackErrorPollLimit : kPlaybackStartupPollLimit));
		return;
	}

	fPlaybackRequestPending = true;
	BMessenger self(this);
	api->Playback().GetPlaybackState([self, api](bool ok,
			const nlohmann::json& data) {
		// Do not double the traffic during rate limits or outages. The legacy
		// endpoint is only useful when Spotify reports that the state endpoint
		// itself is unavailable for this playback session.
		if (!ok && SpotifyResponseStatus(data) == 404) {
			api->Playback().GetCurrentlyPlaying([self](bool fallbackOk,
					const nlohmann::json& fallbackData) {
				BMessage fallback(kMsgPlaybackPollResult);
				_FillPlaybackPollMessage(fallback, fallbackOk, fallbackData);
				self.SendMessage(&fallback);
			});
			return;
		}
		BMessage msg(kMsgPlaybackPollResult);
		_FillPlaybackPollMessage(msg, ok, data);
		self.SendMessage(&msg);
	});
}

void
PlayerWindow::_SchedulePlaybackPoll(bigtime_t delay)
{
	delete fPollTimer;
	fPollTimer = nullptr;
	BMessage message('poll');
	if (delay <= 0) {
		PostMessage(&message);
		return;
	}
	fPollTimer = new BMessageRunner(BMessenger(this), &message, delay, 1);
}


void
PlayerWindow::_FetchQueuePrediction()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || fCurrentTrackUri.empty() || fQueueRequestPending)
		return;

	fQueueRequestPending = true;
	fQueueTrackUri = fCurrentTrackUri;

	BMessenger self(this);
	std::string trackUri = fCurrentTrackUri;
	api->Playback().GetQueue([self, trackUri](bool ok,
			const nlohmann::json& data) {
		BMessage msg('qprd');
		msg.AddString("source_track_uri", trackUri.c_str());
		if (ok && data.contains("queue") && data["queue"].is_array()
				&& !data["queue"].empty()) {
			for (const auto& item : data["queue"]) {
				if (item.is_object()
						&& _FillTrackMessage(msg, item,
							item.value("type", ""))) {
					msg.AddBool("has_next", true);
					break;
				}
			}
		}
		self.SendMessage(&msg);
	});
}


void
PlayerWindow::_ScheduleVerifyPoll(bigtime_t delay)
{
	delete fVerifyTimer;
	fVerifyTimer = nullptr;

	BMessage message(kMsgVerifyPoll);
	fVerifyTimer = new BMessageRunner(BMessenger(this), &message, delay, 1);
}


void
PlayerWindow::_ApplyPredictedNext()
{
	if (!fHasPredictedNext)
		return;

	fOptimisticSourceTrackUri = fCurrentTrackUri;
	fOptimisticUntilUs = system_time() + kOptimisticTrackGuard;

	BMessage predicted(fPredictedNext);
	if (predicted.ReplaceBool("is_playing", true) != B_OK)
		predicted.AddBool("is_playing", true);
	if (predicted.ReplaceInt32("progress_ms", 0) != B_OK)
		predicted.AddInt32("progress_ms", 0);
	if (predicted.ReplaceString("repeat_state", fRepeatState.c_str()) != B_OK)
		predicted.AddString("repeat_state", fRepeatState.c_str());
	if (predicted.ReplaceBool("shuffle_state", fShuffleOn) != B_OK)
		predicted.AddBool("shuffle_state", fShuffleOn);
	if (fVolumePct >= 0) {
		if (predicted.ReplaceInt32("volume_percent", fVolumePct) != B_OK)
			predicted.AddInt32("volume_percent", fVolumePct);
	}
	predicted.RemoveName("source_track_uri");
	predicted.AddBool("optimistic", true);

	fHasPredictedNext = false;
	fPredictedNext.MakeEmpty();
	_ApplyPlaybackMessage(&predicted);
	_ScheduleVerifyPoll(kVerifyPollDelay);
}


void
PlayerWindow::_HandlePlaybackTick()
{
	_ReadLibrespotEvent();

	if (!fIsPlaying || fDurationMs <= 0 || fLastPlaybackSyncUs <= 0)
		return;

	int64 elapsedMs = (system_time() - fLastPlaybackSyncUs) / 1000LL;
	int64 estimated = (int64)fProgressMs + elapsedMs;
	if (estimated > fDurationMs)
		estimated = fDurationMs;

	int32 remainingMs = fDurationMs - (int32)estimated;
	if (remainingMs <= kQueuePrefetchRemainingMs && !fHasPredictedNext
			&& !fQueueRequestPending && fQueueTrackUri != fCurrentTrackUri) {
		_FetchQueuePrediction();
	}

	if (remainingMs <= 0) {
		if (fHasPredictedNext)
			_ApplyPredictedNext();
		else if (!fVerifyTimer)
			_ScheduleVerifyPoll(kVerifyPollDelay);
	}
}


void
PlayerWindow::_ApplyPlaybackMessage(BMessage* message)
{
	delete fVerifyTimer;
	fVerifyTimer = nullptr;

	bool isPlaying    = message->GetBool("is_playing", false);
	int32 progressMs  = message->GetInt32("progress_ms", 0);
	int32 durationMs  = message->GetInt32("duration_ms", 0);
	int32 volumePct   = message->GetInt32("volume_percent", -1);
	const char* title       = message->GetString("title",       "");
	const char* artist      = message->GetString("artist",      "");
	const char* albumId     = message->GetString("album_id",    "");
	const char* artistId    = message->GetString("artist_id",   "");
	const char* itemKind    = message->GetString(kNowPlayingItemKindField, "");
	const char* openUri = message->GetString(
		kNowPlayingPrimaryOpenUriField, "");
	const char* parentUri = message->GetString(kNowPlayingParentUriField, "");
	const char* parentKind = message->GetString(kNowPlayingParentKindField, "");
	const char* showId = message->GetString(kNowPlayingShowIdField, "");
	const char* audiobookId = message->GetString(
		kNowPlayingAudiobookIdField, "");
	const char* repeatState = message->GetString("repeat_state","off");
	bool shuffleState = message->GetBool("shuffle_state", false);
	const char* artworkUrl  = message->GetString("artwork_url",  "");
	const char* trackUri    = message->GetString("track_uri",    "");
	bool optimistic = message->GetBool("optimistic", false);
	bool preserveCurrentArtwork
		= message->GetBool("preserve_current_artwork", false);
	bool volumeAuthoritative
		= message->GetBool("volume_authoritative", true);
	std::string effectiveTitle = title;
	std::string effectiveArtist = artist;
	std::string effectiveAlbumId = albumId;
	std::string effectiveArtistId = artistId;
	std::string effectiveItemKind = itemKind;
	std::string effectiveOpenUri = openUri;
	std::string effectiveParentUri = parentUri;
	std::string effectiveParentKind = parentKind;
	std::string effectiveShowId = showId;
	std::string effectiveAudiobookId = audiobookId;
	std::string effectiveArtworkUrl = ResolvePlaybackArtworkUrl(artworkUrl,
		fLastArtworkUrl, preserveCurrentArtwork);

	bool hasItem = true;
	bool knownItemState = message->FindBool("has_item", &hasItem) == B_OK;
	bool guardActive = system_time() < fOptimisticUntilUs;
	if (ShouldDeferOptimisticPlaybackPoll(optimistic, guardActive,
			trackUri[0] != 0, trackUri, fCurrentTrackUri,
			fOptimisticSourceTrackUri, knownItemState, hasItem)) {
		_ScheduleVerifyPoll(kVerifyPollDelay);
		return;
	}

	bool trackChanged = (trackUri[0] && fCurrentTrackUri != trackUri)
		|| (knownItemState && !hasItem && !fCurrentTrackUri.empty());
	if (trackChanged) {
		fSeekGuardUntilUs = 0;
		fSeekTargetMs = 0;
	}
	if (!optimistic && !trackChanged && fSeekGuardUntilUs > 0) {
		bigtime_t now = system_time();
		if (now < fSeekGuardUntilUs) {
			int32 estimatedMs = fSeekTargetMs;
			if (isPlaying && fLastPlaybackSyncUs > 0)
				estimatedMs += (int32)((now - fLastPlaybackSyncUs) / 1000LL);
			if (durationMs > 0 && estimatedMs > durationMs)
				estimatedMs = durationMs;
			if (estimatedMs < 0)
				estimatedMs = 0;

			int32 delta = progressMs - estimatedMs;
			if (delta < 0)
				delta = -delta;
			if (delta > kSeekProgressToleranceMs) {
				progressMs = estimatedMs;
				fSeekTargetMs = progressMs;
				_ScheduleVerifyPoll(kVerifyPollDelay);
			} else {
				fSeekGuardUntilUs = 0;
			}
		} else {
			fSeekGuardUntilUs = 0;
		}
	}
	if (!optimistic && trackUri[0] && fCurrentTrackUri == trackUri) {
		fOptimisticSourceTrackUri.clear();
		fOptimisticUntilUs = 0;
	}
	if (!optimistic && volumeAuthoritative && volumePct >= 0
			&& !_AcceptReportedVolume(volumePct)) {
		volumePct = fVolumePct;
	}

	bool preserveCurrentMetadata = ShouldPreserveCurrentNowPlayingMetadata(
		optimistic, trackChanged, trackUri[0] != 0);
	bool preserveCurrentAudiobookContext = ShouldPreserveCurrentAudiobookContext(
		optimistic, trackChanged, trackUri[0] != 0, fCurrentParentKind,
		effectiveParentKind, fCurrentPrimaryOpenUri, effectiveOpenUri);
	effectiveTitle = ResolveNowPlayingFallbackField(effectiveTitle,
		fCurrentTitle, preserveCurrentMetadata);
	effectiveArtist = ResolveNowPlayingFallbackField(effectiveArtist,
		fCurrentArtist, preserveCurrentMetadata);
	effectiveAlbumId = ResolveNowPlayingFallbackField(effectiveAlbumId,
		fCurrentAlbumId, preserveCurrentMetadata);
	effectiveArtistId = ResolveNowPlayingFallbackField(effectiveArtistId,
		fCurrentArtistId, preserveCurrentMetadata);

	bool preserveCurrentItemContext = preserveCurrentMetadata;
	if (preserveCurrentAudiobookContext) {
		effectiveArtist = fCurrentArtist;
		effectiveItemKind = fCurrentItemKind;
		effectiveOpenUri = fCurrentPrimaryOpenUri;
		effectiveParentUri = fCurrentParentUri;
		effectiveParentKind = fCurrentParentKind;
		effectiveShowId = fCurrentShowId;
		effectiveAudiobookId = fCurrentAudiobookId;
		preserveCurrentItemContext = false;
	}
	effectiveItemKind = ResolveNowPlayingFallbackField(effectiveItemKind,
		fCurrentItemKind, preserveCurrentItemContext);
	effectiveOpenUri = ResolveNowPlayingFallbackField(effectiveOpenUri,
		fCurrentPrimaryOpenUri, preserveCurrentItemContext);
	effectiveParentUri = ResolveNowPlayingFallbackField(effectiveParentUri,
		fCurrentParentUri, preserveCurrentItemContext);
	effectiveParentKind = ResolveNowPlayingFallbackField(effectiveParentKind,
		fCurrentParentKind, preserveCurrentItemContext);
	effectiveShowId = ResolveNowPlayingFallbackField(effectiveShowId,
		fCurrentShowId, preserveCurrentItemContext);
	effectiveAudiobookId = ResolveNowPlayingFallbackField(effectiveAudiobookId,
		fCurrentAudiobookId, preserveCurrentItemContext);
	effectiveArtworkUrl = ResolveNowPlayingFallbackField(effectiveArtworkUrl,
		fLastArtworkUrl, optimistic);
	if (effectiveOpenUri.empty() && trackUri[0])
		effectiveOpenUri = trackUri;

	if (!optimistic || !effectiveTitle.empty())
		fCurrentTitle = effectiveTitle;
	if (!optimistic || !effectiveArtist.empty())
		fCurrentArtist = effectiveArtist;
	if (!optimistic || !effectiveAlbumId.empty())
		fCurrentAlbumId = effectiveAlbumId;
	if (!optimistic || !effectiveArtistId.empty())
		fCurrentArtistId = effectiveArtistId;
	if (!optimistic || !effectiveItemKind.empty())
		fCurrentItemKind = effectiveItemKind;
	if (!optimistic || !effectiveOpenUri.empty())
		fCurrentPrimaryOpenUri = effectiveOpenUri;
	if (!optimistic || !effectiveParentUri.empty())
		fCurrentParentUri = effectiveParentUri;
	if (!optimistic || !effectiveParentKind.empty())
		fCurrentParentKind = effectiveParentKind;
	if (!optimistic || !effectiveShowId.empty())
		fCurrentShowId = effectiveShowId;
	if (!optimistic || !effectiveAudiobookId.empty())
		fCurrentAudiobookId = effectiveAudiobookId;

	fIsPlaying = isPlaying;
	fProgressMs = progressMs;
	fDurationMs = durationMs;
	fLastPlaybackSyncUs = system_time();
	if (volumePct >= 0)
		fVolumePct = volumePct;
	if (fVolumePct > 0) {
		fLastNonZeroVolume = fVolumePct;
		fHasLastNonZeroVolume = true;
		fMutedByHaify = false;
	}
	fRepeatState = repeatState;
	fShuffleOn   = shuffleState;

	if (fPlayerBar) {
		fPlayerBar->SetTrack(effectiveTitle.c_str(), effectiveArtist.c_str());
		fPlayerBar->SetTrackUri(trackUri);
		fPlayerBar->SetOpenUri(effectiveOpenUri.c_str());
		fPlayerBar->SetTrackIds(effectiveAlbumId.c_str(), effectiveArtistId.c_str());
		fPlayerBar->SetPlaybackOptionsEnabled(
			effectiveParentKind != "audiobook");
		fPlayerBar->SetPlaying(isPlaying);
		fPlayerBar->SetPosition(
			(bigtime_t)progressMs  * 1000LL,
			(bigtime_t)durationMs  * 1000LL);
		if (volumePct >= 0)
			fPlayerBar->SetVolume(volumePct);
		fPlayerBar->SetShuffle(shuffleState);
		fPlayerBar->SetRepeat(repeatState);
		_ApplySizeLimits();
	}

	if (trackChanged) {
		if (!optimistic) {
			fOptimisticSourceTrackUri.clear();
			fOptimisticUntilUs = 0;
		}
		fCurrentTrackUri = trackUri;
		fQueueTrackUri.clear();
		fHasPredictedNext = false;
		fPredictedNext.MakeEmpty();
		BMessage nowPlaying('pStU');
		nowPlaying.AddString("trackUri", trackUri);
		be_app->PostMessage(&nowPlaying);
	}

	fLastArtworkUrl = effectiveArtworkUrl;
	_ResolveAudiobookContextForPlayback(trackUri, effectiveParentKind,
		effectiveOpenUri);

	{
		BMessage stateMsg(MSG_REPLICANT_STATE);
		_FillReplicantStateMessage(stateMsg, progressMs);
		be_app->PostMessage(&stateMsg);
	}

	if (trackChanged && fIsPlaying)
		_FetchQueuePrediction();
	else
		_HandlePlaybackTick();
}


void
PlayerWindow::_ApplyOptimisticPlay(BMessage* message)
{
	const char* uri = message->GetString("uri", "");
	if (!uri || !uri[0])
		uri = message->GetString("trackUri", "");
	if (!uri || !uri[0])
		return;

	std::string oldTrackUri = fCurrentTrackUri;
	BMessage optimistic('pbst');
	optimistic.AddBool("optimistic", true);
	optimistic.AddBool("is_playing", true);
	optimistic.AddInt32("progress_ms", 0);
	optimistic.AddInt32("duration_ms", message->GetInt32("duration_ms", 0));
	optimistic.AddString("repeat_state", fRepeatState.c_str());
	optimistic.AddBool("shuffle_state", fShuffleOn);
	if (fVolumePct >= 0)
		optimistic.AddInt32("volume_percent", fVolumePct);
	optimistic.AddString("track_uri", uri);
	optimistic.AddString("title", message->GetString("title", ""));
	optimistic.AddString("artist", message->GetString("artist", ""));
	optimistic.AddString("album_id", message->GetString("album_id", ""));
	optimistic.AddString("artist_id", message->GetString("artist_id", ""));
	optimistic.AddString(kNowPlayingItemKindField,
		message->GetString(kNowPlayingItemKindField, ""));
	optimistic.AddString(kNowPlayingPrimaryOpenUriField,
		message->GetString(kNowPlayingPrimaryOpenUriField, uri));
	optimistic.AddString(kNowPlayingParentUriField,
		message->GetString(kNowPlayingParentUriField, ""));
	optimistic.AddString(kNowPlayingParentKindField,
		message->GetString(kNowPlayingParentKindField, ""));
	optimistic.AddString(kNowPlayingShowIdField,
		message->GetString(kNowPlayingShowIdField, ""));
	optimistic.AddString(kNowPlayingAudiobookIdField,
		message->GetString(kNowPlayingAudiobookIdField, ""));
	optimistic.AddString("artwork_url", message->GetString("artwork_url", ""));

	if (!oldTrackUri.empty() && oldTrackUri != uri) {
		fOptimisticSourceTrackUri = oldTrackUri;
		fOptimisticUntilUs = system_time() + kOptimisticTrackGuard;
	}

	_ApplyPlaybackMessage(&optimistic);
}


void
PlayerWindow::_ResolveAudiobookContextForPlayback(
	const std::string& trackUri, const std::string& parentKind,
	const std::string& openUri)
{
	if (SpotifyItemKindForUri(trackUri) != kSpotifyItemEpisode)
		return;
	if (parentKind == "audiobook"
			|| SpotifyItemKindForUri(openUri) == kSpotifyItemAudiobook)
		return;
	if (parentKind != "show"
			&& SpotifyItemKindForUri(openUri) != kSpotifyItemShow)
		return;
	if (fAudiobookContextRequestPending
			&& fAudiobookContextRequestTrackUri == trackUri) {
		return;
	}
	if (fLastAudiobookContextLookupTrackUri == trackUri)
		return;

	App* app = dynamic_cast<App*>(be_app);
	SpotifyApi* api = app ? app->GetApi() : nullptr;
	if (!api)
		return;

	fAudiobookContextRequestPending = true;
	fAudiobookContextRequestTrackUri = trackUri;
	fLastAudiobookContextLookupTrackUri = trackUri;

	BMessenger self(this);
	std::string chapterId = SpotifyItemIdForUri(trackUri);
	api->Content().GetChapter(chapterId, [self, trackUri](bool ok,
			const nlohmann::json& chapter) {
		BMessage result(kMsgAudiobookContextResult);
		result.AddString("source_track_uri", trackUri.c_str());
		result.AddBool("ok", ok);
		if (ok && chapter.is_object())
			_FillTrackMessage(result, chapter, "chapter");
		self.SendMessage(&result);
	});
}


void
PlayerWindow::_PlayUri(BMessage* message)
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;

	const char* uri = message->GetString("uri", "");
	if (!uri || !uri[0])
		uri = message->GetString("trackUri", "");
	if (!uri || !uri[0])
		return;

	std::string uriStr = uri;
	std::string contextUri = message->GetString("context_uri", "");
	std::string nextQueueUri = message->GetString("next_queue_uri", "");

	SpotifyItemKind kind = SpotifyItemKindForUri(uriStr);
	if (SpotifyItemIsPlayable(kind)) {
		_ApplyOptimisticPlay(message);

		if (kind == kSpotifyItemTrack) {
			BMessenger self(this);
			std::string trackId = SpotifyItemIdForUri(uriStr);
			std::string repeatState = fRepeatState;
			bool shuffleOn = fShuffleOn;
			int32 volumePct = fVolumePct;
			api->Content().GetTrack(trackId, [self, repeatState, shuffleOn,
					volumePct](bool ok, const nlohmann::json& data) {
				if (!ok || !data.is_object())
					return;
				BMessage msg('pbst');
				msg.AddBool("optimistic", true);
				msg.AddBool("is_playing", true);
				msg.AddInt32("progress_ms", 0);
				msg.AddString("repeat_state", repeatState.c_str());
				msg.AddBool("shuffle_state", shuffleOn);
				if (volumePct >= 0)
					msg.AddInt32("volume_percent", volumePct);
				_FillTrackMessage(msg, data, "track");
				self.SendMessage(&msg);
			});
		}

		api->Playback().PlayTrack(uriStr, contextUri,
			[api, nextQueueUri](bool ok, const nlohmann::json&) {
			if (ok && !nextQueueUri.empty())
				api->Playback().AddToQueue(nextQueueUri, nullptr);
		});
		_ScheduleVerifyPoll(kVerifyPollDelay);
		return;
	}

	api->Playback().PlayContext(uriStr, nullptr);
	_ScheduleVerifyPoll(kVerifyPollDelay);
}


void
PlayerWindow::_ReadLibrespotEvent()
{
	auto readEventFile = [this](const std::string& path,
			std::string& lastEventId) {
		std::ifstream file(path);
		if (!file.is_open())
			return;

		std::map<std::string, std::string> fields;
		std::string line;
		while (std::getline(file, line)) {
			size_t pos = line.find('=');
			if (pos == std::string::npos)
				continue;
			fields[line.substr(0, pos)] = line.substr(pos + 1);
		}

		auto it = fields.find("event_id");
		if (it == fields.end() || it->second.empty()
				|| it->second == lastEventId) {
			return;
		}

		lastEventId = it->second;
		_ApplyLibrespotEvent(fields);
	};

	std::string statePath = SettingsController::LibrespotEventStatePath();
	readEventFile(statePath, fLastLibrespotTrackEventId);
	readEventFile(statePath + ".playback", fLastLibrespotPlaybackEventId);
}


void
PlayerWindow::_ApplyLibrespotEvent(
	const std::map<std::string, std::string>& fields)
{
	auto value = [&](const char* key) -> std::string {
		auto it = fields.find(key);
		return it == fields.end() ? "" : it->second;
	};

	std::string event = value("event");
	if (event == "track_changed") {
		std::string trackUri = value("uri");
		int32 durationMs = (int32)strtol(
			value("duration_ms").c_str(), nullptr, 10);
		int32 reportedProgressMs = (int32)strtol(
			value("position_ms").c_str(), nullptr, 10);
		bool sameTrack = !trackUri.empty() && fCurrentTrackUri == trackUri;
		bigtime_t elapsedSinceSyncMs = fLastPlaybackSyncUs > 0
			? (system_time() - fLastPlaybackSyncUs) / 1000LL : 0;
		int32 progressMs = ResolveTrackChangedProgress(reportedProgressMs,
			fProgressMs, elapsedSinceSyncMs, sameTrack, fIsPlaying, durationMs);

		BMessage msg('pbst');
		msg.AddBool("is_playing", fIsPlaying);
		msg.AddInt32("progress_ms", progressMs);
		msg.AddInt32("duration_ms", durationMs);
		msg.AddString("track_uri", trackUri.c_str());
		msg.AddString("title", value("name").c_str());
		msg.AddString("artist", value("artist").c_str());
		// Librespot is authoritative for immediate playback events, but artwork
		// metadata always comes from the Spotify Web API. Keep the last API URL
		// until the immediate playback poll returns the new track metadata.
		msg.AddBool("preserve_current_artwork", true);
		msg.AddString("repeat_state", fRepeatState.c_str());
		msg.AddBool("shuffle_state", fShuffleOn);
		if (fVolumePct >= 0)
			msg.AddInt32("volume_percent", fVolumePct);
		msg.AddBool("volume_authoritative", false);
		if (ShouldDeferLibrespotTrackChanged(sameTrack)) {
			fPendingLibrespotTrack = msg;
			fHasPendingLibrespotTrack = true;
			_SchedulePlaybackPoll(0);
			return;
		}
		fPendingLibrespotTrack.MakeEmpty();
		fHasPendingLibrespotTrack = false;
		_ApplyPlaybackMessage(&msg);
		return;
	}

	if (event == "playing" || event == "paused" || event == "seeked"
			|| event == "position_correction") {
		int32 positionMs = (int32)strtol(value("position_ms").c_str(),
			nullptr, 10);
		if (event == "playing") {
			fIsPlaying = true;
			if (fHasPendingLibrespotTrack) {
				BMessage pending(fPendingLibrespotTrack);
				pending.ReplaceBool("is_playing", true);
				pending.ReplaceInt32("progress_ms", positionMs);
				fPendingLibrespotTrack.MakeEmpty();
				fHasPendingLibrespotTrack = false;
				_ApplyPlaybackMessage(&pending);
				return;
			}
		} else if (event == "paused") {
			fIsPlaying = false;
		}
		fProgressMs = positionMs;
		fLastPlaybackSyncUs = system_time();
		if (fPlayerBar) {
			fPlayerBar->SetPlaying(fIsPlaying);
			fPlayerBar->SetPosition((bigtime_t)fProgressMs * 1000LL,
				(bigtime_t)fDurationMs * 1000LL);
		}
		return;
	}

	if (event == "shuffle_changed") {
		std::string shuffle = value("shuffle");
		fShuffleOn = shuffle == "True" || shuffle == "true";
		fHasPredictedNext = false;
		fQueueTrackUri.clear();
		if (fPlayerBar)
			fPlayerBar->SetShuffle(fShuffleOn);
		return;
	}

	if (event == "repeat_changed") {
		std::string repeat = value("repeat");
		fRepeatState = (repeat == "True" || repeat == "true")
			? "context" : "off";
		fHasPredictedNext = false;
		fQueueTrackUri.clear();
		if (fPlayerBar)
			fPlayerBar->SetRepeat(fRepeatState.c_str());
		return;
	}

	if (event == "volume_changed") {
		int32 rawVolume = (int32)strtol(value("volume").c_str(),
			nullptr, 10);
		if (rawVolume >= 0) {
			int32 reportedVolume = (rawVolume * 100 + 32767) / 65535;
			if (reportedVolume < 0)
				reportedVolume = 0;
			if (reportedVolume > 100)
				reportedVolume = 100;
			if (!_AcceptReportedVolume(reportedVolume))
				return;
			fVolumePct = reportedVolume;
			if (fVolumePct > 0) {
				fLastNonZeroVolume = fVolumePct;
				fHasLastNonZeroVolume = true;
				fMutedByHaify = false;
			}
			if (fPlayerBar)
				fPlayerBar->SetVolume(fVolumePct);
		}
	}
}


void
PlayerWindow::_SetVolumeOptimistically(int32 volume)
{
	if (volume < 0)
		volume = 0;
	if (volume > 100)
		volume = 100;

	fVolumePct = volume;
	fVolumeTargetPct = volume;
	fVolumeGuardUntilUs = system_time() + kVolumeGuard;
	fMutedByHaify = volume == 0;
	if (volume > 0) {
		fLastNonZeroVolume = volume;
		fHasLastNonZeroVolume = true;
	}
	if (fPlayerBar)
		fPlayerBar->SetVolume(volume);
	_PublishReplicantState();
	_ScheduleVerifyPoll(kVerifyPollDelay);
}


bool
PlayerWindow::_AcceptReportedVolume(int32 volume)
{
	if (fVolumeGuardUntilUs <= 0)
		return true;

	bool guardActive = system_time() < fVolumeGuardUntilUs;
	if (!guardActive) {
		fVolumeGuardUntilUs = 0;
		fVolumeTargetPct = -1;
		return true;
	}

	if (ShouldAcceptReportedVolume(volume, fVolumeTargetPct, true))
		return true;

	_ScheduleVerifyPoll(kVerifyPollDelay);
	return false;
}


void
PlayerWindow::_PublishReplicantState()
{
	if (fCurrentTrackUri.empty() && fCurrentTitle.empty()
			&& fCurrentArtist.empty() && fDurationMs <= 0) {
		_PollPlayback();
		return;
	}

	int32 progressMs = fProgressMs;
	if (fIsPlaying && fLastPlaybackSyncUs > 0) {
		bigtime_t elapsedUs = system_time() - fLastPlaybackSyncUs;
		if (elapsedUs > 0)
			progressMs += (int32)(elapsedUs / 1000LL);
		if (fDurationMs > 0 && progressMs > fDurationMs)
			progressMs = fDurationMs;
	}

	BMessage stateMsg(MSG_REPLICANT_STATE);
	_FillReplicantStateMessage(stateMsg, progressMs);
	be_app->PostMessage(&stateMsg);
}


void
PlayerWindow::_FillReplicantStateMessage(BMessage& stateMsg,
	int32 progressMs) const
{
	stateMsg.AddBool("is_playing",    fIsPlaying);
	stateMsg.AddInt32("progress_ms",  progressMs);
	stateMsg.AddInt32("duration_ms",  fDurationMs);
	stateMsg.AddString("title",        fCurrentTitle.c_str());
	stateMsg.AddString("artist",       fCurrentArtist.c_str());
	stateMsg.AddString("album_id",     fCurrentAlbumId.c_str());
	stateMsg.AddString("artist_id",    fCurrentArtistId.c_str());
	stateMsg.AddString("track_uri",    fCurrentTrackUri.c_str());
	stateMsg.AddString(kNowPlayingItemKindField, fCurrentItemKind.c_str());
	stateMsg.AddString(kNowPlayingPrimaryOpenUriField,
		fCurrentPrimaryOpenUri.c_str());
	stateMsg.AddString(kNowPlayingParentUriField, fCurrentParentUri.c_str());
	stateMsg.AddString(kNowPlayingParentKindField, fCurrentParentKind.c_str());
	stateMsg.AddString(kNowPlayingShowIdField, fCurrentShowId.c_str());
	stateMsg.AddString(kNowPlayingAudiobookIdField,
		fCurrentAudiobookId.c_str());
	stateMsg.AddString("repeat_state", fRepeatState.c_str());
	stateMsg.AddBool("shuffle_state",  fShuffleOn);
	if (fVolumePct >= 0)
		stateMsg.AddInt32("volume_percent", fVolumePct);
	stateMsg.AddString("artwork_url", fLastArtworkUrl.c_str());
}


void
PlayerWindow::_ShowAddTrackMenu(const std::string& trackUri,
	BPoint screenWhere, bool liked)
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;

	std::string context = liked ? "spotify:collection" : "";
	BMessenger self(this);
	auto showMenu = [self, api, trackUri, context, screenWhere]() {
		ShowTrackContextMenu(trackUri, context, screenWhere, self, api, true);
	};
	if (api->Playlists().GetCachedPlaylists().empty()) {
		api->Playlists().GetPlaylists([showMenu](bool, const nlohmann::json&) {
			showMenu();
		});
	} else {
		showMenu();
	}
}


void
PlayerWindow::_RemoveCurrentTrackFromLikedSongs(const std::string& trackUri)
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || SpotifyItemKindForUri(trackUri) != kSpotifyItemTrack)
		return;

	api->Library().RemoveSavedTrack(SpotifyItemIdForUri(trackUri), nullptr);
}


void
PlayerWindow::_ApplyPlaybackPollResult(BMessage* message)
{
	fPlaybackRequestPending = false;
	if (message->GetBool("poll_ok", false)) {
		fPendingLibrespotTrack.MakeEmpty();
		fHasPendingLibrespotTrack = false;
		fPlaybackPollFailures = 0;
		bool hadPlaybackState = fHasPlaybackState;
		bool hasItem = message->GetBool("has_item", false);
		bool retryStartupEmptyPoll =
			ShouldRetryStartupEmptyPlaybackPoll(hasItem, hadPlaybackState,
				system_time(), fStartupEmptyPlaybackRetryUntilUs);
		if (retryStartupEmptyPoll) {
			_SchedulePlaybackPoll(kPlaybackStartupPollInterval);
			return;
		}
		if (hasItem) {
			fHasPlaybackState = true;
			fStartupEmptyPlaybackRetryUntilUs = 0;
		} else {
			message->AddString("title", B_TRANSLATE("Nothing is playing"));
		}
		_ApplyPlaybackMessage(message);
		bigtime_t delay = message->GetBool("is_playing", false)
			? kPlaybackActivePollInterval : kPlaybackIdlePollInterval;
		_SchedulePlaybackPoll(delay);
	} else {
		fPlaybackPollFailures++;
		int32 retryAfter = message->GetInt32("retry_after", -1);
		int32 shift = std::min(fPlaybackPollFailures - 1, (int32)5);
		bigtime_t delay = retryAfter > 0
			? (bigtime_t)retryAfter * 1000000LL
			: kPlaybackStartupPollInterval << shift;
		bigtime_t limit = fHasPlaybackState
			? kPlaybackErrorPollLimit : kPlaybackStartupPollLimit;
		if (retryAfter <= 0 && delay > limit)
			delay = limit;
		_SchedulePlaybackPoll(delay);
	}
}


void
PlayerWindow::_ApplyAudiobookContextResult(BMessage* message)
{
	const char* sourceTrackUri = message->GetString("source_track_uri", "");
	if (!sourceTrackUri || fAudiobookContextRequestTrackUri != sourceTrackUri)
		return;
	fAudiobookContextRequestPending = false;
	fAudiobookContextRequestTrackUri.clear();
	if (!message->GetBool("ok", false) || fCurrentTrackUri != sourceTrackUri)
		return;

	const char* openUri = message->GetString(
		kNowPlayingPrimaryOpenUriField, "");
	const char* parentKind = message->GetString(
		kNowPlayingParentKindField, "");
	std::string openUriString = openUri ? openUri : "";
	if (SpotifyItemKindForUri(openUriString) != kSpotifyItemAudiobook
			|| !parentKind || strcmp(parentKind, "audiobook") != 0) {
		return;
	}

	message->AddBool("is_playing", fIsPlaying);
	message->AddInt32("progress_ms", fProgressMs);
	if (fVolumePct >= 0)
		message->AddInt32("volume_percent", fVolumePct);
	message->AddString("repeat_state", fRepeatState.c_str());
	message->AddBool("shuffle_state", fShuffleOn);
	_ApplyPlaybackMessage(message);
}


void
PlayerWindow::_ApplyQueuePrediction(BMessage* message)
{
	fQueueRequestPending = false;
	const char* trackUri = message->GetString("source_track_uri", "");
	if (trackUri && fCurrentTrackUri == trackUri
			&& message->GetBool("has_next", false)) {
		fPredictedNext = *message;
		fPredictedNext.RemoveName("has_next");
		fHasPredictedNext = true;
		fQueueTrackUri = trackUri;
		_HandlePlaybackTick();
	} else if (fIsPlaying && trackUri && fCurrentTrackUri != trackUri) {
		_FetchQueuePrediction();
	}
}


void
PlayerWindow::_ApplyVerifyPoll()
{
	delete fVerifyTimer;
	fVerifyTimer = nullptr;
	_PollPlayback();
}


void
PlayerWindow::_TogglePlayPause()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	bool wasPlaying = fIsPlaying;
	if (fPlayerBar)
		fPlayerBar->SetPlaying(!wasPlaying);
	fIsPlaying = !wasPlaying;
	fLastPlaybackSyncUs = system_time();
	if (wasPlaying)
		api->Playback().Pause(nullptr);
	else
		api->Playback().Play(nullptr);
}


void
PlayerWindow::_SkipNextTrack()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	api->Playback().Next(nullptr);
	if (fHasPredictedNext)
		_ApplyPredictedNext();
	else
		_ScheduleVerifyPoll(kVerifyPollDelay);
}


void
PlayerWindow::_SkipPreviousTrack()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	api->Playback().Previous(nullptr);
	fHasPredictedNext = false;
	fQueueTrackUri.clear();
	_ScheduleVerifyPoll(kVerifyPollDelay);
}


void
PlayerWindow::_SetVolumeFromMessage(BMessage* message)
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	int32 volume = 0;
	if (message->FindInt32("be:value", &volume) != B_OK)
		return;
	if (volume < 0)
		volume = 0;
	if (volume > 100)
		volume = 100;
	_SetVolumeOptimistically(volume);
	if (api)
		api->Playback().SetVolume((int)volume, nullptr);
}


void
PlayerWindow::_ToggleMute()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;

	if (_RestoreMutedVolumeIfNeeded(api))
		return;

	BMessenger self(this);
	api->Playback().GetPlaybackState([self](bool ok,
			const nlohmann::json& data) {
		BMessage toggle(kMsgApplyMuteToggle);
		toggle.AddBool("ok", ok);
		if (ok)
			_AddMuteToggleDeviceFields(toggle, data);
		self.SendMessage(&toggle);
	});
}


bool
PlayerWindow::_RestoreMutedVolumeIfNeeded(SpotifyApi* api)
{
	if (!fMutedByHaify || fVolumePct != 0 || !fHasLastNonZeroVolume)
		return false;

	int32 targetVolume = fLastNonZeroVolume;
	if (targetVolume <= 0)
		targetVolume = 50;
	if (targetVolume > 100)
		targetVolume = 100;

	_SetVolumeOptimistically(targetVolume);
	api->Playback().SetVolume((int)targetVolume, nullptr, fVolumeDeviceId);
	return true;
}


void
PlayerWindow::_ApplyMuteToggle(BMessage* message)
{
	if (!message->GetBool("ok", false)
			|| !message->GetBool("supports_volume", true)
			|| message->GetBool("is_restricted", false)) {
		return;
	}

	int32 currentVolume = -1;
	if (message->FindInt32("volume_percent", &currentVolume) != B_OK)
		return;
	if (currentVolume < 0)
		return;
	if (currentVolume > 100)
		currentVolume = 100;
	fVolumeDeviceId = message->GetString("device_id", "");

	int32 targetVolume = 0;
	if (currentVolume > 0) {
		fLastNonZeroVolume = currentVolume;
		fHasLastNonZeroVolume = true;
	} else {
		targetVolume = fHasLastNonZeroVolume ? fLastNonZeroVolume : 50;
	}
	if (targetVolume < 0)
		targetVolume = 0;
	if (targetVolume > 100)
		targetVolume = 100;

	_SetVolumeOptimistically(targetVolume);

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (api)
		api->Playback().SetVolume((int)targetVolume, nullptr,
			fVolumeDeviceId);
}


void
PlayerWindow::_ToggleShuffle()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || fCurrentParentKind == "audiobook")
		return;
	fShuffleOn = !fShuffleOn;
	if (fPlayerBar)
		fPlayerBar->SetShuffle(fShuffleOn);
	fHasPredictedNext = false;
	fQueueTrackUri.clear();
	api->Playback().SetShuffle(fShuffleOn, nullptr);
}


void
PlayerWindow::_ToggleRepeat()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || fCurrentParentKind == "audiobook")
		return;
	if (fRepeatState == "off")
		fRepeatState = "context";
	else if (fRepeatState == "context")
		fRepeatState = "track";
	else
		fRepeatState = "off";
	if (fPlayerBar)
		fPlayerBar->SetRepeat(fRepeatState.c_str());
	fHasPredictedNext = false;
	fQueueTrackUri.clear();
	api->Playback().SetRepeat(fRepeatState, nullptr);
}


void
PlayerWindow::_ToggleLibrespotAutostart()
{
	bool autostart = false;
	SettingsController::Update([&](HaifySettings& settings) {
		settings.librespotAlwaysStart = !settings.librespotAlwaysStart;
		autostart = settings.librespotAlwaysStart;
	});
	if (fAutostartItem)
		fAutostartItem->SetMarked(autostart);
}


void
PlayerWindow::_SeekFromMessage(BMessage* message)
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	int64 posUs = 0;
	if (!api || message->FindInt64("position", &posUs) != B_OK)
		return;
	bigtime_t now = system_time();
	fProgressMs = (int32)(posUs / 1000LL);
	fSeekTargetMs = fProgressMs;
	fSeekGuardUntilUs = now + kSeekGuard;
	fLastPlaybackSyncUs = now;
	if (fPlayerBar)
		fPlayerBar->SetPosition(posUs, (bigtime_t)fDurationMs * 1000LL);
	BMessage stateMsg(MSG_REPLICANT_STATE);
	_FillReplicantStateMessage(stateMsg, fProgressMs);
	be_app->PostMessage(&stateMsg);
	api->Playback().Seek((int)(posUs / 1000LL), nullptr);
	_ScheduleVerifyPoll(kVerifyPollDelay);
}


void
PlayerWindow::_ApplySeekBarColor(BMessage* message)
{
	rgb_color color = {
		(uint8)message->GetInt32("red", 0),
		(uint8)message->GetInt32("green", 120),
		(uint8)message->GetInt32("blue", 215),
		(uint8)message->GetInt32("alpha", 255)
	};
	if (fPlayerBar)
		fPlayerBar->SetSeekBarColor(
			message->GetBool("use_system", false), color);
}


void
PlayerWindow::_ApplyDroppedSeekBarColor(BMessage* message)
{
	const rgb_color* color = nullptr;
	ssize_t colorSize = 0;
	if (message->FindData("color", B_RGB_COLOR_TYPE,
			(const void**)&color, &colorSize) != B_OK
			|| colorSize != sizeof(rgb_color)) {
		return;
	}
	SettingsController::Update([&](HaifySettings& settings) {
		settings.seekBarUseSystemColor = false;
		settings.seekBarColorRed = color->red;
		settings.seekBarColorGreen = color->green;
		settings.seekBarColorBlue = color->blue;
		settings.seekBarColorAlpha = color->alpha;
	});
	BMessage changed(MSG_SEEKBAR_COLOR_CHANGED);
	changed.AddBool("use_system", false);
	changed.AddInt32("red", color->red);
	changed.AddInt32("green", color->green);
	changed.AddInt32("blue", color->blue);
	changed.AddInt32("alpha", color->alpha);
	be_app->PostMessage(&changed);
}


void
PlayerWindow::_SaveCurrentTrack()
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api || SpotifyItemKindForUri(fCurrentTrackUri) != kSpotifyItemTrack)
		return;
	api->Library().SaveTrack(SpotifyItemIdForUri(fCurrentTrackUri), nullptr);
}


void
PlayerWindow::_PrepareAddTrackMenu(BMessage* message)
{
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api)
		return;
	const char* trackUri = message->GetString("trackUri",
		fCurrentTrackUri.c_str());
	std::string uri = trackUri ? trackUri : "";
	std::string id = SpotifyItemIdForUri(uri);
	if (SpotifyItemKindForUri(uri) != kSpotifyItemTrack || id.empty())
		return;
	BPoint screenWhere;
	if (message->FindPoint("screen_where", &screenWhere) != B_OK)
		screenWhere = Frame().LeftTop();
	BMessenger self(this);
	api->Library().CheckSavedTracks(id, [self, uri, screenWhere](bool ok,
			const nlohmann::json& data) {
		bool liked = false;
		if (ok && data.is_array() && !data.empty()
				&& data[0].is_boolean()) {
			liked = data[0].get<bool>();
		}
		BMessage open('sAtM');
		open.AddString("trackUri", uri.c_str());
		open.AddPoint("screen_where", screenWhere);
		open.AddBool("liked", liked);
		self.SendMessage(&open);
	});
}


void
PlayerWindow::_ShowAddTrackMenuFromMessage(BMessage* message)
{
	const char* trackUri = message->GetString("trackUri", "");
	BPoint screenWhere;
	if (SpotifyItemKindForUri(trackUri ? trackUri : "")
			!= kSpotifyItemTrack)
		return;
	if (message->FindPoint("screen_where", &screenWhere) != B_OK)
		screenWhere = Frame().LeftTop();
	_ShowAddTrackMenu(trackUri, screenWhere,
		message->GetBool("liked", false));
}


void
PlayerWindow::_ApplyAuthStatus(BMessage* message)
{
	bool auth = false;
	message->FindBool("ok", &auth);
	if (fAuthItem) {
		fAuthItem->SetLabel(auth
			? B_TRANSLATE("Sign Out") : B_TRANSLATE("Sign In"));
		fAuthItem->SetMessage(new BMessage(auth ? 'sout' : MSG_INIT_AUTH));
	}
}


void
PlayerWindow::_ApplyDeviceList(BMessage* message)
{
	if (!fDeviceMenu)
		return;
	while (fDeviceMenu->CountItems() > 0)
		delete fDeviceMenu->RemoveItem((int32)0);

	int32 index = 0;
	const char* id;
	while (message->FindString("id", index, &id) == B_OK) {
		const char* name = message->FindString("name", index);
		const char* type = message->FindString("type", index);
		bool active = false;
		message->FindBool("active", index, &active);

		BMessage* deviceMessage = new BMessage('toDv');
		deviceMessage->AddString("id", id);
		std::string displayName = _SpotifyDeviceDisplayName(
			id ? id : "", name ? name : "", type ? type : "");
		BMenuItem* item = new BMenuItem(displayName.c_str(), deviceMessage);
		item->SetMarked(active);
		fDeviceMenu->AddItem(item);
		index++;
	}
	if (fDeviceMenu->CountItems() == 0)
		fDeviceMenu->AddItem(new BMenuItem(
			B_TRANSLATE("No devices found"), nullptr));
}


void
PlayerWindow::_TransferToDevice(BMessage* message)
{
	const char* id = nullptr;
	if (message->FindString("id", &id) != B_OK || !id)
		return;
	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (api)
		api->Playback().TransferPlayback(id, nullptr);
}


bool
PlayerWindow::_HandlePlaybackMessage(BMessage* message)
{
	switch (message->what) {
		case 'lddt':
		case 'poll':
			_PollPlayback();
			return true;

		case 'pbst':
			_ApplyPlaybackMessage(message);
			return true;

		case kMsgPlaybackPollResult:
			_ApplyPlaybackPollResult(message);
			return true;

		case kMsgAudiobookContextResult:
			_ApplyAudiobookContextResult(message);
			return true;

		case MSG_SYNC_REPLICANT_STATE:
			_PublishReplicantState();
			return true;

		case 'play':
			_PlayUri(message);
			return true;

		case 'qprd':
			_ApplyQueuePrediction(message);
			return true;

		case kMsgPlaybackTick:
			_HandlePlaybackTick();
			return true;

		case kMsgVerifyPoll:
			_ApplyVerifyPoll();
			return true;

		default:
			return false;
	}
}


bool
PlayerWindow::_HandleTransportMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_PLAY_PAUSE:
			_TogglePlayPause();
			return true;

		case MSG_NEXT_TRACK:
			_SkipNextTrack();
			return true;

		case MSG_PREV_TRACK:
			_SkipPreviousTrack();
			return true;

		case MSG_SET_VOLUME:
			_SetVolumeFromMessage(message);
			return true;

		case MSG_TOGGLE_MUTE:
			_ToggleMute();
			return true;

		case kMsgApplyMuteToggle:
			_ApplyMuteToggle(message);
			return true;

		case MSG_TOGGLE_SHUFFLE:
			_ToggleShuffle();
			return true;

		case MSG_TOGGLE_REPEAT:
			_ToggleRepeat();
			return true;

		case MSG_TOGGLE_LIBRESPOT_RUNNING:
			be_app->PostMessage(MSG_TOGGLE_LIBRESPOT_RUNNING);
			_UpdateLibrespotMenuItems();
			return true;

		case MSG_TOGGLE_LIBRESPOT_AUTOSTART:
			_ToggleLibrespotAutostart();
			return true;

		case MSG_SEEK_REQUEST:
			_SeekFromMessage(message);
			return true;

		default:
			return false;
	}
}


bool
PlayerWindow::_HandleInterfaceMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_SEEKBAR_COLOR_CHANGED:
			_ApplySeekBarColor(message);
			return true;

		case MSG_REPLICANT_APPEARANCE_CHANGED:
			if (fPlayerBar)
				BMessenger(fPlayerBar).SendMessage(message);
			return true;

		case MSG_SEEKBAR_COLOR_DROPPED:
			_ApplyDroppedSeekBarColor(message);
			return true;

		case MSG_QUIT_APP:
			be_app->PostMessage(B_QUIT_REQUESTED);
			return true;

		case MSG_SAVE_CURRENT_TRACK:
			_SaveCurrentTrack();
			return true;

		case MSG_SHOW_ADD_TRACK_MENU:
			_PrepareAddTrackMenu(message);
			return true;

		case 'sAtM':
			_ShowAddTrackMenuFromMessage(message);
			return true;

		case 'remL':
		{
			const char* trackUri = message->GetString("trackUri", "");
			if (trackUri && trackUri[0])
				_RemoveCurrentTrackFromLikedSongs(trackUri);
			return true;
		}

		default:
			return false;
	}
}


bool
PlayerWindow::_ForwardAppMessage(BMessage* message)
{
	switch (message->what) {
		case MSG_OPEN_BROWSER:
		case MSG_OPEN_PLAYLIST:
		case MSG_OPEN_QUEUE:
		case MSG_OPEN_SEARCH:
		case MSG_OPEN_ARTWORK:
		case MSG_OPEN_SETTINGS:
		case MSG_SHOW_PLAYER_WINDOW:
		case MSG_SHOW_ARTIST:
		case MSG_SHOW_ALBUM:
		case MSG_INIT_AUTH:
		case 'open':
			be_app->PostMessage(message);
			return true;

		case 'sout':
			be_app->PostMessage('sout');
			return true;

		default:
			return false;
	}
}


bool
PlayerWindow::_HandleAccountDeviceMessage(BMessage* message)
{
	switch (message->what) {
		case 'aust':
			_ApplyAuthStatus(message);
			return true;

		case 'dEvL':
			_ApplyDeviceList(message);
			return true;

		case 'toDv':
			_TransferToDevice(message);
			return true;

		case B_ABOUT_REQUESTED:
			_ShowAboutWindow();
			return true;

		default:
			return false;
	}
}


void
PlayerWindow::MessageReceived(BMessage* message)
{
	if (_HandlePlaybackMessage(message) || _HandleTransportMessage(message)
			|| _HandleInterfaceMessage(message)
			|| _ForwardAppMessage(message)
			|| _HandleAccountDeviceMessage(message)) {
		return;
	}

	BWindow::MessageReceived(message);
}


void
PlayerWindow::_ShowAboutWindow()
{
	BAboutWindow* about = new BAboutWindow(HAIFY_APP_NAME, HAIFY_MIME_SIG);
	about->AddCopyright(2026, "Daniel Weber");
	about->AddDescription(
		"A Spotify WebAPI client for Haiku.\n\n"
		"A gentle green\n"
		"Melodies bloom like spring leaves\n"
		"Time fades away\n\n"
		"Licensed under the MIT License.");
	about->Show();
}


void
PlayerWindow::MenusBeginning()
{
	_UpdateLibrespotMenuItems();

	if (!fDeviceMenu) return;

	App* app = (App*)be_app;
	SpotifyApi* api = app->GetApi();
	if (!api) {
		while (fDeviceMenu->CountItems() > 0)
			delete fDeviceMenu->RemoveItem((int32)0);
		fDeviceMenu->AddItem(new BMenuItem(
			B_TRANSLATE("Not signed in"), nullptr));
		return;
	}

	BMessenger self(this);
	api->Playback().GetDevices([self](bool ok, const nlohmann::json& data) {
		BMessage* msg = new BMessage('dEvL');
		if (ok && data.contains("devices")) {
			for (const auto& d : data["devices"]) {
				msg->AddString("id",     d.value("id",   "").c_str());
				msg->AddString("name",   d.value("name", "").c_str());
				msg->AddString("type",   d.value("type", "").c_str());
				msg->AddBool  ("active", d.value("is_active", false));
			}
		}
		self.SendMessage(msg);
		delete msg;
	});
}


void
PlayerWindow::_UpdateLibrespotMenuItems()
{
	App* app = dynamic_cast<App*>(be_app);
	if (fLibrespotToggleItem) {
		bool running = app && app->IsLibrespotRunning();
		fLibrespotToggleItem->SetLabel(running
			? B_TRANSLATE("Stop librespot")
			: B_TRANSLATE("Start librespot"));
	}

	if (fAutostartItem)
		fAutostartItem->SetMarked(
			SettingsController::Load().librespotAlwaysStart);
}


void
PlayerWindow::FrameResized(float width, float height)
{
	BWindow::FrameResized(width, height);
	_ApplySizeLimits();
}


void
PlayerWindow::_InitMenu()
{
	fMenuBar = new BMenuBar("MenuBar");

	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	App* app = dynamic_cast<App*>(be_app);
	HaifySettings settings = SettingsController::Load();
	bool hasStoredSession = !settings.refreshToken.empty();
	bool signedIn = (app && app->GetApi()) || hasStoredSession;
	fAuthItem = new BMenuItem(signedIn
		? B_TRANSLATE("Sign Out") : B_TRANSLATE("Sign In"),
		new BMessage(signedIn ? 'sout' : MSG_INIT_AUTH));
	fileMenu->AddItem(fAuthItem);
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Settings" B_UTF8_ELLIPSIS),
		new BMessage(MSG_OPEN_SETTINGS)));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("About Haify" B_UTF8_ELLIPSIS),
		new BMessage(B_ABOUT_REQUESTED)));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(MSG_QUIT_APP), 'Q'));
	fMenuBar->AddItem(fileMenu);

	fDeviceMenu = new BMenu(B_TRANSLATE("Device"));
	fDeviceMenu->AddItem(new BMenuItem(B_TRANSLATE("Loading" B_UTF8_ELLIPSIS), nullptr));
	fMenuBar->AddItem(fDeviceMenu);

	BMenu* playMenu = new BMenu(B_TRANSLATE("Playback"));
	fLibrespotToggleItem = new BMenuItem(
		B_TRANSLATE("Start librespot"),
		new BMessage(MSG_TOGGLE_LIBRESPOT_RUNNING));
	playMenu->AddItem(fLibrespotToggleItem);
	fAutostartItem = new BMenuItem(
		B_TRANSLATE("Autostart librespot"),
		new BMessage(MSG_TOGGLE_LIBRESPOT_AUTOSTART));
	fAutostartItem->SetMarked(SettingsController::Load().librespotAlwaysStart);
	playMenu->AddItem(fAutostartItem);
	_UpdateLibrespotMenuItems();
	playMenu->AddSeparatorItem();
	playMenu->AddItem(new BMenuItem(B_TRANSLATE("Play / Pause"),
		new BMessage(MSG_PLAY_PAUSE), ' '));
	playMenu->AddItem(new BMenuItem(B_TRANSLATE("Next Track"),
		new BMessage(MSG_NEXT_TRACK), B_RIGHT_ARROW));
	playMenu->AddItem(new BMenuItem(B_TRANSLATE("Previous Track"),
		new BMessage(MSG_PREV_TRACK), B_LEFT_ARROW));
	fMenuBar->AddItem(playMenu);

	BMenu* windowMenu = new BMenu(B_TRANSLATE("Window"));
	windowMenu->AddItem(new BMenuItem(B_TRANSLATE("Artwork"),
		new BMessage(MSG_OPEN_ARTWORK), 'A'));
	windowMenu->AddItem(new BMenuItem(B_TRANSLATE("Discover"),
		new BMessage(MSG_OPEN_BROWSER), 'N'));
	windowMenu->AddItem(new BMenuItem(B_TRANSLATE("Search"),
		new BMessage(MSG_OPEN_SEARCH), 'F'));
	windowMenu->AddItem(new BMenuItem(B_TRANSLATE("Queue"),
		new BMessage(MSG_OPEN_QUEUE), 'U'));
	fMenuBar->AddItem(windowMenu);

}


void
PlayerWindow::_InitLayout()
{
	fPlayerBar = new PlayerBarView();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, 0, 0, 0)
		.Add(fMenuBar)
		.Add(fPlayerBar)
		.End();

	BMessage tickMsg(kMsgPlaybackTick);
	fPlaybackTimer = new BMessageRunner(BMessenger(this), &tickMsg,
		kLocalPlaybackInterval);

	fStartupEmptyPlaybackRetryUntilUs = system_time()
		+ kPlaybackStartupEmptyItemRetryLimit;
	_SchedulePlaybackPoll(0);

	_ApplySizeLimits();
}

void
PlayerWindow::_ApplySizeLimits()
{
	float menuHeight = fMenuBar ? fMenuBar->MinSize().height : 0.0f;
	float playerHeight = fPlayerBar ? fPlayerBar->MinSize().height : 48.0f;
	float minHeight = menuHeight + playerHeight;
	SetSizeLimits(kMinPlayerWindowWidth, 100000.0f, minHeight, minHeight);

	float width = Frame().Width();
	if (width < kMinPlayerWindowWidth)
		width = kMinPlayerWindowWidth;
	if (Frame().Width() != width || Frame().Height() != minHeight)
		ResizeTo(width, minHeight);
}

PlayerWindow::~PlayerWindow()
{
	delete fPollTimer;
	delete fPlaybackTimer;
	delete fVerifyTimer;

	BRect frame = Frame();
	SettingsController::Update([&](HaifySettings& s) {
		s.playerWindowX = frame.left;
		s.playerWindowY = frame.top;
		s.playerWindowW = frame.Width();
		s.playerWindowH = frame.Height();
	});
}
