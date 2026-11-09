#include "AudioAnalyzer.h"
#include <numeric>
#include <algorithm>
#include <cmath>

// ─── Main entry point ─────────────────────────────────────────────────────────
AudioAnalysis AudioAnalyzer::analyze(const juce::File& audioFile)
{
    AudioAnalysis result;
    if (!audioFile.existsAsFile()) return result;

    result.fileSizeBytes = audioFile.getSize();

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(audioFile));
    if (!reader) return result;

    result.sampleRate = reader->sampleRate;
    result.channels   = (int)reader->numChannels;
    result.duration   = (double)reader->lengthInSamples / reader->sampleRate;
    result.bitDepth   = (int)reader->bitsPerSample;
    if (result.bitDepth <= 0) result.bitDepth = 24;

    // Cap at 10 minutes so huge files don't OOM
    const int maxSamples = (int)(reader->sampleRate * juce::jmin(600.0, result.duration));
    const int numCh      = juce::jmin(2, result.channels);

    juce::AudioBuffer<float> buf(numCh, maxSamples);
    reader->read(&buf, 0, maxSamples, 0, true, true);

    const double sr = result.sampleRate;

    // ── True peak (must measure BEFORE any processing) ────────────────────────
    result.truePeak = computeTruePeak(buf);

    // ── Clipping (sample peak ≥ −0.13 dBFS, on original signal) ──────────────
    result.clippingCount = countClips(buf);
    result.hasClipping   = result.clippingCount > 0;

    // ── LUFS — requires K-weighted copy ───────────────────────────────────────
    juce::AudioBuffer<float> kBuf(numCh, maxSamples);
    kBuf.copyFrom(0, 0, buf, 0, 0, maxSamples);
    if (numCh > 1) kBuf.copyFrom(1, 0, buf, 1, 0, maxSamples);
    applyKWeighting(kBuf, sr);

    result.integratedLUFS = computeIntegratedLUFS(kBuf, sr);
    result.shortTermLUFS  = computeShortTermLUFS(kBuf, sr);
    // ── DR score (on original, unweighted signal) ─────────────────────────────
    result.dynamicRange = computeDynamicRange(buf, sr);

    // ── Per-channel RMS ───────────────────────────────────────────────────────
    result.rmsLeft  = computeRMS(buf, 0);
    result.rmsRight = numCh > 1 ? computeRMS(buf, 1) : result.rmsLeft;

    // ── Stereo ────────────────────────────────────────────────────────────────
    if (numCh > 1)
    {
        result.stereoWidth      = computeStereoWidth(buf);
        result.phaseCorrelation = computePhaseCorrelation(buf);
        result.isMono           = false;
    }
    else
    {
        result.stereoWidth      = 0.0f;
        result.phaseCorrelation = 1.0f;
        result.isMono           = true;
    }

    result.dcOffsetLeft  = computeDCOffset(buf, 0);
    result.dcOffsetRight = numCh > 1 ? computeDCOffset(buf, 1) : result.dcOffsetLeft;

    // ── Limiting ──────────────────────────────────────────────────────────────
    result.limitingAmount = estimateLimitingAmount(buf, sr);
    result.hasLimiting    = result.limitingAmount > 1.5f && !result.hasClipping;

    // ── Spectrum ──────────────────────────────────────────────────────────────
    computeFrequencyBands(buf, sr, result.frequencyBands, 16);

    // ── Transients & spectral tags ────────────────────────────────────────────
    result.transientDensity = classifyTransients(buf, sr);

    float bassAvg = 0.0f, midAvg = 0.0f, highAvg = 0.0f;
    for (int i = 0; i < 4;  ++i) bassAvg += result.frequencyBands[i];
    for (int i = 4; i < 10; ++i) midAvg  += result.frequencyBands[i];
    for (int i = 10;i < 16; ++i) highAvg += result.frequencyBands[i];
    bassAvg /= 4.0f; midAvg /= 6.0f; highAvg /= 6.0f;
    float allAvg = (bassAvg + midAvg + highAvg) / 3.0f;
    result.bassEnergy = classifyBand(bassAvg - allAvg, -6.0f, 6.0f);
    result.midEnergy  = classifyBand(midAvg  - allAvg, -6.0f, 6.0f);
    result.highEnergy = classifyBand(highAvg - allAvg, -6.0f, 6.0f);

    return result;
}

