#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── If you add songsender_web_ui.html as a BinaryData resource in Projucer,
//    this include will exist and the embedded HTML path will be used automatically.
//    If it doesn't exist yet, the disk-search fallback runs instead.
#if __has_include("BinaryData.h")
  #include "BinaryData.h"
  #define HAS_BINARY_DATA 1
#endif

// ─────────────────────────────────────────────────────────────────────────────
SongSenderProcessor::SongSenderProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Default watch folder: Documents/SongSender/LastExports
    watchFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                    .getChildFile("SongSender").getChildFile("LastExports");

    startServer();
    startTimer(3000); // poll watch folder every 3 seconds
}

SongSenderProcessor::~SongSenderProcessor()
{
    stopTimer();
    stopServer();
}

void SongSenderProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
}

// ─── Add file ─────────────────────────────────────────────────────────────────
void SongSenderProcessor::addFile(const juce::File& f, const juce::String& folder)
{
    if (!f.existsAsFile()) return;

    juce::Thread::launch([this, f, folder]()
    {
        FileRecord rec;
        rec.id        = juce::Uuid().toString().substring(0, 8);
        rec.file      = f;
        rec.timestamp = juce::Time::currentTimeMillis();
        rec.folder    = folder.isEmpty() ? "Uploads" : folder;
        rec.analysis  = analyzer.analyze(f);

        {
            juce::ScopedLock lock(filesLock);
            files.insert(files.begin(), rec);
            if ((int)files.size() > maxFilesToKeep) files.resize((size_t)maxFilesToKeep);
        }

        juce::MessageManager::callAsync([this]() {
            if (onFilesChanged) onFilesChanged();
        });
    });
}

// ─── Watch folder ─────────────────────────────────────────────────────────────
void SongSenderProcessor::setWatchFolder(const juce::File& dir)
{
    watchFolder = dir;
    watchFolder.createDirectory(); // ensure it exists
    seenByWatcher.clear();         // re-scan on next tick
}

void SongSenderProcessor::setWatchEnabled(bool e)
{
    watchEnabled = e;
    if (e)
    {
        watchFolder.createDirectory();
        seenByWatcher.clear(); // will re-scan immediately but not re-add existing files
        // Pre-populate seenByWatcher so we only pick up NEW files from here on
        juce::Array<juce::File> existing;
        watchFolder.findChildFiles(existing, juce::File::findFiles, false,
                                    "*.wav;*.aiff;*.aif;*.mp3;*.flac");
        for (auto& ef : existing)
            seenByWatcher.addIfNotAlreadyThere(ef.getFullPathName());
    }
}

void SongSenderProcessor::timerCallback()
{
    if (watchEnabled) scanWatchFolder();
}

void SongSenderProcessor::scanWatchFolder()
{
    if (!watchFolder.isDirectory()) return;

    juce::Array<juce::File> found;
    watchFolder.findChildFiles(found, juce::File::findFiles, false,
                                "*.wav;*.aiff;*.aif;*.mp3;*.flac");

    for (auto& f : found)
    {
        juce::String path = f.getFullPathName();
        if (seenByWatcher.contains(path)) continue;
        seenByWatcher.add(path);

        // Only ingest files that finished writing (size stable for >1s)
        // We check by comparing file size to itself 500ms later
        juce::int64 size1 = f.getSize();
        juce::Thread::sleep(500);
        if (f.getSize() != size1) continue; // still being written

        // Add with "Last Exports" folder tag
        juce::File fileCopy = f;
        juce::Thread::launch([this, fileCopy]()
        {
            FileRecord rec;
            rec.id           = juce::Uuid().toString().substring(0, 8);
            rec.file         = fileCopy;
            rec.timestamp    = juce::Time::currentTimeMillis();
            rec.folder       = "Last Exports";
            rec.isAutoWatch  = true;
            rec.analysis     = analyzer.analyze(fileCopy);

            {
                juce::ScopedLock lock(filesLock);
                files.insert(files.begin(), rec);
                if ((int)files.size() > maxFilesToKeep) files.resize((size_t)maxFilesToKeep);
            }

            juce::MessageManager::callAsync([this]() {
                if (onFilesChanged) onFilesChanged();
            });
        });
    }
}

// ─── URL & IP ─────────────────────────────────────────────────────────────────
juce::String SongSenderProcessor::getURL() const
{
    return "http://" + getLocalIP() + ":" + juce::String(serverPort);
}

