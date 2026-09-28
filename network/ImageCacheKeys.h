#pragma once

#include <cstdint>
#include <string>

// Pure disk-cache key rules for downloaded artwork.
namespace ImageCacheKeys {

// FNV-1a over the complete URL. Host, size segment and query all take part,
// so e.g. mosaic.scdn.co/640/<ids> and /300/<ids> get different files.
uint64_t	UrlHash(const std::string& url);

// "<16 hex digits>.img"; the extension differs from the former "<tail>.jpg"
// names, which the size limit prunes as the oldest entries.
std::string	FileName(const std::string& url);

// Size to prune down to once the limit is exceeded; the headroom avoids a
// directory scan after every following download.
int64_t		PruneTarget(int64_t limitBytes);

}
