#pragma once
#include <JuceHeader.h>

struct AudioAnalysis
{
    // ── Loudness (ITU-R BS.1770-4) ────────────────────────────────────────────
    float integratedLUFS    = -99.0f;   // Gated integrated loudness
    float truePeak          = -99.0f;   // True inter-sample peak (dBTP), 4x oversampled
    float dynamicRange      = 0.0f;     // EBU-style DR score (LU)
    float rmsLeft           = -99.0f;   // RMS dBFS left channel
    float rmsRight          = -99.0f;   // RMS dBFS right channel
    float shortTermLUFS     = -99.0f;   // Loudest 3s window

    // ── Stereo ────────────────────────────────────────────────────────────────
    float stereoWidth       = 0.0f;     // 0–100 %
    float phaseCorrelation  = 1.0f;     // −1 to +1
    float dcOffsetLeft      = 0.0f;     // linear fraction
    float dcOffsetRight     = 0.0f;
    bool  isMono            = false;

    // ── Dynamics ──────────────────────────────────────────────────────────────
    bool  hasClipping       = false;
    int   clippingCount     = 0;        // number of distinct clip events
    bool  hasLimiting       = false;
    float limitingAmount    = 0.0f;     // estimated dB of gain reduction

    // ── Spectrum (10 octave bands, 31Hz – 16kHz) ──────────────────────────────
    float frequencyBands[16] = {};      // 0–100 normalised for display

    // ── Format ────────────────────────────────────────────────────────────────
    double sampleRate       = 44100.0;
    int    bitDepth         = 24;
    double duration         = 0.0;      // seconds
    int    channels         = 2;
    juce::int64 fileSizeBytes = 0;

    // ── Tags ──────────────────────────────────────────────────────────────────
    juce::String transientDensity;
    juce::String bassEnergy;
    juce::String midEnergy;
    juce::String highEnergy;
};

// ─────────────────────────────────────────────────────────────────────────────
class AudioAnalyzer
{
public:
    AudioAnalysis analyze(const juce::File& audioFile);

private:
    // Loudness
    float computeIntegratedLUFS (const juce::AudioBuffer<float>& buf, double sr);
    float computeShortTermLUFS  (const juce::AudioBuffer<float>& buf, double sr);
    float computeTruePeak       (const juce::AudioBuffer<float>& buf);
    float computeDynamicRange   (const juce::AudioBuffer<float>& buf, double sr);
    float computeRMS            (const juce::AudioBuffer<float>& buf, int ch);

    // K-weighting filter (ITU-R BS.1770) applied in-place to a copy
    void  applyKWeighting       (juce::AudioBuffer<float>& buf, double sr);

    // Stereo
    float computeStereoWidth    (const juce::AudioBuffer<float>& buf);
    float computePhaseCorrelation(const juce::AudioBuffer<float>& buf);
    float computeDCOffset       (const juce::AudioBuffer<float>& buf, int ch);

    // Dynamics
    int   countClips            (const juce::AudioBuffer<float>& buf,
                                  float threshold = 0.99985f); // −0.13 dBFS
    float estimateLimitingAmount(const juce::AudioBuffer<float>& buf, double sr);

    // Spectrum
    void  computeFrequencyBands (const juce::AudioBuffer<float>& buf, double sr,
                                  float* bandsOut, int numBands);

    // Tags
    juce::String classifyTransients(const juce::AudioBuffer<float>& buf, double sr);
    juce::String classifyBand(float relativeEnergy, float low, float high);
};