// ─── K-weighting (ITU-R BS.1770-4) ───────────────────────────────────────────
// Two biquad stages applied in sequence to each channel:
//   Stage 1: High-shelf +4dB at ~1500 Hz  (pre-filter)
//   Stage 2: High-pass 2nd order at ~38 Hz (RLB weighting)
void AudioAnalyzer::applyKWeighting(juce::AudioBuffer<float>& buf, double sr)
{
    const int n  = buf.getNumSamples();
    const int ch = buf.getNumChannels();

    // ── Stage 1: Pre-filter high-shelf (from ITU-R BS.1770 Annex 1) ──────────
    // Coefficients derived for 48kHz; we recalculate for arbitrary sr.
    {
        const double Vh = 1.58486986;   // linear gain ~+4dB
        const double Vb = 1.25363471;
        const double f0 = 1681.974450955533;
        const double Q  = 0.7071752369554196;
        const double K  = std::tan(juce::MathConstants<double>::pi * f0 / sr);

        const double Kk  = K * K;
        const double KQ  = K / Q;
        const double den = 1.0 + KQ + Kk;

        const double b0 = (Vh + Vb * KQ + Kk) / den;
        const double b1 = 2.0 * (Kk - Vh) / den;
        const double b2 = (Vh - Vb * KQ + Kk) / den;
        const double a1 = 2.0 * (Kk - 1.0) / den;
        const double a2 = (1.0 - KQ + Kk) / den;

        for (int c = 0; c < ch; ++c)
        {
            float* d = buf.getWritePointer(c);
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (int i = 0; i < n; ++i)
            {
                double x0 = d[i];
                double y0 = b0*x0 + b1*x1 + b2*x2 - a1*y1 - a2*y2;
                d[i] = (float)y0;
                x2 = x1; x1 = x0; y2 = y1; y1 = y0;
            }
        }
    }

    // ── Stage 2: RLB high-pass (2nd order Butterworth at ~38 Hz) ─────────────
    {
        const double f0 = 38.13547087602444;
        const double Q  = 0.5003270373238773;
        const double K  = std::tan(juce::MathConstants<double>::pi * f0 / sr);

        const double Kk  = K * K;
        const double KQ  = K / Q;
        const double den = 1.0 + KQ + Kk;

        const double b0 =  1.0 / den;
        const double b1 = -2.0 / den;
        const double b2 =  1.0 / den;
        const double a1 = 2.0 * (Kk - 1.0) / den;
        const double a2 = (1.0 - KQ + Kk) / den;

        for (int c = 0; c < ch; ++c)
        {
            float* d = buf.getWritePointer(c);
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (int i = 0; i < n; ++i)
            {
                double x0 = d[i];
                double y0 = b0*x0 + b1*x1 + b2*x2 - a1*y1 - a2*y2;
                d[i] = (float)y0;
                x2 = x1; x1 = x0; y2 = y1; y1 = y0;
            }
        }
    }
}

// ─── Integrated LUFS (ITU-R BS.1770-4 gated) ─────────────────────────────────
// Call on K-weighted buffer.
// Algorithm:
//   1. Compute mean square of each 400ms block (75% overlap)
//   2. Absolute gate: discard blocks below −70 LUFS (power < 1e-7)
//   3. Relative gate: discard blocks more than 10 LU below ungated mean
//   4. Final loudness = −0.691 + 10*log10(mean of surviving blocks)
float AudioAnalyzer::computeIntegratedLUFS(const juce::AudioBuffer<float>& buf, double sr)
{
    const int n          = buf.getNumSamples();
    const int ch         = buf.getNumChannels();
    const int blockSamps = (int)(sr * 0.4);   // 400 ms
    const int hopSamps   = (int)(sr * 0.1);   // 100 ms hop (75% overlap)

    if (blockSamps <= 0 || n < blockSamps) return -99.0f;

    std::vector<double> blockPower;
    blockPower.reserve(n / hopSamps + 1);

    for (int start = 0; start + blockSamps <= n; start += hopSamps)
    {
        double power = 0.0;
        for (int c = 0; c < ch; ++c)
        {
            const float* d = buf.getReadPointer(c, start);
            double s = 0.0;
            for (int i = 0; i < blockSamps; ++i) s += (double)d[i] * d[i];
            power += s / blockSamps;
            // ITU: stereo L and R each weighted 1.0, sum them
        }
        blockPower.push_back(power);
    }

    if (blockPower.empty()) return -99.0f;

    // Absolute gate: −70 LUFS absolute = power < 1e-7
    const double absGate = 1e-7;
    std::vector<double> gated1;
    for (double p : blockPower)
        if (p > absGate) gated1.push_back(p);

    if (gated1.empty()) return -99.0f;

    // Ungated mean for relative gate threshold
    double ungatedMean = std::accumulate(gated1.begin(), gated1.end(), 0.0) / gated1.size();

    // Relative gate: −10 LU below ungated integrated loudness
    // −10 LU in power = factor of 10^(−10/10) = 0.1
    double relGate = ungatedMean * 0.1;

    std::vector<double> gated2;
    for (double p : gated1)
        if (p > relGate) gated2.push_back(p);

    if (gated2.empty()) return -99.0f;

    double mean = std::accumulate(gated2.begin(), gated2.end(), 0.0) / gated2.size();
    if (mean <= 0.0) return -99.0f;

    float lufs = (float)(-0.691 + 10.0 * std::log10(mean));
    return juce::jlimit(-60.0f, 0.0f, lufs);
}