juce::String SongSenderProcessor::getLocalIP() const
{
    for (auto& addr : juce::IPAddress::getAllAddresses())
    {
        auto s = addr.toString();
        if (s.startsWith("192.168.") || s.startsWith("10.") || s.startsWith("172."))
            return s;
    }
    return "127.0.0.1";
}

// ─── HTTP Server ──────────────────────────────────────────────────────────────
void SongSenderProcessor::startServer()
{
    if (!serverSocket.createListener(serverPort))
    {
        ++serverPort;
        serverSocket.createListener(serverPort);
    }
    serverRunning = true;
    juce::Thread::launch([this]()
    {
        while (serverRunning.load())
        {
            auto* client = serverSocket.waitForNextConnection();
            if (!serverRunning.load()) { delete client; break; }
            if (!client) continue;
            juce::Thread::launch([this, client]() {
                handleClient(client);
                delete client;
            });
        }
    });
}

void SongSenderProcessor::stopServer()
{
    serverRunning = false;
    serverSocket.close();
}

void SongSenderProcessor::handleClient(juce::StreamingSocket* client)
{
    std::vector<char> raw;
    raw.reserve(2048);
    char b = 0;
    while (raw.size() < 16384)
    {
        int n = client->read(&b, 1, true);
        if (n <= 0) break;
        raw.push_back(b);
        int sz = (int)raw.size();
        if (sz >= 4 &&
            raw[sz-4]=='\r' && raw[sz-3]=='\n' &&
            raw[sz-2]=='\r' && raw[sz-1]=='\n')
            break;
    }
    if (raw.empty()) return;

    juce::String req(raw.data(), (int)raw.size());
    juce::String firstLine = req.upToFirstOccurrenceOf("\r\n", false, false);
    juce::StringArray parts = juce::StringArray::fromTokens(firstLine, " ", "");
    juce::String method = parts.size() > 0 ? parts[0].toUpperCase() : "GET";
    juce::String path   = parts.size() > 1 ? parts[1] : "/";
    path = path.upToFirstOccurrenceOf("?", false, false);
    if (path.isEmpty()) path = "/";

    serveRequest(client, method, path, req);
}

static bool parseRange(const juce::String& req, juce::int64 fileSize,
                        juce::int64& outStart, juce::int64& outEnd)
{
    juce::String lower = req.toLowerCase();
    int pos = lower.indexOf("range: bytes=");
    if (pos < 0) return false;
    juce::String val = req.substring(pos + 13)
                          .upToFirstOccurrenceOf("\r\n", false, false).trim();
    juce::String startStr = val.upToFirstOccurrenceOf("-", false, false).trim();
    juce::String endStr   = val.fromFirstOccurrenceOf("-", false, false).trim();
    outStart = startStr.isEmpty() ? 0LL : startStr.getLargeIntValue();
    outEnd   = endStr.isEmpty()   ? fileSize - 1
                                  : juce::jmin(endStr.getLargeIntValue(), fileSize - 1);
    outStart = juce::jlimit((juce::int64)0, fileSize-1, outStart);
    outEnd   = juce::jlimit(outStart, fileSize-1, outEnd);
    return true;
}

void SongSenderProcessor::serveRequest(juce::StreamingSocket* client,
                                        const juce::String& method,
                                        const juce::String& path,
                                        const juce::String& req)
{
    bool isHead = (method == "HEAD");

    if (path == "/" || path == "/index.html")
    {
        juce::String body = buildIndexHTML();
        if (isHead) sendHTTPResponseHeadersOnly(client, 200, "text/html; charset=utf-8",
                                                  (juce::int64)body.getNumBytesAsUTF8());
        else        sendHTTPResponse(client, 200, "text/html; charset=utf-8", body);
        return;
    }
    if (path == "/api/files")
    {
        juce::String body = buildFilesJSON();
        if (isHead) sendHTTPResponseHeadersOnly(client, 200, "application/json",
                                                  (juce::int64)body.getNumBytesAsUTF8());
        else        sendHTTPResponse(client, 200, "application/json", body);
        return;
    }
    if (path.startsWith("/audio/") || path.startsWith("/dl/"))
    {
        bool isDownload = path.startsWith("/dl/");
        juce::String id = path.fromLastOccurrenceOf("/", false, false);
        juce::ScopedLock lock(filesLock);
        for (auto& rec : files)
        {
            if (rec.id != id) continue;
            juce::String ext  = rec.file.getFileExtension().toLowerCase();
            juce::String mime = ext == ".mp3"  ? "audio/mpeg"
                              : ext == ".wav"  ? "audio/wav"
                              : ext == ".aiff" || ext == ".aif" ? "audio/aiff"
                              : ext == ".flac" ? "audio/flac"
                              : "application/octet-stream";
            if (isHead)
                sendHTTPResponseHeadersOnly(client, 200, mime,
                                             rec.file.getSize(), "Accept-Ranges: bytes\r\n");
            else if (isDownload)
                sendAudioFileDownload(client, rec.file, mime, rec.file.getFileName());
            else
                sendAudioFileStream(client, rec.file, mime, req);
            return;
        }
        sendHTTPResponse(client, 404, "text/plain", "File not found");
        return;
    }
    sendHTTPResponse(client, 404, "text/plain", "Not found");
}

