#include "ImageCache.h"
#include "HttpClient.h"
#include "SettingsController.h"
#include <TranslationUtils.h>
#include <Autolock.h>
#include <DataIO.h>
#include <Path.h>
#include <File.h>
#include <Directory.h>
#include <Entry.h>
#include <Locker.h>
#include <algorithm>
#include <chrono>
#include <stdio.h>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

std::map<std::string, BBitmap*> ImageCache::sCache;
std::map<std::string, uint64> ImageCache::sAccessOrder;
uint64 ImageCache::sNextAccessOrder = 0;
std::map<std::string, std::vector<ImageCallback>> ImageCache::sPending;
std::map<std::string, uint64> ImageCache::sGenerations;
static int64 sMaxCacheBytes = 500LL * 1024LL * 1024LL;
static BLocker sImageCacheLock("Haify image cache");
static const size_t kMaxMemoryCacheEntries = 96;
static const int32 kMaxDownloadAttempts = 3;

struct CacheFileInfo {
    std::string path;
    off_t size;
    time_t modified;
};

static std::string GetCacheDirectoryPath(bool create) {
    return SettingsController::CachePath("images", create);
}

std::string ImageCache::CacheDirectoryPath()
{
    return GetCacheDirectoryPath(false);
}

static std::string GetCachePath(const std::string& url) {
    std::string dirPath = GetCacheDirectoryPath(true);
    if (dirPath.empty())
        return "";
    BPath path(dirPath.c_str());

    std::string filename;
    size_t lastSlash = url.find_last_of('/');
    filename = (lastSlash != std::string::npos) ? url.substr(lastSlash + 1) : url;

    for (char& c : filename) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    if (filename.length() > 60) filename = filename.substr(filename.length() - 60);
    filename += ".jpg";

    path.Append(filename.c_str());
    return path.Path();
}

status_t ImageCache::Clear(int32* filesRemoved)
{
    if (filesRemoved)
        *filesRemoved = 0;

    std::vector<ImageCallback> cancelledCallbacks;
    {
        BAutolock lock(&sImageCacheLock);
        for (auto& entry : sCache)
            delete entry.second;
        sCache.clear();
        sAccessOrder.clear();
        for (auto& pending : sPending) {
            ++sGenerations[pending.first];
            cancelledCallbacks.insert(cancelledCallbacks.end(),
                pending.second.begin(), pending.second.end());
        }
        sPending.clear();
    }
    for (auto& callback : cancelledCallbacks)
        callback(nullptr);

    std::string dirPath = GetCacheDirectoryPath(false);
    if (dirPath.empty())
        return B_ERROR;

    BDirectory directory(dirPath.c_str());
    status_t status = directory.InitCheck();
    if (status == B_ENTRY_NOT_FOUND)
        return B_OK;
    if (status != B_OK)
        return status;

    BEntry entry;
    while (directory.GetNextEntry(&entry) == B_OK) {
        if (entry.IsFile() && entry.Remove() == B_OK && filesRemoved)
            (*filesRemoved)++;
    }

    return B_OK;
}

void ImageCache::SetMaxCacheBytes(int64 maxBytes)
{
    int64 limit = maxBytes < 0 ? 0 : maxBytes;
    {
        BAutolock lock(&sImageCacheLock);
        sMaxCacheBytes = limit;
    }
    if (limit > 0)
        PruneToSize(limit);
}

int64 ImageCache::MaxCacheBytes()
{
    BAutolock lock(&sImageCacheLock);
    return sMaxCacheBytes;
}

static status_t CollectCacheFiles(std::vector<CacheFileInfo>& files,
    int64* totalSize)
{
    if (totalSize)
        *totalSize = 0;

    std::string dirPath = GetCacheDirectoryPath(false);
    if (dirPath.empty())
        return B_ERROR;

    BDirectory directory(dirPath.c_str());
    status_t status = directory.InitCheck();
    if (status == B_ENTRY_NOT_FOUND)
        return B_OK;
    if (status != B_OK)
        return status;

    BEntry entry;
    while (directory.GetNextEntry(&entry) == B_OK) {
        if (!entry.IsFile())
            continue;

        BPath path;
        if (entry.GetPath(&path) != B_OK)
            continue;

        struct stat st;
        if (stat(path.Path(), &st) != 0)
            continue;

        files.push_back({path.Path(), st.st_size, st.st_mtime});
        if (totalSize)
            *totalSize += st.st_size;
    }

    return B_OK;
}

int64 ImageCache::CacheSize()
{
    std::vector<CacheFileInfo> files;
    int64 totalSize = 0;
    if (CollectCacheFiles(files, &totalSize) != B_OK)
        return 0;
    return totalSize;
}

