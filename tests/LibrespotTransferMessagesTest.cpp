#include "playback/LibrespotTransferMessages.h"
#include "Messages.h"

#include <cassert>
#include <iostream>

#ifdef NDEBUG
#error Message tests require assertions; compile without NDEBUG.
#endif

static LibrespotTransferResult
Sample()
{
	LibrespotTransferResult result;
	result.request = {10, 1, LibrespotTransferStep::FindDevice, "Haify", ""};
	result.ok = true;
	result.foundDeviceId = "local";
	return result;
}

static void
ExpectRejected(const BMessage& message)
{
	LibrespotTransferResult result = Sample();
	result.request.generation = 999;
	assert(!ReadLibrespotTransferResultMessage(message, result));
	assert(result.request.generation == 999 && result.foundDeviceId == "local");
}

static void
DuplicateField(BMessage& message, const char* field, type_code type)
{
	status_t status = B_ERROR;
	switch (type) {
		case B_INT64_TYPE: status = message.AddInt64(field, 10); break;
		case B_INT32_TYPE: status = message.AddInt32(field, 1); break;
		case B_BOOL_TYPE: status = message.AddBool(field, true); break;
		case B_STRING_TYPE: status = message.AddString(field, "duplicate"); break;
		default: break;
	}
	assert(status == B_OK);
}

static void
TestRequiredFields()
{
	BMessage valid = MakeLibrespotTransferResultMessage(Sample());
	const char* fields[] = {MessageFields::PlaybackGeneration, "sequence", "step",
		"device_name", "device_id", "ok", "response_valid", "status", "retry_after",
		"found_device_id", "should_transfer"};
	for (const char* field : fields) {
		BMessage missing(valid);
		assert(missing.RemoveName(field) == B_OK);
		ExpectRejected(missing);
		assert(missing.AddFloat(field, 1.0f) == B_OK);
		ExpectRejected(missing);
		BMessage duplicate(valid);
		type_code type = 0;
		int32 count = 0;
		assert(valid.GetInfo(field, &type, &count) == B_OK);
		DuplicateField(duplicate, field, type);
		ExpectRejected(duplicate);
	}
	valid.what = MSG_PLAY_URI;
	ExpectRejected(valid);
	ExpectRejected(BMessage(MSG_LIBRESPOT_TRANSFER_RESULT)); // Old bare success.
}

static void
TestInvalidValues()
{
	for (const char* field : {"sequence", "step"}) {
		BMessage message = MakeLibrespotTransferResultMessage(Sample());
		message.ReplaceInt32(field, 0);
		ExpectRejected(message);
	}
	BMessage message = MakeLibrespotTransferResultMessage(Sample());
	message.ReplaceInt32("step", 99);
	ExpectRejected(message);
	message = MakeLibrespotTransferResultMessage(Sample());
	message.ReplaceInt64(MessageFields::PlaybackGeneration, -1);
	ExpectRejected(message);
	for (const char* field : {"status", "retry_after"}) {
		message = MakeLibrespotTransferResultMessage(Sample());
		message.ReplaceInt32(field, -2);
		ExpectRejected(message);
	}
	for (const char* field : {"ok", "response_valid"}) {
		message = MakeLibrespotTransferResultMessage(Sample());
		message.ReplaceBool(field, false); // Failure cannot claim a found device.
		ExpectRejected(message);
	}
	message = MakeLibrespotTransferResultMessage(Sample());
	message.ReplaceBool("should_transfer", true); // Wrong step.
	ExpectRejected(message);
}

static LibrespotTransferResult
RoundTrip(const LibrespotTransferResult& result)
{
	LibrespotTransferResult parsed;
	assert(ReadLibrespotTransferResultMessage(MakeLibrespotTransferResultMessage(result), parsed));
	assert(parsed.request.generation == result.request.generation);
	assert(parsed.request.sequence == result.request.sequence && parsed.request.step == result.request.step);
	assert(parsed.request.deviceName == result.request.deviceName && parsed.request.deviceId == result.request.deviceId);
	assert(parsed.ok == result.ok && parsed.responseValid == result.responseValid);
	assert(parsed.status == result.status && parsed.retryAfter == result.retryAfter);
	assert(parsed.foundDeviceId == result.foundDeviceId && parsed.shouldTransfer == result.shouldTransfer);
	return parsed;
}

static void
TestSequenceAndFailures()
{
	LibrespotTransferController controller;
	controller.Begin(10, kLibrespotTransferIfIdle);
	LibrespotTransferResult found = Sample();
	found.request = controller.Poll("Haify");
	auto update = controller.Apply(RoundTrip(found));
	assert(update.accepted && !update.ready);
	LibrespotTransferResult idle;
	idle.request = update.next;
	idle.ok = true;
	idle.shouldTransfer = true;
	update = controller.Apply(RoundTrip(idle));
	assert(update.accepted && !update.ready);
	LibrespotTransferResult transferred;
	transferred.request = update.next;
	transferred.ok = true;
	transferred.status = 204;
	assert(controller.Apply(RoundTrip(transferred)).ready);
	controller.Begin(20);
	assert(!controller.Apply(RoundTrip(transferred)).accepted);
	transferred.ok = false;
	transferred.status = 429;
	transferred.retryAfter = 15;
	RoundTrip(transferred);
	found.ok = false;
	found.foundDeviceId.clear();
	found.status = 403;
	RoundTrip(found);
	idle.responseValid = false;
	idle.shouldTransfer = false;
	RoundTrip(idle);
}

static void
TestPoll()
{
	assert(ReadLibrespotTransferPoll(MakeLibrespotTransferPoll(10)) == 10);
	assert(ReadLibrespotTransferPoll(MakeLibrespotTransferPoll(0)) == 0);
	assert(ReadLibrespotTransferPoll(MakeLibrespotTransferPoll(-1)) == 0);
	BMessage message = MakeLibrespotTransferPoll(10);
	message.AddInt64(MessageFields::PlaybackGeneration, 20);
	assert(ReadLibrespotTransferPoll(message) == 0);
	message = MakeLibrespotTransferPoll(10);
	message.what = MSG_LIBRESPOT_TRANSFER_RESULT;
	assert(ReadLibrespotTransferPoll(message) == 0);
	assert(ReadLibrespotTransferPoll(BMessage(MSG_LIBRESPOT_TRANSFER_POLL)) == 0);
}

int
main()
{
	TestRequiredFields();
	TestInvalidValues();
	TestSequenceAndFailures();
	TestPoll();
	std::cout << "Librespot transfer message tests passed.\n";
}