void SongSenderProcessor::sendAudioFileStream(juce::StreamingSocket* client,
                                               const juce::File& file,
                                               const juce::String& mime,
                                               const juce::String& req)
{
    juce::int64 fileSize = file.getSize();
    if (fileSize <= 0) { sendHTTPResponse(client, 404, "text/plain", "Empty"); return; }

    juce::int64 rangeStart = 0, rangeEnd = fileSize - 1;
    bool hasRange = parseRange(req, fileSize, rangeStart, rangeEnd);
    juce::int64 contentLength = rangeEnd - rangeStart + 1;
    int statusCode = hasRange ? 206 : 200;
    juce::String statusText = hasRange ? "Partial Content" : "OK";

    juce::String headers;
    headers << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
            << "Content-Type: " << mime << "\r\n"
            << "Content-Length: " << contentLength << "\r\n"
            << "Accept-Ranges: bytes\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Cache-Control: no-cache\r\n"
            << "Connection: close\r\n";
    if (hasRange)
        headers << "Content-Range: bytes " << rangeStart << "-" << rangeEnd << "/" << fileSize << "\r\n";
    headers << "\r\n";
    client->write(headers.toRawUTF8(), (int)headers.getNumBytesAsUTF8());

    juce::FileInputStream stream(file);
    if (!stream.openedOk()) return;
    if (rangeStart > 0) stream.setPosition(rangeStart);

    const int CHUNK = 65536;
    juce::HeapBlock<char> buf(CHUNK);
    juce::int64 remaining = contentLength;
    while (remaining > 0)
    {
        int toRead  = (int)juce::jmin((juce::int64)CHUNK, remaining);
        int didRead = stream.read(buf.getData(), toRead);
        if (didRead <= 0) break;
        if (client->write(buf.getData(), didRead) < 0) break;
        remaining -= didRead;
    }
}

void SongSenderProcessor::sendAudioFileDownload(juce::StreamingSocket* client,
                                                 const juce::File& file,
                                                 const juce::String& mime,
                                                 const juce::String& filename)
{
    juce::String headers;
    headers << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: " << mime << "\r\n"
            << "Content-Length: " << file.getSize() << "\r\n"
            << "Content-Disposition: attachment; filename=\"" << filename.replace("\"","\\\"") << "\"\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Connection: close\r\n\r\n";
    client->write(headers.toRawUTF8(), (int)headers.getNumBytesAsUTF8());

    juce::FileInputStream stream(file);
    if (!stream.openedOk()) return;
    const int CHUNK = 65536;
    juce::HeapBlock<char> buf(CHUNK);
    int didRead = 0;
    while ((didRead = stream.read(buf.getData(), CHUNK)) > 0)
        client->write(buf.getData(), didRead);
}

void SongSenderProcessor::sendHTTPResponse(juce::StreamingSocket* client,
                                            int status,
                                            const juce::String& contentType,
                                            const juce::String& body)
{
    juce::String st = status==200?"OK":status==206?"Partial Content":status==404?"Not Found":"Error";
    juce::String h;
    h << "HTTP/1.1 " << status << " " << st << "\r\n"
      << "Content-Type: " << contentType << "\r\n"
      << "Content-Length: " << (int)body.getNumBytesAsUTF8() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Cache-Control: no-cache\r\n"
      << "Connection: close\r\n\r\n";
    client->write(h.toRawUTF8(), (int)h.getNumBytesAsUTF8());
    client->write(body.toRawUTF8(), (int)body.getNumBytesAsUTF8());
}

