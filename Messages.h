#ifndef MESSAGES_H
#define MESSAGES_H

enum {
	MSG_DISCOVER_LIBRARY_WRITE_RESULT = 'dLwR',
	MSG_DISCOVER_PLAYLIST_MUTATION_RESULT = 'dPmR',
	MSG_DISCOVER_PLAYLIST_CREATE_RESULT = 'plCr',
	MSG_DISCOVER_PLAYLIST_DROP_RESULT = 'dPlA',
	MSG_DISCOVER_CHECK_LAZY_LOAD = 'dLzy',
	MSG_DISCOVER_PLAYLIST_SNAPSHOT = 'pSyn',
	MSG_DISCOVER_MEMBERSHIP_CACHED = 'dLSt',
	MSG_DISCOVER_LIBRARY_RESOLVED = 'lAdd',
	MSG_DISCOVER_AUDIOBOOK_IDS = 'dAId',
	MSG_DISCOVER_ROWS = 'uRow',
	MSG_DISCOVER_PAGE_DONE = 'dPgD',
	MSG_DISCOVER_CACHE_SAVE = 'dCsv',
	MSG_DISCOVER_CACHE_LOADED = 'dCch',
	MSG_DRAG_ITEM = 'drag',
	MSG_DISCOVER_DROP = 'dDrp',
	MSG_PLAY_URI = 'play',
	MSG_QUEUE_ITEM = 'addQ',
	MSG_SEARCH_QUEUE_ITEM = 'sQue',
	MSG_CURRENT_TRACK_UPDATE = 'pStU',
	MSG_PLAYBACK_DEVICE_SELECTED = 'pbDs',
	MSG_PLAYBACK_DEVICE_START_LOCAL = 'pbDl',
	MSG_PLAYBACK_DEVICE_PROMPT_CLOSED = 'pbDx',

	MSG_PLAY_PAUSE		= 'plpa',
	MSG_NEXT_TRACK		= 'nxtr',
	MSG_PREV_TRACK		= 'pvtr',


	MSG_OPEN_BROWSER	= 'opbr',
	MSG_OPEN_PLAYLIST	= 'oppl',
	MSG_QUIT_APP		= 'qapp',
	MSG_SHOW_PLAYER_WINDOW		= 'shpw',
	MSG_HIDE_PLAYER_WINDOW		= 'hipw',
	MSG_TOGGLE_PLAYER_WINDOW	= 'tgpw',
	MSG_OPEN_SETTINGS			= 'opst',
	MSG_OPEN_ARTWORK			= 'opaw',
	MSG_SHOW_REPLICANT_MENU		= 'srpm',


	MSG_INIT_AUTH		= 'iaut',
	MSG_AUTH_COMPLETE	= 'acmp',


	MSG_SEARCH			= 'srch',


	MSG_TRACK_INVOKED	= 'trin',
	MSG_SHOW_ALBUM		= 'shal',
	MSG_SHOW_ARTIST		= 'shar',


	MSG_OPEN_QUEUE		= 'opqu',


	MSG_OPEN_SEARCH		= 'opsh',


	MSG_TOGGLE_SHUFFLE			= 'tshf',
	MSG_TOGGLE_REPEAT			= 'trep',
	MSG_START_LIBRESPOT			= 'stLb',
	MSG_TOGGLE_LIBRESPOT_RUNNING = 'tlbr',
	MSG_TOGGLE_LIBRESPOT_AUTOSTART = 'tlba',
	MSG_TOGGLE_MUTE			= 'tmte',
	MSG_SET_VOLUME				= 'svol',
	MSG_SEEK_REQUEST			= 'seek',
	MSG_SEEKBAR_COLOR_DROPPED	= 'sbcd',
	MSG_SEEKBAR_COLOR_CHANGED	= 'sbcc',
	MSG_DESKBAR_REPLICANT_CHANGED = 'dbCh',
	MSG_REPLICANT_APPEARANCE_CHANGED = 'raCh',
	MSG_SAVE_CURRENT_TRACK		= 'svct',
	MSG_SHOW_ADD_TRACK_MENU		= 'satm',
	MSG_PLAYLISTS_CHANGED		= 'plCh',
	MSG_LIBRARY_CHANGED			= 'lbCh',
	MSG_SPOTIFY_CAPABILITIES_CHANGED = 'spCp',
	MSG_DISCOVER_DRAG_HOVER		= 'dDhv',
	MSG_DISCOVER_DRAG_EXIT		= 'dDhx',
	MSG_DISCOVER_TAB_MOVED		= 'tRdr',
	MSG_DISCOVER_TAB_SELECTED	= 'tabS',
	MSG_DISCOVER_TAB_TOGGLED		= 'togT',
	MSG_DISCOVER_TAB_ORDER_RESET	= 'tRst',
	MSG_DISCOVER_DROP_TAB_SWITCH	= 'dTsW',
	MSG_HAIFY_DRAG_ENDED		= 'dEnd',

