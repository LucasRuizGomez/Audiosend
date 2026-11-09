# AudioSend

A VST3 plugin that serves your audio exports to your phone over local Wi-Fi, so you can listen on another speaker without cables or copying files.

**Website:** https://lukeda.me/audiosend/

![AudioSend in FL Studio](docs/plugin.png)

## Features

- Built-in HTTP server: open the plugin's URL on your phone, or scan the QR code shown in the plugin.
- Add files by drag and drop or a file browser (WAV, AIFF, MP3, FLAC).
- Watch folder: new exports dropped into a folder show up automatically under "Last Exports".
- Folder tags (Uploads, Snippets, Final Versions, References, Stems, or custom).
- Mix analysis for every file: integrated and short-term LUFS (ITU-R BS.1770-4), true peak (4x oversampled), dynamic range, RMS, stereo width, phase correlation, DC offset, clipping, limiting estimate and a 16-band spectrum.
- Phone player in the browser: seek, download, volume up to 200%, playback speed, mono check and loop.
- File list, watch folder and settings are saved with the DAW project.

## How it works

- **Audio source:** the plugin does not capture or stream the DAW's live output. `processBlock` leaves the audio untouched. It serves audio *files* that you add by hand or that appear in the watch folder (checked every 3 s; a file is ingested once its size is stable).
- **Network:** a minimal HTTP/1.1 server written on `juce::StreamingSocket` (TCP), port `8765` by default (tries `8766` if busy; configurable in settings). Each request runs on its own thread and the connection closes after the response.
- **Endpoints:**
  - `GET /`: the phone web UI.
  - `GET /api/files`: the file list and analysis as JSON.
  - `GET /audio/<id>`: streams the file, with HTTP `Range` support so the phone can seek.
  - `GET /dl/<id>`: downloads the file.
- **Audio format:** no transcoding or compression. The original file bytes are sent in 64 KB chunks with a MIME type based on the extension.
- **Latency and buffering:** playback is not real time. The phone's browser buffers the file through a standard `<audio>` element. The web UI polls `/api/files` every 5 s to pick up new files.
- **Discovery:** no automatic discovery. The plugin takes the first private IPv4 address of the computer (`192.168.*`, `10.*` or `172.*`) and shows `http://<ip>:<port>` as text and as a QR code.
- **Phone side:** any mobile browser. No app needed. The web UI (`songsender_web_ui.html`) is embedded into the plugin binary as JUCE BinaryData. Volume and mono use the Web Audio API.
- **Analysis:** runs on a background thread when a file is added (`AudioAnalyzer`). Loudness uses a K-weighting filter and gated blocks; the spectrum uses an 8192-point FFT.

## Tech stack

- C++17
- JUCE 8.0.12 (Projucer project)
- Plugin format: VST3
- Platform: Windows (Visual Studio 2026 exporter)
- Phone UI: HTML, CSS and JavaScript (no framework)
- QR codes: [Nayuki QR Code generator](https://www.nayuki.io/page/qr-code-generator-library)

<!-- TODO: the website lists AU and macOS. This project only has a Windows/VST3 exporter. -->

## Build (Windows)

Requirements:
- [JUCE 8.0.12](https://github.com/juce-framework/JUCE/releases/tag/8.0.12) (includes Projucer)
- Visual Studio 2026 with the "Desktop development with C++" workload

Steps:
1. Open `Audiosend.jucer` in Projucer.
2. In Projucer, set **File → Global Paths → Path to JUCE** and **JUCE Modules** to your JUCE folder.
3. Click **Save and Open in IDE**. This generates `Builds/VisualStudio2026/` and `JuceLibraryCode/`.
4. In Visual Studio, select **Release | x64** and build the **Audiosend - VST3** project.
5. The plugin is written to `Builds/VisualStudio2026/x64/Release/VST3/NewProject.vst3`.
6. Copy `NewProject.vst3` to `C:\Program Files\Common Files\VST3\` and rescan plugins in your DAW.

<!-- TODO: the output file is named NewProject.vst3 (targetName in the .jucer), not Audiosend.vst3. -->

For usage (connecting the phone, watch folder, tags, settings) see [docs/SETUP_GUIDE.md](docs/SETUP_GUIDE.md).

## Project structure

```
Audiosend.jucer              Projucer project (generates Builds/ and JuceLibraryCode/)
docs/
└── SETUP_GUIDE.md           User guide
Source/
├── PluginProcessor.h/.cpp   Plugin core: file list, watch folder, HTTP server, saved state
├── PluginEditor.h/.cpp      Plugin window: drop zone, file list, QR code, analysis panel, settings
├── AudioAnalyzer.h/.cpp     Offline analysis: LUFS, true peak, dynamics, stereo, spectrum
├── QRCodeGenerator.h        Draws the server URL as a QR code (juce::Image)
├── qrcodegen.hpp/.cpp       Nayuki QR Code generator library (third-party, MIT)
└── songsender_web_ui.html   Phone web UI, embedded as BinaryData
```

## License

AudioSend is free software, licensed under the [GNU Affero General Public License v3.0](LICENSE).

It is built with [JUCE](https://juce.com), used under the AGPLv3. `qrcodegen.hpp` and `qrcodegen.cpp` are © Project Nayuki, MIT License.
