# AudioSend: User Guide

## Connecting your phone

1. Load AudioSend on any track in your DAW.
2. Make sure your phone and computer are on the same Wi-Fi network.
3. Scan the QR code in the plugin window, or type the URL it shows (`http://<your-ip>:8765`) into your phone's browser. **Copy URL** puts it on the clipboard.

No app is needed. The phone UI is built into the plugin.

## Adding files

- Drag audio files (WAV, AIFF, MP3, FLAC) onto the plugin, or use the file browser.
- Each file is analysed when it is added (loudness, true peak, stereo, spectrum).
- New files appear on the phone within a few seconds (the phone refreshes the list every 5 s).

## Last Exports (watch folder)

The watch folder defaults to:

```
C:\Users\<you>\Documents\SongSender\LastExports\
```

1. Click **Set Folder** to choose a different folder (optional).
2. Enable **Auto-watch Last Exports**.
3. Export from your DAW into that folder.

The folder is checked every 3 seconds. New audio files are added under the **Last Exports** tag once they finish writing. Files that were already in the folder when you enabled Auto-watch are skipped.

Where to point your DAW's export:
- **FL Studio:** File → Export → WAV/MP3 → choose the watch folder.
- **Ableton Live:** File → Export Audio/Video → choose the watch folder.
- **Reaper:** File → Render → choose the watch folder.

## Folder tags

Pick a tag from the dropdown before adding a file. Built-in tags:

| Tag | Use for |
|-----|---------|
| Uploads | General / unsorted |
| Snippets | Short ideas, loops, hooks |
| Final Versions | Mastered or finished exports |
| References | Reference tracks from other artists |
| Stems | Individual stem exports |

Click **+** next to the dropdown to create a custom tag. On the phone, tap a tag at the top of the list to filter.

## Settings

Open **Settings** in the plugin to change:
- **Server Port** (default 8765). Avoid 80, 443 and 3000. Takes effect after reloading the plugin.
- **Max files to keep** (default 50). The oldest files drop off the list.
- **Show LUFS badge** and **Compact file rows** (display only).

## Window size

The plugin window is resizable from 560 × 360 up to 1000 × 700 px. The QR code and file list scale with it.

## For developers: the embedded web UI

`Source/songsender_web_ui.html` is marked as a binary resource in `Audiosend.jucer`, so Projucer embeds it into the plugin as BinaryData. After editing the HTML, re-save the project in Projucer and rebuild.

If BinaryData is missing, the plugin looks for `songsender_web_ui.html` on disk next to the `.vst3` and in `Documents\SongSender\`, and shows an error page if it can't find it.
