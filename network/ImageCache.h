#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <Bitmap.h>
#include <SupportDefs.h>

// A non-null bitmap passed to a callback belongs to the callback recipient.
using ImageCallback = std::function<void(BBitmap* bitmap)>;

struct ImageLoadJob;

class ImageCache {
public:
    static void GetImage(const std::string& url, ImageCallback callback);
    static void ReloadImage(const std::string& url, ImageCallback callback);
    static status_t Clear(int32* filesRemoved = nullptr);
    static std::string CacheDirectoryPath();
    static void SetMaxCacheBytes(int64 maxBytes);
    static int64 MaxCacheBytes();
    static int64 CacheSize();
    static status_t PruneToSize(int64 maxBytes);

private:
    // At most kMaxActiveLoads jobs run at once; the rest wait in a FIFO, so
    // a long list cannot spawn one thread and one HTTP request per row.
    static void StartLoad(const std::string& url, uint64 generation,
        bool allowDiskCache);
    static void EnqueueLoad(const ImageLoadJob& job);
    static void PumpLoads();
    static void ReleaseLoadSlot();
    static void RunLoad(const ImageLoadJob& job);
    static void StartNetworkLoad(const ImageLoadJob& job);
    static void RetryLater(const ImageLoadJob& job, int32 delayMs);
    static bool IsCurrentGeneration(const std::string& url,
        uint64 generation);
    static void FinishLoad(const std::string& url, uint64 generation,
        BBitmap* bitmap);
    static void StoreMemoryCacheLocked(const std::string& url,
        BBitmap* bitmap);
    static void TakePendingCallbacksLocked(const std::string& url,
        std::vector<ImageCallback>& callbacks);
    static void CopyCallbackBitmaps(BBitmap* bitmap,
        size_t count, std::vector<BBitmap*>& callbackBitmaps);
    static std::map<std::string, BBitmap*> sCache;
    static std::map<std::string, uint64> sAccessOrder;
    static uint64 sNextAccessOrder;
    static std::map<std::string, std::vector<ImageCallback>> sPending;
    static std::map<std::string, uint64> sGenerations;
};
