#pragma once
#include <JuceHeader.h>
#include "AudioAnalyzer.h"

// ── FileRecord ────────────────────────────────────────────────────────────────
struct FileRecord
{
    juce::String  id;
    juce::File    file;
    juce::int64   timestamp  { 0 };
    juce::String  folder;          // user-assigned folder tag, e.g. "Snippets"
    bool          isAutoWatch { false }; // came from the watch folder
    AudioAnalysis analysis;
};

// ─────────────────────────────────────────────────────────────────────────────
class SongSenderProcessor  : public juce::AudioProcessor,
                              private juce::Timer          // watch-folder polling
{
public:
    SongSenderProcessor();
    ~SongSenderProcessor() override;

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SongSender"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── Public API ────────────────────────────────────────────────────────────
    void addFile(const juce::File& f, const juce::String& folder = "Uploads");
    juce::String getURL() const;
    const std::vector<FileRecord>& getFiles() const { return files; }

    // Watch folder (Last Exports) ─────────────────────────────────────────────
    void          setWatchFolder(const juce::File& dir);
    juce::File    getWatchFolder() const  { return watchFolder; }
    bool          isWatchEnabled() const  { return watchEnabled; }
    void          setWatchEnabled(bool e);

    // Callbacks
    std::function<void()> onFilesChanged;
    int  serverPort    = 8765;

    // ── Persistent editor settings (owned here so they survive editor close) ──
    int  maxFilesToKeep   = 50;    // enforced in addFile()
    bool autoAnalyse      = true;  // analyse on drop
    bool showLufsBadge    = true;  // show LUFS in file rows
    bool compactRows      = false; // smaller row height


private:
    // ── File store ────────────────────────────────────────────────────────────
    std::vector<FileRecord> files;
    AudioAnalyzer           analyzer;
    juce::CriticalSection   filesLock;

    // ── Watch folder ──────────────────────────────────────────────────────────
    juce::File              watchFolder;
    bool                    watchEnabled { false };
    juce::StringArray       seenByWatcher;          // paths already ingested
    void timerCallback() override;                  // polls watchFolder every 3s
    void scanWatchFolder();

    // ── HTTP Server ───────────────────────────────────────────────────────────
    juce::StreamingSocket   serverSocket;
    std::atomic<bool>       serverRunning { false };

    void startServer();
    void stopServer();
    void handleClient(juce::StreamingSocket* client);
    void serveRequest(juce::StreamingSocket* client,
                      const juce::String& method,
                      const juce::String& path,
                      const juce::String& fullRequest);

    void sendAudioFileStream  (juce::StreamingSocket* client,
                               const juce::File& file,
                               const juce::String& mime,
                               const juce::String& req);
    void sendAudioFileDownload(juce::StreamingSocket* client,
                               const juce::File& file,
                               const juce::String& mime,
                               const juce::String& filename);
    void sendHTTPResponse     (juce::StreamingSocket* client,
                               int status,
                               const juce::String& contentType,
                               const juce::String& body);
    void sendHTTPResponseHeadersOnly(juce::StreamingSocket* client,
                                     int status,
                                     const juce::String& contentType,
                                     juce::int64 contentLength,
                                     const juce::String& extraHeaders = {});

    juce::String buildFilesJSON();
    juce::String buildIndexHTML();
    juce::String getLocalIP() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongSenderProcessor)
};