void SongSenderProcessor::sendHTTPResponseHeadersOnly(juce::StreamingSocket* client,
                                                       int status,
                                                       const juce::String& contentType,
                                                       juce::int64 contentLength,
                                                       const juce::String& extra)
{
    juce::String st = status==200?"OK":status==206?"Partial Content":status==404?"Not Found":"Error";
    juce::String h;
    h << "HTTP/1.1 " << status << " " << st << "\r\n"
      << "Content-Type: " << contentType << "\r\n"
      << "Content-Length: " << contentLength << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Cache-Control: no-cache\r\n"
      << "Connection: close\r\n"
      << extra << "\r\n";
    client->write(h.toRawUTF8(), (int)h.getNumBytesAsUTF8());
}

// ─── JSON ─────────────────────────────────────────────────────────────────────
juce::String SongSenderProcessor::buildFilesJSON()
{
    juce::ScopedLock lock(filesLock);
    juce::String out = "[";
    bool first = true;
    for (auto& rec : files)
    {
        if (!first) out += ",";
        first = false;
        const AudioAnalysis& a = rec.analysis;
        float sizeKB = (float)rec.file.getSize() / 1024.0f;
        juce::String sizeStr = sizeKB > 1024.0f
            ? juce::String(sizeKB/1024.0f, 1) + " MB"
            : juce::String((int)sizeKB) + " KB";
        juce::String safeName = rec.file.getFileName().replace("\\","\\\\").replace("\"","\\\"");
        juce::String safeFolder = rec.folder.replace("\"","\\\"");
        juce::String bands = "[";
        for (int i = 0; i < 16; ++i)
            bands += juce::String(a.frequencyBands[i], 1) + (i < 15 ? "," : "");
        bands += "]";

        out << "{"
            << "\"id\":\""          << rec.id       << "\","
            << "\"name\":\""        << safeName     << "\","
            << "\"ext\":\""         << rec.file.getFileExtension().trimCharactersAtStart(".").toUpperCase() << "\","
            << "\"timestamp\":"     << rec.timestamp << ","
            << "\"size\":\""        << sizeStr      << "\","
            << "\"folder\":\""      << safeFolder   << "\","
            << "\"autoWatch\":"     << (rec.isAutoWatch ? "true" : "false") << ","
            << "\"url\":\"/audio/"  << rec.id       << "\","
            << "\"dlUrl\":\"/dl/"   << rec.id       << "\","
            << "\"analysis\":{"
            << "\"lufs\":"              << juce::String(a.integratedLUFS,   2) << ","
            << "\"shortTermLufs\":"     << juce::String(a.shortTermLUFS,    2) << ","
            << "\"truePeak\":"          << juce::String(a.truePeak,          2) << ","
            << "\"dynamicRange\":"      << juce::String(a.dynamicRange,      2) << ","
            << "\"stereoWidth\":"       << juce::String(a.stereoWidth,       1) << ","
            << "\"phaseCorrelation\":"  << juce::String(a.phaseCorrelation,  3) << ","
            << "\"dcOffset\":"          << juce::String((a.dcOffsetLeft+a.dcOffsetRight)*0.5f, 4) << ","
            << "\"rmsLeft\":"           << juce::String(a.rmsLeft,           2) << ","
            << "\"rmsRight\":"          << juce::String(a.rmsRight,          2) << ","
            << "\"clipping\":"          << (a.hasClipping?"true":"false")       << ","
            << "\"clippingCount\":"     << a.clippingCount                      << ","
            << "\"limiting\":"          << (a.hasLimiting?"true":"false")       << ","
            << "\"limitingAmount\":"    << juce::String(a.limitingAmount,    2) << ","
            << "\"sampleRate\":"        << (int)a.sampleRate                    << ","
            << "\"bitDepth\":"          << a.bitDepth                           << ","
            << "\"duration\":"          << juce::String(a.duration,          1) << ","
            << "\"transientDensity\":\"" << a.transientDensity                  << "\","
            << "\"bassEnergy\":\""      << a.bassEnergy                         << "\","
            << "\"midEnergy\":\""       << a.midEnergy                          << "\","
            << "\"highEnergy\":\""      << a.highEnergy                         << "\","
            << "\"mono\":"              << (a.isMono?"true":"false")            << ","
            << "\"freqBalance\":"       << bands
            << "}"
            << "}";
    }
    return out + "]";
}