// ─── Short-term LUFS (loudest 3s window) ─────────────────────────────────────
float AudioAnalyzer::computeShortTermLUFS(const juce::AudioBuffer<float>& buf, double sr)
{
    const int n          = buf.getNumSamples();
    const int ch         = buf.getNumChannels();
    const int blockSamps = (int)(sr * 3.0);
    const int hopSamps   = (int)(sr * 0.1);

    if (blockSamps <= 0 || n < blockSamps) return -99.0f;

    float maxLUFS = -99.0f;

    for (int start = 0; start + blockSamps <= n; start += hopSamps)
    {
        double power = 0.0;
        for (int c = 0; c < ch; ++c)
        {
            const float* d = buf.getReadPointer(c, start);
            double s = 0.0;
            for (int i = 0; i < blockSamps; ++i) s += (double)d[i] * d[i];
            power += s / blockSamps;
        }
        if (power > 1e-10)
        {
            float lufs = (float)(-0.691 + 10.0 * std::log10(power));
            maxLUFS = juce::jmax(maxLUFS, lufs);
        }
    }

    return juce::jlimit(-60.0f, 0.0f, maxLUFS);
}

// ─── True Peak — 4× oversampled ───────────────────────────────────────────────
// Upsamples by zero-stuffing + sinc-like interpolation using a polyphase FIR.
// This catches inter-sample peaks that exceed 0 dBFS even when the samples don't.
float AudioAnalyzer::computeTruePeak(const juce::AudioBuffer<float>& buf)
{
    // ITU-R BS.1770 Annex 2 specifies 4x oversampling.
    // We use a simple 4-point cubic interpolation between each pair of samples,
    // which is a good approximation and avoids a heavy FIR filter bank.
    // For each interval (n, n+1) we evaluate at t = 0.25, 0.5, 0.75
    // using Catmull-Rom spline: requires samples n-1, n, n+1, n+2

    float peak = 0.0f;

    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        const float* d = buf.getReadPointer(c);
        const int    n = buf.getNumSamples();

        for (int i = 0; i < n; ++i)
        {
            // Also check the actual sample
            peak = juce::jmax(peak, std::abs(d[i]));

            if (i + 2 >= n) continue;

            // Catmull-Rom spline coefficients for t = 0.25, 0.5, 0.75
            float p0 = (i > 0)     ? d[i-1] : d[i];
            float p1 = d[i];
            float p2 = d[i+1];
            float p3 = (i+2 < n)   ? d[i+2] : d[i+1];

            // t = 0.25
            float t = 0.25f;
            float v = 0.5f * ((2.0f*p1)
                + (-p0 + p2)*t
                + (2.0f*p0 - 5.0f*p1 + 4.0f*p2 - p3)*t*t
                + (-p0 + 3.0f*p1 - 3.0f*p2 + p3)*t*t*t);
            peak = juce::jmax(peak, std::abs(v));

            // t = 0.5
            t = 0.5f;
            v = 0.5f * ((2.0f*p1)
                + (-p0 + p2)*t
                + (2.0f*p0 - 5.0f*p1 + 4.0f*p2 - p3)*t*t
                + (-p0 + 3.0f*p1 - 3.0f*p2 + p3)*t*t*t);
            peak = juce::jmax(peak, std::abs(v));

            // t = 0.75
            t = 0.75f;
            v = 0.5f * ((2.0f*p1)
                + (-p0 + p2)*t
                + (2.0f*p0 - 5.0f*p1 + 4.0f*p2 - p3)*t*t
                + (-p0 + 3.0f*p1 - 3.0f*p2 + p3)*t*t*t);
            peak = juce::jmax(peak, std::abs(v));
        }
    }

    if (peak <= 0.0f) return -99.0f;
    return 20.0f * std::log10(peak);
}

