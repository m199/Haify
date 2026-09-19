#pragma once

#include "DiscoverCacheDocument.h"
#include "DiscoverAsyncScope.h"
#include "DiscoverPlaylistMutationController.h"
#include "DiscoverLibraryWriteController.h"
#include "messages/Messages.h"

#include <Message.h>

namespace DiscoverMessages {
void AddContext(BMessage& message, const DiscoverAsyncContext& context);
std::optional<DiscoverAsyncContext> ReadContext(const BMessage& message);
void AddAsyncToken(BMessage& message, const DiscoverAsyncToken& token);
std::optional<DiscoverAsyncToken> ReadAsyncToken(const BMessage& message);
BMessage OperationResult(uint32 code, const DiscoverAsyncToken& token, bool ok, int32 status);
void AddLibraryWrite(BMessage& message, const DiscoverLibraryWriteRequest& request);
std::optional<DiscoverLibraryWriteRequest> ReadLibraryWrite(const BMessage& message);
std::optional<DiscoverLibraryWriteRequest> ReadLibraryCommand(const BMessage& message);
BMessage LibraryChanged(const DiscoverLibraryChange& change, const std::string& account);
BMessage PlaylistChanged(const DiscoverPlaylistChange& change, const std::string& account);
bool ReadRowColumns(const BMessage& message, int32_t tab, DiscoverRowData& columns);
// Wire contracts and ordering: docs/discover-state-contracts.md.
BMessage PageDone(const DiscoverPageResult& result);
std::optional<DiscoverPageResult> ReadPageDone(const BMessage& message);
std::optional<DiscoverRowData> ReadResolvedLibraryRow(const BMessage& message,
	int32_t tab, const std::string& uri);
std::optional<DiscoverPlaylistChange> ReadPlaylistChange(const BMessage& message);
// Producers supply validated two-column playlist rows from DiscoverRowFactory.
BMessage PlaylistSnapshot(const std::vector<DiscoverRowData>& rows, int32_t generation);
std::vector<DiscoverRowData> ReadPlaylistSnapshot(const BMessage& message);
void AppendRows(BMessage& message, const std::vector<DiscoverRowData>& rows,
	size_t begin, size_t end);
BMessage CacheBatch(const DiscoverCacheReadResult& result, size_t begin, size_t end);
}
