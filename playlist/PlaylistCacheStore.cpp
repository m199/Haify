#include "PlaylistCacheStore.h"

#include "PlaylistCacheKeys.h"
#include "settings/SettingsController.h"

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Locker.h>
#include <Path.h>
#include <SupportDefs.h>

#include <cstdio>
#include <map>
#include <thread>
#include <unistd.h>

const char* const PlaylistCacheStore::kPlaylists = "playlists";
const char* const PlaylistCacheStore::kLibrary = "library";
const char* const PlaylistCacheStore::kShows = "shows";
const char* const PlaylistCacheStore::kLikedSongsId = "liked-songs";

namespace {

BLocker sStoreLock("Haify playlist cache store");
std::string sAccount;
// Path -> generation of the newest pending write. A missing entry means no
// write may publish that path any more (finished, superseded or cancelled).
std::map<std::string, uint64> sPendingWrites;
uint64 sNextWriteGeneration = 0;

const char* const kKinds[] = {
	PlaylistCacheStore::kPlaylists,
	PlaylistCacheStore::kLibrary,
	PlaylistCacheStore::kShows
};


bool
HasJsonExtension(const char* name)
{
	std::string value(name);
	return value.size() > 5
		&& value.compare(value.size() - 5, 5, ".json") == 0;
}


// Removes regular files in one directory; subdirectories are left alone so a
// legacy sweep of <kind>/ never enters the per-account directories.
void
RemoveFilesIn(const std::string& directoryPath, bool jsonOnly)
{
	if (directoryPath.empty())
		return;
	BDirectory directory(directoryPath.c_str());
	if (directory.InitCheck() != B_OK)
		return;
	BEntry entry;
	char name[B_FILE_NAME_LENGTH];
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsFile() || entry.GetName(name) != B_OK)
			continue;
		if (!jsonOnly || HasJsonExtension(name))
			entry.Remove();
	}
}

}


void
PlaylistCacheStore::SetAccount(const std::string& accountId)
{
	BAutolock lock(&sStoreLock);
	if (sAccount == accountId)
		return;
	sAccount = accountId;
	// A pending write was computed for the previous identity.
	sPendingWrites.clear();
}


std::string
PlaylistCacheStore::Account()
{
	BAutolock lock(&sStoreLock);
	return sAccount;
}


std::string
PlaylistCacheStore::FilePath(const std::string& kind, const std::string& id,
	bool createDirectory)
{
	if (!PlaylistCacheKeys::IsSafeId(id))
		return "";
	std::string directory = PlaylistCacheKeys::AccountDirectory(kind,
		Account());
	if (directory.empty())
		return "";
	return SettingsController::CacheFilePath(directory, id + ".json",
		createDirectory);
}


void
PlaylistCacheStore::WriteAsync(const std::string& path, nlohmann::json data)
{
	if (path.empty())
		return;
	uint64 generation;
	{
		BAutolock lock(&sStoreLock);
		generation = ++sNextWriteGeneration;
		sPendingWrites[path] = generation;
	}
	std::thread([path, generation, data = std::move(data)]() {
		std::string serialized = data.dump();
		std::string temporary = path + ".part-" + std::to_string(generation);
		BFile file(temporary.c_str(), B_WRITE_ONLY | B_CREATE_FILE
			| B_ERASE_FILE);
		bool written = file.InitCheck() == B_OK
			&& file.Write(serialized.data(), serialized.size())
				== (ssize_t)serialized.size();
		file.Unset();

		// Publish under the lock so Remove() cannot interleave between the
		// generation check and the rename.
		BAutolock lock(&sStoreLock);
		auto pending = sPendingWrites.find(path);
		bool current = pending != sPendingWrites.end()
			&& pending->second == generation;
		if (current)
			sPendingWrites.erase(pending);
		if (written && current && rename(temporary.c_str(), path.c_str()) == 0)
			return;
		unlink(temporary.c_str());
	}).detach();
}


void
PlaylistCacheStore::Remove(const std::string& path)
{
	if (path.empty())
		return;
	BAutolock lock(&sStoreLock);
	sPendingWrites.erase(path);
	unlink(path.c_str());
}


void
PlaylistCacheStore::Remove(const std::string& kind, const std::string& id)
{
	Remove(FilePath(kind, id, false));
}


void
PlaylistCacheStore::RemoveAccountFiles(const std::string& accountId)
{
	if (accountId.empty())
		return;
	BAutolock lock(&sStoreLock);
	if (accountId == sAccount)
		sPendingWrites.clear();
	for (const char* kind : kKinds) {
		std::string directory = SettingsController::CachePath(
			PlaylistCacheKeys::AccountDirectory(kind, accountId), false);
		RemoveFilesIn(directory, false);
		if (!directory.empty())
			rmdir(directory.c_str());
	}
}


void
PlaylistCacheStore::RemoveLegacyUnscopedFiles()
{
	for (const char* kind : kKinds)
		RemoveFilesIn(SettingsController::CachePath(kind, false), true);
}
