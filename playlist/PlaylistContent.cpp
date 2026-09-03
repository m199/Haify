#include "PlaylistContent.h"

PlaylistContentTarget
ResolvePlaylistContentTarget(const std::string& uri)
{
	PlaylistContentTarget target;
	target.isCollection = uri == "spotify:collection";
	target.kind = target.isCollection
		? kSpotifyItemUnknown : SpotifyItemKindForUri(uri);
	target.id = target.isCollection ? "" : SpotifyItemIdForUri(uri);
	return target;
}
