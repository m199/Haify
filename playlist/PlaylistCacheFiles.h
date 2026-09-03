#pragma once

#include "PlaylistCacheDocument.h"

#include <SupportDefs.h>

#include <string>
#include <vector>

class BPath;

namespace PlaylistCacheFiles {

struct TrackDocument {
	int32									total = 0;
	int32									nextOffset = 0;
	std::string								snapshotId;
	std::vector<PlaylistCacheDocument::Track> tracks;
};

struct ShowDocument {
	int32									total = 0;
	int32									nextOffset = 0;
	bool									hasNextOffset = false;
	std::vector<PlaylistCacheDocument::Episode> episodes;
};

bool	LikedSongsPath(BPath& path, bool createDirectories);
bool	PlaylistPath(const std::string& playlistId, BPath& path,
			bool createDirectories);
bool	ShowPath(const std::string& showId, BPath& path,
			bool createDirectories);
bool	TrackPath(bool isPlaylist, const std::string& playlistId,
			BPath& path, bool createDirectories);
bool	UriUsesTrackCache(const std::string& uri);
bool	UriUsesShowCache(const std::string& uri);
bool	UriUsesPlaylistTrackCache(const std::string& uri);
bool	ReadTrackDocument(bool isPlaylist, const std::string& playlistId,
			TrackDocument& document);
bool	ReadTrackDocument(const BPath& path, bool isPlaylist,
			TrackDocument& document);
bool	ReadTrackDocumentForUri(const std::string& uri,
			TrackDocument& document, bool* isPlaylist = nullptr);
bool	ReadShowDocument(const std::string& showId, ShowDocument& document);
bool	ReadShowDocument(const BPath& path, ShowDocument& document);
bool	ReadShowDocumentForUri(const std::string& uri,
			ShowDocument& document);
bool	WriteTrackDocument(bool isPlaylist, const std::string& playlistId,
			int32 total, int32 nextOffset, const std::string& snapshotId,
			const std::vector<PlaylistCacheDocument::Track>& tracks);
bool	WriteTrackDocumentForUri(const std::string& uri, int32 total,
			int32 nextOffset, const std::string& snapshotId,
			const std::vector<PlaylistCacheDocument::Track>& tracks);
void	WriteTrackDocument(const std::string& path, bool isPlaylist,
			int32 total, int32 nextOffset, const std::string& snapshotId,
			const std::vector<PlaylistCacheDocument::Track>& tracks);
void	WriteShowDocument(const std::string& showId, int32 total,
			int32 nextOffset, bool complete,
			const std::vector<PlaylistCacheDocument::Episode>& episodes);
void	WriteShowDocumentForUri(const std::string& uri, int32 total,
			int32 nextOffset, bool complete,
			const std::vector<PlaylistCacheDocument::Episode>& episodes);
void	RemoveLikedSongs();
void	RemovePlaylist(const std::string& playlistId);
void	RemoveShow(const std::string& showId);
void	RemoveForUri(const std::string& uri);

}
