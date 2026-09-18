#pragma once

#include "LibrespotTransferController.h"
#include <Message.h>

// In-process, single-value fields only. Readers reject incomplete/old payloads
// and leave the destination untouched. See docs/message-contracts.md.
BMessage MakeLibrespotTransferPoll(int64_t generation);
int64_t ReadLibrespotTransferPoll(const BMessage& message);
BMessage MakeLibrespotTransferResultMessage(const LibrespotTransferResult& result);
bool ReadLibrespotTransferResultMessage(const BMessage& message,
	LibrespotTransferResult& result);