status_t ImageCache::PruneToSize(int64 maxBytes)
{
    if (maxBytes <= 0)
        return B_OK;

    std::vector<CacheFileInfo> files;
    int64 totalSize = 0;
    status_t status = CollectCacheFiles(files, &totalSize);
    if (status != B_OK)
        return status;

    std::sort(files.begin(), files.end(),
        [](const CacheFileInfo& a, const CacheFileInfo& b) {
            return a.modified < b.modified;
        });

    for (const auto& file : files) {
        if (totalSize <= maxBytes)
            break;
        if (unlink(file.path.c_str()) == 0)
            totalSize -= file.size;
    }

    return B_OK;
}

void ImageCache::GetImage(const std::string& url, ImageCallback callback)
{
    if (url.empty()) {
        if (callback)
            callback(nullptr);
        return;
    }

    BBitmap* callbackBitmap = nullptr;
    bool cacheHit = false;
    uint64 generation = 0;
    {
        BAutolock lock(&sImageCacheLock);
        auto cached = sCache.find(url);
        if (cached != sCache.end()) {
            cacheHit = true;
            sAccessOrder[url] = ++sNextAccessOrder;
            callbackBitmap = new BBitmap(cached->second);
            if (!callbackBitmap->IsValid()) {
                delete callbackBitmap;
                callbackBitmap = nullptr;
            }
        }
        else {
            auto pending = sPending.find(url);
            if (pending != sPending.end()) {
                if (callback)
                    pending->second.push_back(callback);
                return;
            }
            std::vector<ImageCallback>& callbacks = sPending[url];
            if (callback)
                callbacks.push_back(callback);
            generation = sGenerations[url];
        }
    }

    if (cacheHit) {
        if (callback)
            callback(callbackBitmap);
        else
            delete callbackBitmap;
        return;
    }

    StartLoad(url, generation, true);
}

void ImageCache::ReloadImage(const std::string& url, ImageCallback callback)
{
    if (url.empty()) {
        if (callback)
            callback(nullptr);
        return;
    }

    uint64 generation;
    {
        BAutolock lock(&sImageCacheLock);
        generation = ++sGenerations[url];
        auto cached = sCache.find(url);
        if (cached != sCache.end()) {
            delete cached->second;
            sCache.erase(cached);
            sAccessOrder.erase(url);
        }
        if (callback)
            sPending[url].push_back(callback);
        else
            sPending[url];
    }
    StartLoad(url, generation, false);
}

static bool
LooksLikeCompleteImage(const std::string& body)
{
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(
        body.data());
    size_t size = body.size();
    if (size >= 2 && bytes[0] == 0xff && bytes[1] == 0xd8)
        return size >= 4 && bytes[size - 2] == 0xff && bytes[size - 1] == 0xd9;
    if (size >= 8 && bytes[0] == 0x89 && bytes[1] == 'P'
            && bytes[2] == 'N' && bytes[3] == 'G') {
        return size >= 12 && bytes[size - 8] == 'I' && bytes[size - 7] == 'E'
            && bytes[size - 6] == 'N' && bytes[size - 5] == 'D';
    }
    return true;
}

void ImageCache::StartLoad(const std::string& url, uint64 generation,
    bool allowDiskCache)
{
    std::thread([url, generation, allowDiskCache]() {
        if (!ImageCache::IsCurrentGeneration(url, generation))
            return;

        std::string cacheFile = GetCachePath(url);
        if (!allowDiskCache && !cacheFile.empty())
            unlink(cacheFile.c_str());

        if (allowDiskCache && !cacheFile.empty()) {
            BBitmap* diskBitmap = BTranslationUtils::GetBitmap(
                cacheFile.c_str());
            if (diskBitmap && !diskBitmap->IsValid()) {
                delete diskBitmap;
                diskBitmap = nullptr;
            }
            if (diskBitmap) {
                utime(cacheFile.c_str(), nullptr);
                ImageCache::FinishLoad(url, generation, diskBitmap);
                return;
            }
            // Do not repeatedly decode a known-broken cache entry.
            unlink(cacheFile.c_str());
        }

        ImageCache::StartNetworkLoad(url, generation, cacheFile, 0);
    }).detach();
}

bool ImageCache::IsCurrentGeneration(const std::string& url,
    uint64 generation)
{
    BAutolock lock(&sImageCacheLock);
    auto current = sGenerations.find(url);
    return current != sGenerations.end() && current->second == generation
        && sPending.find(url) != sPending.end();
}

static bool
IsTransientImageFailure(int statusCode)
{
    return statusCode < 0 || statusCode == 408 || statusCode == 425
        || statusCode == 429 || statusCode >= 500;
}

