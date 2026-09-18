#include "LibrespotTransferMessages.h"
#include "Messages.h"

#include <utility>

namespace {
bool
SingleField(const BMessage& message, const char* field, type_code expected)
{
	type_code type = 0;
	int32 count = 0;
	return message.GetInfo(field, &type, &count) == B_OK
		&& type == expected && count == 1;
}

bool
ResultFieldsValid(const BMessage& message)
{
	const struct { const char* name = nullptr; type_code type = 0; } fields[] = {
		{MessageFields::PlaybackGeneration, B_INT64_TYPE},
		{"sequence", B_INT32_TYPE}, {"step", B_INT32_TYPE},
		{"device_name", B_STRING_TYPE}, {"device_id", B_STRING_TYPE},
		{"ok", B_BOOL_TYPE}, {"response_valid", B_BOOL_TYPE},
		{"status", B_INT32_TYPE}, {"retry_after", B_INT32_TYPE},
		{"found_device_id", B_STRING_TYPE}, {"should_transfer", B_BOOL_TYPE}
	};
	for (const auto& field : fields) {
		if (!SingleField(message, field.name, field.type))
			return false;
	}
	return true;
}

bool
ResultValuesValid(const LibrespotTransferResult& result)
{
	if (!ValidLibrespotTransferRequest(result.request)
			|| result.status < -1 || result.retryAfter < -1)
		return false;
	if ((!result.ok || !result.responseValid)
			&& (!result.foundDeviceId.empty() || result.shouldTransfer))
		return false;
	if (result.request.step != LibrespotTransferStep::FindDevice
			&& !result.foundDeviceId.empty())
		return false;
	return result.request.step == LibrespotTransferStep::InspectPlayback
		|| !result.shouldTransfer;
}
}

BMessage
MakeLibrespotTransferPoll(int64_t generation)
{
	BMessage message(MSG_LIBRESPOT_TRANSFER_POLL);
	message.AddInt64(MessageFields::PlaybackGeneration, generation);
	return message;
}

int64_t
ReadLibrespotTransferPoll(const BMessage& message)
{
	if (message.what != MSG_LIBRESPOT_TRANSFER_POLL
			|| !SingleField(message, MessageFields::PlaybackGeneration, B_INT64_TYPE))
		return 0;
	int64 generation = message.GetInt64(MessageFields::PlaybackGeneration, 0);
	return generation > 0 ? generation : 0;
}

BMessage
MakeLibrespotTransferResultMessage(const LibrespotTransferResult& result)
{
	BMessage message(MSG_LIBRESPOT_TRANSFER_RESULT);
	message.AddInt64(MessageFields::PlaybackGeneration, result.request.generation);
	message.AddInt32("sequence", result.request.sequence);
	message.AddInt32("step", static_cast<int32>(result.request.step));
	message.AddString("device_name", result.request.deviceName.c_str());
	message.AddString("device_id", result.request.deviceId.c_str());
	message.AddBool("ok", result.ok);
	message.AddBool("response_valid", result.responseValid);
	message.AddInt32("status", result.status);
	message.AddInt32("retry_after", result.retryAfter);
	message.AddString("found_device_id", result.foundDeviceId.c_str());
	message.AddBool("should_transfer", result.shouldTransfer);
	return message;
}

bool
ReadLibrespotTransferResultMessage(const BMessage& message,
	LibrespotTransferResult& result)
{
	if (message.what != MSG_LIBRESPOT_TRANSFER_RESULT || !ResultFieldsValid(message))
		return false;
	LibrespotTransferResult parsed;
	parsed.request = {message.GetInt64(MessageFields::PlaybackGeneration, 0),
		message.GetInt32("sequence", 0),
		static_cast<LibrespotTransferStep>(message.GetInt32("step", 0)),
		message.GetString("device_name", ""), message.GetString("device_id", "")};
	parsed.ok = message.GetBool("ok", false);
	parsed.responseValid = message.GetBool("response_valid", false);
	parsed.status = message.GetInt32("status", -1);
	parsed.retryAfter = message.GetInt32("retry_after", -1);
	parsed.foundDeviceId = message.GetString("found_device_id", "");
	parsed.shouldTransfer = message.GetBool("should_transfer", false);
	if (!ResultValuesValid(parsed))
		return false;
	result = std::move(parsed);
	return true;
}