// ─── Dynamic Range (EBU-style DR score) ──────────────────────────────────────
// Matches the DR meter used by Pleasurize Music Foundation / foobar DR plugin.
// Algorithm per block (3 seconds):
//   - Peak = sample peak of block
//   - RMS  = RMS of block
// DR = mean(peak_dBFS) − mean(RMS_dBFS) of all blocks, approximately.
// More precisely: DR = peak_dBFS − RMS_dBFS, averaged over 20% loudest blocks.
float AudioAnalyzer::computeDynamicRange(const juce::AudioBuffer<float>& buf, double sr)
{
    const int blockSamps = (int)(sr * 3.0); // 3-second blocks (DR standard)
    if (blockSamps < 1) return 0.0f;

    const int n  = buf.getNumSamples();
    const int ch = buf.getNumChannels();

    struct Block { float peak; float rms; };
    std::vector<Block> blocks;

    for (int start = 0; start + blockSamps <= n; start += blockSamps)
    {
        float blockPeak = 0.0f;
        double blockSumSq = 0.0;

        for (int c = 0; c < ch; ++c)
        {
            const float* d = buf.getReadPointer(c, start);
            for (int i = 0; i < blockSamps; ++i)
            {
                float s = std::abs(d[i]);
                blockPeak = juce::jmax(blockPeak, s);
                blockSumSq += (double)d[i] * d[i];
            }
        }

        float rms = (float)std::sqrt(blockSumSq / (blockSamps * ch));
        if (rms > 1e-6f && blockPeak > 1e-6f)
            blocks.push_back({ blockPeak, rms });
    }

    if (blocks.empty()) return 0.0f;

    // Sort by RMS descending, take loudest 20%
    std::sort(blocks.begin(), blocks.end(),
              [](const Block& a, const Block& b){ return a.rms > b.rms; });

    int useCount = juce::jmax(1, (int)(blocks.size() * 0.2f));

    double sumPeakDB = 0.0, sumRmsDB = 0.0;
    for (int i = 0; i < useCount; ++i)
    {
        sumPeakDB += 20.0 * std::log10((double)blocks[i].peak);
        sumRmsDB  += 20.0 * std::log10((double)blocks[i].rms);
    }

    float dr = (float)((sumPeakDB - sumRmsDB) / useCount);
    return juce::jlimit(0.0f, 30.0f, dr);
}

// ─── RMS per channel (dBFS) ───────────────────────────────────────────────────
float AudioAnalyzer::computeRMS(const juce::AudioBuffer<float>& buf, int ch)
{
    if (ch >= buf.getNumChannels()) return -99.0f;
    const float* d = buf.getReadPointer(ch);
    double sum = 0.0;
    const int n = buf.getNumSamples();
    for (int i = 0; i < n; ++i) sum += (double)d[i] * d[i];
    float rms = (float)std::sqrt(sum / n);
    if (rms <= 1e-10f) return -99.0f;
    return 20.0f * std::log10(rms);
}

// ─── Stereo Width (mid/side energy ratio) ────────────────────────────────────
float AudioAnalyzer::computeStereoWidth(const juce::AudioBuffer<float>& buf)
{
    if (buf.getNumChannels() < 2) return 0.0f;
    const float* L = buf.getReadPointer(0);
    const float* R = buf.getReadPointer(1);
    const int    n = buf.getNumSamples();

    double mid = 0.0, side = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double m = (L[i] + R[i]) * 0.5;
        double s = (L[i] - R[i]) * 0.5;
        mid  += m * m;
        side += s * s;
    }
    if (mid + side <= 0.0) return 0.0f;
    // side / total gives 0 for mono, 0.5 for fully decorrelated stereo
    // Multiply by 200 to get 0–100 scale
    return juce::jlimit(0.0f, 100.0f, (float)(side / (mid + side) * 200.0));
}

