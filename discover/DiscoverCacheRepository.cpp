#include "DiscoverCacheRepository.h"
#include "DiscoverCacheWriteOrder.h"
#include "DiscoverMessages.h"
#include "SettingsController.h"

#include <Autolock.h>
#include <File.h>
#include <Locker.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <thread>
#include <unistd.h>

namespace {
BLocker sWriterLock("Haify discover cache writer");
DiscoverCacheWriteOrder sWrites;
std::map<std::string, nlohmann::json> sPendingDocuments;

std::string
CachePath(const std::string& account, bool createDirectory)
{
	std::string name = DiscoverCacheDocument::SafeAccountName(account);
	return name.empty() ? "" : SettingsController::CacheFilePath("discover",
		name + ".json", createDirectory);
}

status_t
ReadFile(const std::string& path, nlohmann::json& document)
{
	BFile file(path.c_str(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();
	off_t size = 0;
	status_t status = file.GetSize(&size);
	if (status != B_OK)
		return status;
	if (size <= 0 || size > 50LL * 1024LL * 1024LL)
		return B_BAD_DATA;
	std::string content((size_t)size, '\0');
	if (file.Read(&content[0], (size_t)size) != size)
		return B_IO_ERROR;
	document = nlohmann::json::parse(content, nullptr, false);
	return document.is_discarded() ? B_BAD_DATA : B_OK;
}

void
SendResult(const BMessenger& target, const DiscoverCacheReadResult& result)
{
	size_t begin = 0;
	do {
		size_t end = std::min(begin + size_t(50), result.rows.size());
		BMessage message = DiscoverMessages::CacheBatch(result, begin, end);
		if (target.SendMessage(&message) != B_OK)
			return;
		begin = end;
	} while (begin < result.rows.size());
}

status_t
WriteTemporary(const std::string& path, const nlohmann::json& document)
{
	std::string serialized = document.dump();
	BFile file(path.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();
	return file.Write(serialized.data(), serialized.size()) == (ssize_t)serialized.size()
		? B_OK : B_IO_ERROR;
}

status_t
WriteSnapshot(const std::string& path, uint64_t generation, nlohmann::json document)
{
	std::string temporary = path + ".part-" + std::to_string(generation);
	status_t status = B_ERROR;
	try {
		nlohmann::json existing;
		if (ReadFile(path, existing) == B_OK)
			DiscoverCacheDocument::MergeUnloadedTabs(document, existing);
		status = WriteTemporary(temporary, document);
	} catch (...) {
		status = B_BAD_DATA;
	}
	BAutolock lock(&sWriterLock);
	if (status == B_OK && sWrites.IsCurrent(path, generation)) {
		// Keep the old file intact on failure; rename and generation check are atomic
		// with respect to other writers and requests in this process.
		if (rename(temporary.c_str(), path.c_str()) != 0)
			status = errno;
		else
			sPendingDocuments.erase(path);
	}
	sWrites.Complete(path, generation);
	unlink(temporary.c_str());
	return status;
}

status_t
ReadCurrentDocument(const std::string& path, nlohmann::json& document)
{
	{
		BAutolock lock(&sWriterLock);
		auto pending = sPendingDocuments.find(path);
		if (pending != sPendingDocuments.end())
			document = pending->second;
	}
	nlohmann::json disk;
	status_t status = ReadFile(path, disk);
	if (document.is_object()) {
		if (status == B_OK)
			DiscoverCacheDocument::MergeUnloadedTabs(document, disk);
		return B_OK;
	}
	document = std::move(disk);
	return status;
}
} // namespace

void
DiscoverCacheRepository::LoadAsync(const BMessenger& target,
	const DiscoverCacheReadRequest& request)
{
	std::thread([target, request]() {
		DiscoverCacheReadResult result;
		result.request = request;
		try {
			nlohmann::json document;
			result.ioStatus = ReadCurrentDocument(CachePath(request.accountId, false), document);
			if (result.ioStatus == B_OK)
				result = DiscoverCacheDocument::ReadTab(document, request);
			else if (result.ioStatus == B_BAD_DATA)
				result.status = DiscoverCacheReadStatus::Invalid;
		} catch (...) {
			result.status = DiscoverCacheReadStatus::Invalid;
			result.ioStatus = B_BAD_DATA;
		}
		SendResult(target, result);
	}).detach();
}

void
DiscoverCacheRepository::WriteAsync(const DiscoverCacheSnapshot& snapshot)
{
	std::string path = CachePath(snapshot.accountId, true);
	if (path.empty())
		return;
	uint64_t generation;
	auto document = DiscoverCacheDocument::Encode(snapshot);
	{
		BAutolock lock(&sWriterLock);
		auto pending = sPendingDocuments.find(path);
		if (pending != sPendingDocuments.end())
			DiscoverCacheDocument::MergeUnloadedTabs(document, pending->second);
		sPendingDocuments[path] = document;
		generation = sWrites.Begin(path);
	}
	std::thread([path, generation, document = std::move(document)]() mutable {
		status_t status = WriteSnapshot(path, generation, std::move(document));
		if (status != B_OK)
			std::fprintf(stderr, "Haify: discover cache write failed (%ld)\n", (long)status);
	}).detach();
}
