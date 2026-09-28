#include "ImageCache.h"
#include "HttpClient.h"
#include "ImageCacheKeys.h"
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
#include <deque>
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
static const int32 kMaxActiveLoads = 4;

struct ImageLoadJob {
    std::string url;
    uint64 generation = 0;
    bool allowDiskCache = true;
    int32 attempt = 0;
};

// Guarded by sImageCacheLock.
static std::deque<ImageLoadJob> sLoadQueue;
static int32 sActiveLoads = 0;
// Approximate bytes on disk, -1 when unknown. Overestimates (overwrites,
// removed broken files) only cause an earlier prune, never an overrun.
static int64 sDiskCacheBytes = -1;
static bool sPruneRunning = false;

struct CacheFileInfo {
    std::string path;
    off_t size = 0;
    time_t modified = 0;
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
    path.Append(ImageCacheKeys::FileName(url).c_str());
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

    BAutolock lock(&sImageCacheLock);
    sDiskCacheBytes = -1;
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

    BAutolock lock(&sImageCacheLock);
    sDiskCacheBytes = totalSize;
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
    ImageLoadJob job;
    job.url = url;
    job.generation = generation;
    job.allowDiskCache = allowDiskCache;
    EnqueueLoad(job);
}

void ImageCache::EnqueueLoad(const ImageLoadJob& job)
{
    {
        BAutolock lock(&sImageCacheLock);
        sLoadQueue.push_back(job);
    }
    PumpLoads();
}

void ImageCache::PumpLoads()
{
    BAutolock lock(&sImageCacheLock);
    while (sActiveLoads < kMaxActiveLoads && !sLoadQueue.empty()) {
        ImageLoadJob job = sLoadQueue.front();
        sLoadQueue.pop_front();
        sActiveLoads++;
        std::thread([job]() { ImageCache::RunLoad(job); }).detach();
    }
}

// Every started job calls this exactly once, when its disk read or HTTP
// request has finished.
void ImageCache::ReleaseLoadSlot()
{
    {
        BAutolock lock(&sImageCacheLock);
        sActiveLoads--;
    }
    PumpLoads();
}