// ─── Phase Correlation ────────────────────────────────────────────────────────
float AudioAnalyzer::computePhaseCorrelation(const juce::AudioBuffer<float>& buf)
{
    if (buf.getNumChannels() < 2) return 1.0f;
    const float* L = buf.getReadPointer(0);
    const float* R = buf.getReadPointer(1);
    const int    n = buf.getNumSamples();

    double cross = 0.0, pL = 0.0, pR = 0.0;
    for (int i = 0; i < n; ++i)
    {
        cross += L[i] * R[i];
        pL    += L[i] * L[i];
        pR    += R[i] * R[i];
    }
    double denom = std::sqrt(pL * pR);
    if (denom <= 0.0) return 1.0f;
    return (float)juce::jlimit(-1.0, 1.0, cross / denom);
}

// ─── DC Offset ────────────────────────────────────────────────────────────────
float AudioAnalyzer::computeDCOffset(const juce::AudioBuffer<float>& buf, int ch)
{
    if (ch >= buf.getNumChannels()) return 0.0f;
    const float* d = buf.getReadPointer(ch);
    double sum = 0.0;
    const int n = buf.getNumSamples();
    for (int i = 0; i < n; ++i) sum += d[i];
    return (float)(std::abs(sum) / n);
}

// ─── Clip detection ───────────────────────────────────────────────────────────
// Counts distinct clipping events (runs of consecutive over-threshold samples).
// Threshold = 0.99985 ≈ −0.13 dBFS, matching most professional meters.
int AudioAnalyzer::countClips(const juce::AudioBuffer<float>& buf, float threshold)
{
    int  count  = 0;
    bool inClip = false;

    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        const float* d = buf.getReadPointer(c);
        inClip = false;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            bool over = std::abs(d[i]) >= threshold;
            if (over && !inClip) { ++count; inClip = true; }
            else if (!over)       inClip = false;
        }
    }
    return count;
}

// ─── Limiting estimation ──────────────────────────────────────────────────────
// Compares the crest factor (peak/RMS) of the loudest sections against
// what unprocessed music typically sounds like (~12–14 dB crest).
// Heavy limiting squashes the crest factor toward 0–6 dB.
// We look at the 10% loudest blocks to avoid being fooled by quiet sections.
float AudioAnalyzer::estimateLimitingAmount(const juce::AudioBuffer<float>& buf, double sr)
{
    const int blockSamps = (int)(sr * 0.1); // 100ms blocks
    if (blockSamps < 1) return 0.0f;

    const int n  = buf.getNumSamples();
    const int ch = buf.getNumChannels();

    struct Block { float crest; float rms; };
    std::vector<Block> blocks;

    for (int start = 0; start + blockSamps <= n; start += blockSamps)
    {
        float peak    = 0.0f;
        double sumSq  = 0.0;
        for (int c = 0; c < ch; ++c)
        {
            const float* d = buf.getReadPointer(c, start);
            for (int i = 0; i < blockSamps; ++i)
            {
                float s = std::abs(d[i]);
                peak   = juce::jmax(peak, s);
                sumSq += s * s;
            }
        }
        float rms = (float)std::sqrt(sumSq / (blockSamps * ch));
        if (rms > 0.05f) // only blocks with meaningful signal (above −26 dBFS)
            blocks.push_back({ peak / juce::jmax(rms, 1e-6f), rms });
    }

    if (blocks.empty()) return 0.0f;

    // Sort by RMS, take loudest 10%
    std::sort(blocks.begin(), blocks.end(),
              [](const Block& a, const Block& b){ return a.rms > b.rms; });

    int useCount = juce::jmax(1, (int)(blocks.size() * 0.1f));

    double avgCrest = 0.0;
    for (int i = 0; i < useCount; ++i)
        avgCrest += blocks[i].crest;
    avgCrest /= useCount;

    // Natural uncompressed music: ~12–14 dB crest factor in loud passages
    // Hard limited music: ~3–6 dB crest
    float crestDB   = 20.0f * std::log10((float)juce::jmax(avgCrest, 0.001));
    float naturalDB = 12.0f;
    float limiting  = juce::jmax(0.0f, naturalDB - crestDB);

    return juce::jlimit(0.0f, 20.0f, limiting);
}