// ─── HTML — three-tier embedding strategy ─────────────────────────────────────
//
//  Tier 1 (zero-copy, best): Projucer BinaryData
//    → In Projucer: File → Add existing files → songsender_web_ui.html
//      → tick "Add to binary resources" → Save → re-export to VS.
//      → The #if HAS_BINARY_DATA block below will activate automatically.
//
//  Tier 2 (dev convenience): disk search
//    → Searches several standard locations. No recompile needed when you
//      edit the HTML — just save and reload the plugin.
//
//  Tier 3 (absolute fallback): never happens if either above works.
//
juce::String SongSenderProcessor::buildIndexHTML()
{
#ifdef HAS_BINARY_DATA
    // Tier 1: embedded at compile time — zero external file dependency
    if (BinaryData::songsender_web_ui_htmlSize > 0)
        return juce::String::createStringFromData(
            BinaryData::songsender_web_ui_html,
            BinaryData::songsender_web_ui_htmlSize);
#endif

    // Tier 2: disk search (development mode)
    juce::File pluginExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

    // Walk up to find the .vst3 bundle or the project Source folder
    juce::Array<juce::File> searchDirs = {
        pluginExe.getParentDirectory(),
        pluginExe.getParentDirectory().getParentDirectory(),
        pluginExe.getParentDirectory().getParentDirectory().getParentDirectory(),
        juce::File("C:\\Program Files\\Common Files\\VST3\\SongSender"),
        juce::File("C:\\Program Files\\Common Files\\VST3"),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SongSender"),
        // VS project output folder (Release/Debug sit next to Source/)
        pluginExe.getParentDirectory().getParentDirectory()
                 .getChildFile("Source"),
    };

    for (auto& dir : searchDirs)
    {
        auto f = dir.getChildFile("songsender_web_ui.html");
        if (f.existsAsFile())
            return f.loadFileAsString();
    }

    // Tier 3: minimal error page
    return R"HTML(<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SongSender</title>
<style>body{background:#c8eeff;color:#1a3a52;font-family:'Segoe UI',sans-serif;
display:flex;flex-direction:column;align-items:center;justify-content:center;
min-height:100vh;padding:24px;text-align:center}
h1{color:#0078b4;margin-bottom:12px;font-size:24px}
p{color:#5aaad0;line-height:1.8;font-size:13px}code{color:#0088cc;background:rgba(0,136,204,0.1);padding:2px 6px;border-radius:4px}</style>
</head><body><h1>SongSender</h1>
<p>Web UI not found.<br>
Add <code>songsender_web_ui.html</code> as a BinaryData resource in Projucer,<br>
or place it next to the .vst3 file.<br><br>
<a href="/api/files" style="color:#0078b4">View raw JSON</a></p>
</body></html>)HTML";
}

// ─── State persistence ────────────────────────────────────────────────────────
void SongSenderProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ScopedLock lock(filesLock);
    juce::MemoryOutputStream s(destData, true);
    s.writeInt(3); // version
    s.writeString(watchFolder.getFullPathName());
    s.writeBool(watchEnabled);
    // v3: editor settings
    s.writeInt(maxFilesToKeep);
    s.writeBool(autoAnalyse);
    s.writeBool(showLufsBadge);
    s.writeBool(compactRows);
    s.writeInt(serverPort);
    // file list
    s.writeInt((int)files.size());
    for (auto& rec : files)
    {
        s.writeString(rec.file.getFullPathName());
        s.writeString(rec.folder);
    }
}

void SongSenderProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream s(data, (size_t)sizeInBytes, false);
    int version = s.readInt();
    if (version >= 2)
    {
        watchFolder  = juce::File(s.readString());
        watchEnabled = s.readBool();
    }
    if (version >= 3)
    {
        maxFilesToKeep = s.readInt();
        autoAnalyse    = s.readBool();
        showLufsBadge  = s.readBool();
        compactRows    = s.readBool();
        serverPort     = s.readInt();
    }
    int count = s.readInt();
    for (int i = 0; i < count; ++i)
    {
        juce::File f(s.readString());
        juce::String folder = (version >= 2) ? s.readString() : "Uploads";
        if (f.existsAsFile()) addFile(f, folder);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SongSenderProcessor();
}