void ImageCache::RunLoad(const ImageLoadJob& job)
{
    // Rows scrolled away or reloaded meanwhile cost neither disk nor network.
    if (!IsCurrentGeneration(job.url, job.generation)) {
        ReleaseLoadSlot();
        return;
    }

    std::string cacheFile = GetCachePath(job.url);
    bool readDisk = job.allowDiskCache && job.attempt == 0;
    if (!job.allowDiskCache && job.attempt == 0 && !cacheFile.empty())
        unlink(cacheFile.c_str());

    if (readDisk && !cacheFile.empty()) {
        BBitmap* diskBitmap = BTranslationUtils::GetBitmap(cacheFile.c_str());
        if (diskBitmap && !diskBitmap->IsValid()) {
            delete diskBitmap;
            diskBitmap = nullptr;
        }
        if (diskBitmap) {
            utime(cacheFile.c_str(), nullptr);
            FinishLoad(job.url, job.generation, diskBitmap);
            ReleaseLoadSlot();
            return;
        }
        // Do not repeatedly decode a known-broken cache entry.
        unlink(cacheFile.c_str());
    }

    StartNetworkLoad(job);
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

static BBitmap*
ValidatedBitmapFromFile(const std::string& path)
{
    BBitmap* bitmap = BTranslationUtils::GetBitmap(path.c_str());
    if (bitmap && !bitmap->IsValid()) {
        delete bitmap;
        bitmap = nullptr;
    }
    return bitmap;
}

static BBitmap*
DecodeBitmapBody(const std::string& body)
{
    BMemoryIO memory(body.data(), body.size());
    BBitmap* bitmap = BTranslationUtils::GetBitmap(&memory);
    if (bitmap && !bitmap->IsValid()) {
        delete bitmap;
        bitmap = nullptr;
    }
    return bitmap;
}

static void
NoteCacheFileWritten(off_t size)
{
    int64 limit;
    {
        BAutolock lock(&sImageCacheLock);
        limit = sMaxCacheBytes;
        if (sDiskCacheBytes >= 0)
            sDiskCacheBytes += size;
        bool needsPrune = limit > 0
            && (sDiskCacheBytes < 0 || sDiskCacheBytes > limit);
        if (!needsPrune || sPruneRunning)
            return;
        sPruneRunning = true;
    }
    ImageCache::PruneToSize(ImageCacheKeys::PruneTarget(limit));
    BAutolock lock(&sImageCacheLock);
    sPruneRunning = false;
}

static BBitmap*
WriteBitmapBodyToCache(const std::string& cacheFile, uint64 generation,
    const std::string& body)
{
    std::string temporary = cacheFile + ".part-" + std::to_string(generation);
    BFile file(temporary.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    BBitmap* bitmap = nullptr;
    if (file.InitCheck() == B_OK
            && file.Write(body.data(), body.size()) == (ssize_t)body.size()) {
        bitmap = ValidatedBitmapFromFile(temporary);
    }
    file.Unset();
    if (bitmap && rename(temporary.c_str(), cacheFile.c_str()) == 0)
        NoteCacheFileWritten((off_t)body.size());
    else
        unlink(temporary.c_str());
    return bitmap;
}

static BBitmap*
BitmapFromImageResponse(const HttpResponse& response,
    const std::string& cacheFile, uint64 generation, bool& successfulResponse)
{
    successfulResponse = response.statusCode >= 200
        && response.statusCode < 300;
    if (!successfulResponse || response.body.empty()
            || !LooksLikeCompleteImage(response.body)) {
        return nullptr;
    }
    BBitmap* bitmap = cacheFile.empty() ? nullptr
        : WriteBitmapBodyToCache(cacheFile, generation, response.body);
    return bitmap ? bitmap : DecodeBitmapBody(response.body);
}

// Delay before the next attempt, or -1 when the server asked for a longer
// pause than a UI image is worth waiting for.
static int32
RetryDelayMs(const HttpResponse& response, int32 attempt)
{
    static const int32 kMaxRetryDelayMs = 5000;
    if (response.retryAfter <= 0)
        return 250 << (attempt * 2);
    if (response.retryAfter > kMaxRetryDelayMs / 1000)
        return -1;
    return response.retryAfter * 1000;
}

void ImageCache::StartNetworkLoad(const ImageLoadJob& job)
{
    std::string cacheFile = GetCachePath(job.url);
    Headers headers;
    HttpClient::Get(job.url, headers,
        [job, cacheFile](const HttpResponse& response) {
            if (!ImageCache::IsCurrentGeneration(job.url, job.generation)) {
                ImageCache::ReleaseLoadSlot();
                return;
            }

            bool successfulResponse = false;
            BBitmap* bitmap = BitmapFromImageResponse(response, cacheFile,
                job.generation, successfulResponse);

            bool retryable = IsTransientImageFailure(response.statusCode)
                || (successfulResponse && !bitmap);
            int32 delayMs = RetryDelayMs(response, job.attempt);
            if (!bitmap && retryable && delayMs >= 0
                    && job.attempt + 1 < kMaxDownloadAttempts) {
                ImageCache::RetryLater(job, delayMs);
                ImageCache::ReleaseLoadSlot();
                return;
            }

            ImageCache::FinishLoad(job.url, job.generation, bitmap);
            ImageCache::ReleaseLoadSlot();
        });
}

// The waiting thread holds no load slot; the retry queues behind other work.
void ImageCache::RetryLater(const ImageLoadJob& job, int32 delayMs)
{
    ImageLoadJob next = job;
    next.attempt++;
    std::thread([next, delayMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        if (ImageCache::IsCurrentGeneration(next.url, next.generation))
            ImageCache::EnqueueLoad(next);
    }).detach();
}

void ImageCache::StoreMemoryCacheLocked(const std::string& url,
    BBitmap* bitmap)
{
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

void ImageCache::TakePendingCallbacksLocked(const std::string& url,
    std::vector<ImageCallback>& callbacks)
{
    auto pending = sPending.find(url);
    if (pending != sPending.end()) {
        callbacks.swap(pending->second);
        sPending.erase(pending);
    }
}

void ImageCache::CopyCallbackBitmaps(BBitmap* bitmap, size_t count,
    std::vector<BBitmap*>& callbackBitmaps)
{
    if (!bitmap)
        return;
    for (size_t index = 0; index < count; index++) {
        BBitmap* copy = new BBitmap(bitmap);
        if (!copy->IsValid()) {
            delete copy;
            copy = nullptr;
        }
        callbackBitmaps.push_back(copy);
    }
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
        if (bitmap)
            StoreMemoryCacheLocked(url, bitmap);
        TakePendingCallbacksLocked(url, callbacks);
        CopyCallbackBitmaps(bitmap, callbacks.size(), callbackBitmaps);
    }
    for (size_t index = 0; index < callbacks.size(); index++)
        callbacks[index](bitmap ? callbackBitmaps[index] : nullptr);
}