void ImageCache::StartNetworkLoad(const std::string& url, uint64 generation,
    const std::string& cacheFile, int32 attempt)
{
    if (!IsCurrentGeneration(url, generation))
        return;

    Headers headers;
    HttpClient::Get(url, headers,
        [url, cacheFile, generation, attempt](const HttpResponse& response) {
            if (!ImageCache::IsCurrentGeneration(url, generation))
                return;

            BBitmap* bitmap = nullptr;
            bool successfulResponse = response.statusCode >= 200
                && response.statusCode < 300;
            bool completeBody = !response.body.empty()
                && LooksLikeCompleteImage(response.body);
            if (successfulResponse && completeBody) {
                if (!cacheFile.empty()) {
                    std::string temporary = cacheFile + ".part-"
                        + std::to_string(generation);
                    BFile file(temporary.c_str(), B_WRITE_ONLY
                        | B_CREATE_FILE | B_ERASE_FILE);
                    if (file.InitCheck() == B_OK
                            && file.Write(response.body.data(),
                                response.body.size())
                                == (ssize_t)response.body.size()) {
                        bitmap = BTranslationUtils::GetBitmap(
                            temporary.c_str());
                    }
                    if (bitmap && !bitmap->IsValid()) {
                        delete bitmap;
                        bitmap = nullptr;
                    }
                    if (bitmap) {
                        unlink(cacheFile.c_str());
                        rename(temporary.c_str(), cacheFile.c_str());
                        int64 maxCacheBytes = ImageCache::MaxCacheBytes();
                        if (maxCacheBytes > 0)
                            ImageCache::PruneToSize(maxCacheBytes);
                    } else {
                        unlink(temporary.c_str());
                    }
                }
                if (!bitmap) {
                    BMemoryIO memory(response.body.data(),
                        response.body.size());
                    bitmap = BTranslationUtils::GetBitmap(&memory);
                    if (bitmap && !bitmap->IsValid()) {
                        delete bitmap;
                        bitmap = nullptr;
                    }
                }
            }

            bool retryable = IsTransientImageFailure(response.statusCode)
                || (successfulResponse && !bitmap);
            if (!bitmap && retryable && attempt + 1 < kMaxDownloadAttempts) {
                int32 delayMs = 250 << (attempt * 2);
                if (response.retryAfter > 0)
                    delayMs = std::min<int32>(response.retryAfter * 1000, 5000);
                std::thread([url, cacheFile, generation, attempt, delayMs]() {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(delayMs));
                    if (ImageCache::IsCurrentGeneration(url, generation)) {
                        ImageCache::StartNetworkLoad(url, generation,
                            cacheFile, attempt + 1);
                    }
                }).detach();
                return;
            }

            ImageCache::FinishLoad(url, generation, bitmap);
        });
}

void ImageCache::FinishLoad(const std::string& url, uint64 generation,
    BBitmap* bitmap)
{
    std::vector<ImageCallback> callbacks;
    std::vector<BBitmap*> callbackBitmaps;
    {
        BAutolock lock(&sImageCacheLock);
        if (sGenerations[url] != generation) {
            delete bitmap;
            return;
        }
        if (bitmap) {
            auto previous = sCache.find(url);
            if (previous != sCache.end() && previous->second != bitmap)
                delete previous->second;
            sCache[url] = bitmap;
            sAccessOrder[url] = ++sNextAccessOrder;
            while (sCache.size() > kMaxMemoryCacheEntries) {
                auto oldest = sAccessOrder.end();
                for (auto current = sAccessOrder.begin();
                        current != sAccessOrder.end(); ++current) {
                    if (current->first == url)
                        continue;
                    if (oldest == sAccessOrder.end()
                            || current->second < oldest->second)
                        oldest = current;
                }
                if (oldest == sAccessOrder.end())
                    break;
                auto cached = sCache.find(oldest->first);
                if (cached != sCache.end()) {
                    delete cached->second;
                    sCache.erase(cached);
                }
                sAccessOrder.erase(oldest);
            }
        }
        auto pending = sPending.find(url);
        if (pending != sPending.end()) {
            callbacks.swap(pending->second);
            sPending.erase(pending);
        }
        if (bitmap) {
            for (size_t index = 0; index < callbacks.size(); index++) {
                BBitmap* copy = new BBitmap(bitmap);
                if (!copy->IsValid()) {
                    delete copy;
                    copy = nullptr;
                }
                callbackBitmaps.push_back(copy);
            }
        }
    }
    for (size_t index = 0; index < callbacks.size(); index++)
        callbacks[index](bitmap ? callbackBitmaps[index] : nullptr);
}