	MSG_REGISTER_REPLICANT		= 'rRpl',
	MSG_UNREGISTER_REPLICANT	= 'uRpl',
	MSG_SYNC_REPLICANT_STATE	= 'sRpl',
	MSG_REPLICANT_STATE			= 'rSt8',
};

// Wire names are compatibility contracts; see docs/message-contracts.md.
namespace MessageFields {
inline constexpr char ContextEpoch[] = "context_epoch";
inline constexpr char CommandContext[] = "command_context";
inline constexpr char WriteKind[] = "write_kind";
inline constexpr char PlaylistId[] = "playlistId";
inline constexpr char RequestId[] = "request_id";
inline constexpr char Id[] = "id";
inline constexpr char Name[] = "name";
inline constexpr char Owner[] = "owner";
inline constexpr char ApiOk[] = "api_ok";
inline constexpr char ResponseValid[] = "response_valid";
inline constexpr char Saved[] = "saved";
inline constexpr char Ok[] = "ok";
inline constexpr char Operation[] = "operation";
inline constexpr char Generation[] = "generation";
inline constexpr char Snapshot[] = "snapshot";
inline constexpr char HasMore[] = "has_more";
inline constexpr char NextCursor[] = "next_cursor";
inline constexpr char NextOffset[] = "next_offset";
inline constexpr char Status[] = "status";
inline constexpr char AudiobookId[] = "audiobook_id";
inline constexpr char AudiobookIdsSnapshot[] = "audiobook_ids_snapshot";
inline constexpr char Owned[] = "owned";
inline constexpr char Writable[] = "writable";
inline constexpr char Titles[] = "t";
inline constexpr char Uris[] = "u";
inline constexpr char Values[] = "v";
inline constexpr char CacheLast[] = "cache_last";
inline constexpr char CacheFirst[] = "cache_first";
inline constexpr char CacheAvailable[] = "cache_available";
inline constexpr char FromCache[] = "from_cache";
inline constexpr char AccountId[] = "account_id";
inline constexpr char CacheGeneration[] = "cache_generation";
inline constexpr char LoadGeneration[] = "load_generation";
inline constexpr char Columns[] = "cols";
inline constexpr char Uri[] = "uri";
inline constexpr char TrackUri[] = "trackUri";
inline constexpr char AlbumUri[] = "albumUri";
inline constexpr char ItemType[] = "itemType";
inline constexpr char SourcePlaylist[] = "sourcePlaylist";
inline constexpr char SourceIndex[] = "sourceIndex";
inline constexpr char DropIntent[] = "dropIntent";
inline constexpr char ContextUri[] = "context_uri";
inline constexpr char DeviceId[] = "device_id";
inline constexpr char StartPositionMs[] = "start_position_ms";
inline constexpr char NextQueueUri[] = "next_queue_uri";
inline constexpr char Cancelled[] = "cancelled";
inline constexpr char Title[] = "title";
inline constexpr char Artist[] = "artist";
inline constexpr char Album[] = "album";
inline constexpr char Duration[] = "duration";
inline constexpr char Tab[] = "tab";
inline constexpr char VisualTab[] = "visualTab";
inline constexpr char SourceVisualTab[] = "source";
inline constexpr char TargetVisualTab[] = "target";
inline constexpr char ScreenPoint[] = "screenPt";
inline constexpr char DragGeneration[] = "dragGeneration";
inline constexpr char TargetUri[] = "targetUri";
inline constexpr char TargetTitle[] = "targetTitle";
inline constexpr char TargetWritable[] = "targetWritable";
}

#endif