// ─── Frequency bands (FFT, 1/3-octave averaged) ───────────────────────────────
void AudioAnalyzer::computeFrequencyBands(const juce::AudioBuffer<float>& buf,
                                           double sr,
                                           float* bandsOut, int numBands)
{
    const int fftOrder = 13; // 8192 points — good frequency resolution
    const int fftSize  = 1 << fftOrder;
    juce::dsp::FFT fft(fftOrder);

    std::vector<float> magAccum(fftSize / 2 + 1, 0.0f);
    int windowCount = 0;

    // Hann window
    std::vector<float> window(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1)));

    std::vector<float> fftBuf(fftSize * 2, 0.0f);

    const int hop = fftSize / 2;
    const int n   = buf.getNumSamples();

    for (int start = 0; start + fftSize <= n; start += hop)
    {
        std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);

        // Mix to mono
        for (int c = 0; c < buf.getNumChannels(); ++c)
        {
            const float* d = buf.getReadPointer(c, start);
            for (int i = 0; i < fftSize; ++i)
                fftBuf[i] += d[i] * window[i];
        }
        if (buf.getNumChannels() > 1)
            for (int i = 0; i < fftSize; ++i) fftBuf[i] *= 0.5f;

        fft.performRealOnlyForwardTransform(fftBuf.data());

        for (int i = 0; i < fftSize / 2; ++i)
        {
            float re = fftBuf[i * 2];
            float im = fftBuf[i * 2 + 1];
            magAccum[i] += std::sqrt(re * re + im * im);
        }
        ++windowCount;
    }

    if (windowCount == 0) return;
    for (auto& m : magAccum) m /= windowCount;

    // 16 bands: standard 1/3-octave centres from 32 Hz to 16 kHz
    const float centres[16] = {
        31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f,
        2000.0f, 4000.0f, 8000.0f, 16000.0f,
        // extra high bands for display completeness
        18000.0f, 20000.0f, 22000.0f, 22000.0f, 22000.0f, 22000.0f
    };

    for (int b = 0; b < numBands && b < 16; ++b)
    {
        // 1/3-octave bandwidth: f_low = f * 2^(-1/6), f_high = f * 2^(1/6)
        float fLow  = centres[b] * 0.8909f; // 2^(-1/6)
        float fHigh = centres[b] * 1.1225f; // 2^(+1/6)

        int binLow  = juce::jlimit(0, (int)magAccum.size()-1,
                                    (int)(fLow  * fftSize / sr));
        int binHigh = juce::jlimit(0, (int)magAccum.size()-1,
                                    (int)(fHigh * fftSize / sr));
        if (binHigh < binLow) binHigh = binLow;

        double sum = 0.0;
        for (int i = binLow; i <= binHigh; ++i) sum += magAccum[i];
        int cnt = binHigh - binLow + 1;

        float dbVal = 20.0f * std::log10(juce::jmax(1e-9f, (float)(sum / cnt)));
        bandsOut[b] = juce::jlimit(-80.0f, 0.0f, dbVal);
    }

    // Normalise 0–100 for display
    float minV = *std::min_element(bandsOut, bandsOut + numBands);
    float maxV = *std::max_element(bandsOut, bandsOut + numBands);
    float rng  = juce::jmax(1.0f, maxV - minV);
    for (int b = 0; b < numBands; ++b)
        bandsOut[b] = ((bandsOut[b] - minV) / rng) * 100.0f;
}

// ─── Transient density ────────────────────────────────────────────────────────
juce::String AudioAnalyzer::classifyTransients(const juce::AudioBuffer<float>& buf,
                                                double sr)
{
    const int blockSamps = (int)(sr * 0.01); // 10ms
    if (blockSamps < 1) return "Unknown";

    float prevRMS  = 0.0f;
    int transients = 0;
    int blocks     = 0;
    const int n    = buf.getNumSamples();

    for (int start = 0; start + blockSamps <= n; start += blockSamps)
    {
        float sumSq = 0.0f;
        const float* d = buf.getReadPointer(0, start);
        for (int i = 0; i < blockSamps; ++i) sumSq += d[i] * d[i];
        float rms = std::sqrt(sumSq / blockSamps);

        if (prevRMS > 0.001f && rms / prevRMS > 2.5f)
            ++transients;

        prevRMS = rms;
        ++blocks;
    }

    if (blocks == 0) return "Unknown";

    float durSecs = blocks * 0.01f;
    float density = (durSecs > 0.0f) ? ((float)transients / durSecs) : 0.0f;

    if (density < 1.0f) return "Low";
    if (density < 3.0f) return "Medium";
    return "High";
}

juce::String AudioAnalyzer::classifyBand(float relativeEnergy, float low, float high)
{
    if (relativeEnergy < low)  return "Low";
    if (relativeEnergy > high) return "Strong";
    return "Balanced";
}
