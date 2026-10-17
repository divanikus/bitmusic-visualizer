# Playlists

**Tracks** is a single queue of compositions from one or more local files.

| Action | Control |
| --- | --- |
| Open music / replace the queue | Folder button or Ctrl+O; select one or more files |
| Add files / playlists | **Add** icon (list with a plus), Ctrl+Shift+O, or drop files onto the player |
| Play a track | Double-click or Enter |
| Reorder | Drag selected rows; Ctrl/Shift selects several |
| Remove from the list | **Remove** icon (list with a minus) or Delete; music files are never deleted |
| New / open / save playlist | **Playlist actions** icon (list with a dropdown arrow) below the list |
| Repeat | Click the repeat button: Off → Track (1) → Playlist → Off |
| Random order | Toggle Shuffle beside Repeat |

A multi-song file adds one row per composition. A small filename header separates
consecutive compositions from that file. Click the header to select the whole
consecutive group; Ctrl-click adds or removes that group from the selection.
Click a song below the header to select it individually, including the first song.
Double-click a song to play it; double-clicking a header only selects its group.
Compositions can be moved, mixed with other files and removed independently.
MIDI channels/tracks that play together remain one composition.

Adding and reordering leave playback running. Removing the playing row lets it
finish, then advances to its surviving successor; removing all rows stops playback.
Previous/Next follow the queue, including file boundaries. With repeat off, the
last track stops. Track repeat uses the file's loop point when available.
See [track end and duration](#track-end-and-duration) for unknown-length music.

Shuffle visits each entry once before stopping or starting another cycle with
Playlist repeat. Previous follows listening history. Manually selecting a track
starts a new history branch. Repeat and Shuffle settings are saved automatically;
the queue itself is saved explicitly through **Save playlist…**.

## Track end and duration

The player uses file/decoder duration when available, or a confirmed end/loop
boundary from the decoder. Supported VGM command streams provide their first end and loop
point even when header duration is missing; tracker playback reports its first
order loop. These events follow the selected Repeat mode just like timed tracks.
Audio read-ahead does not itself advance the queue.

Many executable formats (NSF, GBS, AY, SID, and SPC without time metadata) do not
provide a reliable musical-loop signal. Such tracks keep playing indefinitely
by default. Silence is not treated as an ending and no song-length database is used.

To bound these tracks, open the **gear → Settings → Playback**, enable
**Limit tracks without a known duration**, and enter a time. The limit applies
when opening the next track; editing it does not interrupt the current track.
A known duration or proven decoder boundary takes precedence over this fallback.
The player shows the effective playback duration by the seek bar; playlist rows
continue to show metadata durations. The setting persists, including the last
entered time when the limit is disabled. Play after the end starts again at zero.

## Files and portability

Save as **M3U8** (UTF-8) or **M3U** (also written as UTF-8). Both are plain text
with relative music paths where possible. Moving the playlist and music together,
preserving their relative paths, keeps those links working. Absolute paths on
another drive or machine may need updating. Playlists do not contain music.

The player writes standard `#EXTM3U` / `#EXTINF` lines and one additional comment
before every path: `#EXTBMV-SUBSONG:1`. Its positive, **one-based** number selects
the composition within that file. This preserves exact order, subsets and
duplicates. Other players may ignore this comment and lose subsong selection.
This is not the older GME extended-M3U dialect; that dialect is not imported.

On import, a path without this comment adds all of the file's compositions.
`#EXTINF` supplies a display title; playback duration always comes from the
decoder, not an imported timeout. Unknown comments are ignored. Legacy M3U text
falls back to the system encoding if it is not valid UTF-8. M3U8 requires UTF-8.
Nested playlists and network URLs are not supported.

Metadata is read in the background. Unavailable entries stay visible in a muted
warning color with the reason in their tooltip; they can be removed or retried.
Playback stops on an error so that a missing ROM or SoundFont can be supplied,
rather than repeatedly retrying a broken queue.
