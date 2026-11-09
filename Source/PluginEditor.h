#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "QRCodeGenerator.h"

// ─── Colour palette (Frutiger Aero) ──────────────────────────────────────────
namespace AeroCol
{
    inline juce::Colour skyTop()   { return juce::Colour(0xffcceeff); }
    inline juce::Colour skyBot()   { return juce::Colour(0xfff5fbff); }
    inline juce::Colour glass()    { return juce::Colour(0xf5f8fcff); }
    inline juce::Colour gloss()    { return juce::Colour(0x80ffffff); }
    inline juce::Colour blue()     { return juce::Colour(0xff0078b4); }
    inline juce::Colour blueMid()  { return juce::Colour(0xff3aaae0); }
    inline juce::Colour bluePale() { return juce::Colour(0xffd0eefa); }
    inline juce::Colour text()     { return juce::Colour(0xff1a3a52); }
    inline juce::Colour muted()    { return juce::Colour(0xff7ab8d0); }
    inline juce::Colour green()    { return juce::Colour(0xff009966); }
    inline juce::Colour amber()    { return juce::Colour(0xffe09020); }
    inline juce::Colour red()      { return juce::Colour(0xffd04060); }
}

// ─────────────────────────────────────────────────────────────────────────────
// FileRowComponent
// ─────────────────────────────────────────────────────────────────────────────
class FileRowComponent : public juce::Component
{
public:
    std::function<void()> onClick;

    void setRecord (const FileRecord& rec, bool showLufs = true)
    {
        recId    = rec.id;
        filename = rec.file.getFileName();
        ext      = rec.file.getFileExtension().trimCharactersAtStart(".").toUpperCase();
        folder   = rec.folder;
        isAuto   = rec.isAutoWatch;
        analysis = rec.analysis;

        float sizeKB = (float)rec.file.getSize() / 1024.0f;
        sizeStr = sizeKB > 1024.0f
            ? juce::String(sizeKB / 1024.0f, 1) + " MB"
            : juce::String((int)sizeKB) + " KB";

        juce::int64 diffMs = juce::Time::currentTimeMillis() - rec.timestamp;
        int diffMin = (int)(diffMs / 60000);
        timeStr = diffMin < 1   ? "just now"
                : diffMin < 60  ? juce::String(diffMin) + "m ago"
                                : juce::String(diffMin / 60) + "h ago";

        float lufs = rec.analysis.integratedLUFS;
        lufsColor = lufs > -9.0f  ? AeroCol::red()
                  : lufs > -14.0f ? AeroCol::amber()
                                  : AeroCol::green();
        lufsStr = juce::String(lufs, 1) + " LU";
        showLufsBadge = showLufs;
        repaint();
    }

    void setActive (bool a) { active = a; repaint(); }

    const juce::String&   getId()       const { return recId;    }
    const AudioAnalysis&  getAnalysis() const { return analysis; }
    const juce::String&   getFilename() const { return filename; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        juce::Colour topCol = active  ? juce::Colour(0xd8dceeff)
                            : hovered ? juce::Colour(0xe8f6ffff)
                                      : juce::Colour(0xf5fbffff);
        juce::Colour botCol = active  ? juce::Colour(0xc8c8e8ff)
                            : hovered ? juce::Colour(0xdaeffdff)
                                      : juce::Colour(0xedf6fcff);
        g.setGradientFill (juce::ColourGradient(topCol, 0.f, 0.f, botCol, 0.f, b.getHeight(), false));
        g.fillRoundedRectangle (b, 9.0f);

        g.setColour (juce::Colour(0xaaffffff));
        g.drawLine  (b.getX()+12.f, b.getY()+0.5f, b.getRight()-12.f, b.getY()+0.5f, 1.0f);

        g.setColour (active ? juce::Colour(0x600088cc) : juce::Colour(0x28006090));
        g.drawRoundedRectangle (b.reduced(0.5f), 9.0f, 0.8f);

        // Ext badge
        auto extBox = juce::Rectangle<float>(10.f, b.getHeight()/2.f - 16.f, 34.f, 32.f);
        g.setGradientFill (juce::ColourGradient(
            juce::Colour(0xffc8e8ff), extBox.getX(), extBox.getY(),
            juce::Colour(0xff90c8ee), extBox.getX(), extBox.getBottom(), false));
        g.fillRoundedRectangle (extBox, 7.0f);
        g.setColour (juce::Colour(0x40006090));
        g.drawRoundedRectangle (extBox.reduced(0.5f), 7.0f, 0.6f);
        g.setColour (AeroCol::blue());
        g.setFont   (juce::Font("Segoe UI", 8.0f, juce::Font::bold));
        g.drawText  (ext, extBox.toNearestInt(), juce::Justification::centred);

        // Filename + sub-row — adapt to row height
        // Compact (< 40px): single line, filename + meta inline, no folder pill
        // Normal (>= 40px): filename on top, folder pill + meta below
        const int h = getHeight();
        const bool compact = (h < 40);

        if (compact)
        {
            // Single centred line: filename
            g.setColour (AeroCol::text());
            g.setFont   (juce::Font("Segoe UI", 11.0f, juce::Font::bold));
            g.drawText  (filename, 52, 0, getWidth()-130, h, juce::Justification::centredLeft, true);

            // Meta right-aligned before LUFS
            g.setColour (AeroCol::muted());
            g.setFont   (juce::Font("Segoe UI", 9.0f, juce::Font::plain));
            g.drawText  (timeStr + " · " + sizeStr,
                         52, 0, getWidth()-130, h, juce::Justification::centredRight);
        }
        else
        {
            // Filename
            g.setColour (AeroCol::text());
            g.setFont   (juce::Font("Segoe UI", 12.5f, juce::Font::bold));
            g.drawText  (filename, 52, 5, getWidth()-130, 17, juce::Justification::centredLeft, true);

            // Folder tag pill
            juce::Font tagFont ("Segoe UI", 9.5f, juce::Font::bold);
            g.setFont (tagFont);
            float tagW   = tagFont.getStringWidth(folder) + 12.0f;
            auto  tagBox = juce::Rectangle<float>(52.f, 24.f, tagW, 14.f);
            auto  tagCol = isAuto ? juce::Colour(0xff0090a0) : AeroCol::blueMid();
            g.setColour (tagCol.withAlpha(0.16f));
            g.fillRoundedRectangle (tagBox, 7.0f);
            g.setColour (tagCol.withAlpha(0.40f));
            g.drawRoundedRectangle (tagBox.reduced(0.5f), 7.0f, 0.7f);
            g.setColour (tagCol);
            g.drawText  (folder, tagBox.toNearestInt(), juce::Justification::centred);

            // Meta
            g.setColour (AeroCol::muted());
            g.setFont   (juce::Font("Segoe UI", 10.0f, juce::Font::plain));
            int metaX = (int)(52 + tagW + 6);
            g.drawText  (timeStr + "  ·  " + sizeStr,
                         metaX, 25, getWidth()-metaX-80, 13,
                         juce::Justification::centredLeft);
        }

        // LUFS badge — only shown when enabled in settings
        if (showLufsBadge)
        {
            auto lufsBox = juce::Rectangle<float>((float)getWidth()-72.f, b.getHeight()/2.f-10.f, 56.f, 20.f);
            g.setColour (lufsColor.withAlpha(0.13f));
            g.fillRoundedRectangle (lufsBox, 10.0f);
            g.setColour (lufsColor.withAlpha(0.50f));
            g.drawRoundedRectangle (lufsBox.reduced(0.5f), 10.0f, 0.7f);
            g.setColour (lufsColor);
            g.setFont   (juce::Font("Segoe UI", 9.5f, juce::Font::bold));
            g.drawText  (lufsStr, lufsBox.toNearestInt(), juce::Justification::centred);
        }

        // Chevron
        g.setColour (AeroCol::blueMid());
        float cx = (float)getWidth()-10.f, cy = b.getCentreY();
        juce::Path arr;
        arr.startNewSubPath (cx-4.f, cy-4.f);
        arr.lineTo          (cx,     cy);
        arr.lineTo          (cx-4.f, cy+4.f);
        g.strokePath (arr, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    void mouseEnter (const juce::MouseEvent&) override { hovered=true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hovered=false; repaint(); }
    void mouseUp    (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick) onClick();
    }

private:
    juce::String  recId, filename, ext, sizeStr, timeStr, lufsStr, folder;
    juce::Colour  lufsColor { AeroCol::muted() };
    AudioAnalysis analysis;
    bool active=false, hovered=false, isAuto=false, showLufsBadge=true;
};

// ─────────────────────────────────────────────────────────────────────────────
// DropZone
// ─────────────────────────────────────────────────────────────────────────────
class DropZone : public juce::Component,
                 public juce::FileDragAndDropTarget
{
public:
    std::function<void(const juce::File&)> onFileDrop;
    std::function<void()>                  onBrowseClick;

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files)
            if (isAudioFile(f)) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging=true;  repaint(); }
    void fileDragExit  (const juce::StringArray&)           override { dragging=false; repaint(); }
    void filesDropped  (const juce::StringArray& files, int, int) override
    {
        dragging = false;
        for (auto& f : files)
            if (isAudioFile(f) && onFileDrop) onFileDrop(juce::File(f));
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onBrowseClick) onBrowseClick();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        if (dragging)
        {
            g.setGradientFill (juce::ColourGradient(
                juce::Colour(0xd0c8e8ff), 0.f, 0.f,
                juce::Colour(0xc8a8d8f8), 0.f, b.getHeight(), false));
            g.fillRoundedRectangle (b, 12.0f);
            g.setColour (juce::Colour(0x800080cc));
            g.drawRoundedRectangle (b.reduced(0.5f), 12.0f, 1.5f);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient(
                juce::Colour(0xf0f8feff), 0.f, 0.f,
                juce::Colour(0xe8f4fbff), 0.f, b.getHeight(), false));
            g.fillRoundedRectangle (b, 12.0f);

            juce::Path border;
            border.addRoundedRectangle (b.reduced(0.5f), 12.0f);
            float dashLen[] = {5.0f, 4.0f};
            juce::PathStrokeType(1.0f).createDashedStroke (border, border, dashLen, 2);
            g.setColour (juce::Colour(0x500088cc));
            g.fillPath  (border);
            g.setColour (juce::Colour(0x60ffffff));
            g.drawLine  (b.getX()+14.f, b.getY()+0.8f, b.getRight()-14.f, b.getY()+0.8f, 0.8f);
        }

        auto ic     = b.getCentre().translated (0.f, -10.f);
        auto barCol = dragging ? AeroCol::blue() : AeroCol::blueMid();
        for (int i = -3; i <= 3; ++i)
        {
            float h2 = std::abs(i)==0 ? 14.f : std::abs(i)==1 ? 10.f
                     : std::abs(i)==2 ? 7.f  : 4.f;
            g.setGradientFill (juce::ColourGradient(
                barCol.brighter(0.3f), 0.f, ic.y-h2*0.5f,
                barCol,               0.f, ic.y+h2*0.5f, false));
            g.fillRoundedRectangle (ic.x+(float)i*5.f-1.5f, ic.y-h2*0.5f, 3.f, h2, 1.5f);
        }
        g.setColour (dragging ? AeroCol::blue() : AeroCol::text());
        g.setFont   (juce::Font("Segoe UI", 12.0f, juce::Font::bold));
        g.drawText  ("Drop audio file here", b.withTrimmedTop(24.f), juce::Justification::centred);
        g.setColour (AeroCol::muted());
        g.setFont   (juce::Font("Segoe UI", 10.0f, juce::Font::plain));
        g.drawText  ("or click to browse",  b.withTrimmedTop(40.f), juce::Justification::centred);
    }

private:
    bool dragging = false;
    static bool isAudioFile (const juce::String& f)
    {
        auto e = juce::File(f).getFileExtension().toLowerCase();
        return e==".wav"||e==".aiff"||e==".aif"||e==".mp3"||e==".flac"||e==".ogg";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// FolderChip  –  clickable filter-chip, Frutiger Aero glass pill style
// ─────────────────────────────────────────────────────────────────────────────
class FolderChip : public juce::Component
{
public:
    std::function<void()> onClick;
    juce::String          label;
    bool                  active = false;
    bool                  isAuto = false;

    FolderChip() { setInterceptsMouseClicks(true, false); }

    void paint (juce::Graphics& g) override
    {
        auto b       = getLocalBounds().toFloat().reduced(1.0f);
        auto baseCol = isAuto ? juce::Colour(0xff008890) : AeroCol::blue();

        if (active)
        {
            g.setGradientFill (juce::ColourGradient(
                baseCol.brighter(0.25f), b.getX(), b.getY(),
                baseCol,                b.getX(), b.getBottom(), false));
            g.fillRoundedRectangle (b, 10.0f);
            g.setColour (juce::Colour(0x50ffffff));
            g.drawLine  (b.getX()+5.f, b.getY()+0.8f, b.getRight()-5.f, b.getY()+0.8f, 0.8f);
            g.setColour (juce::Colour(0x380050a0));
            g.drawRoundedRectangle (b.reduced(0.5f), 10.0f, 0.8f);
            g.setColour (juce::Colours::white);
        }
        else
        {
            g.setColour (hovered ? juce::Colour(0xffd8eefa) : juce::Colour(0xeef6fdff));
            g.fillRoundedRectangle (b, 10.0f);
            g.setColour (juce::Colour(0x50ffffff));
            g.drawLine  (b.getX()+5.f, b.getY()+0.8f, b.getRight()-5.f, b.getY()+0.8f, 0.8f);
            g.setColour (juce::Colour(0x500088cc));
            g.drawRoundedRectangle (b.reduced(0.5f), 10.0f, 0.8f);
            g.setColour (baseCol);
        }

        g.setFont  (juce::Font("Segoe UI", 10.0f, juce::Font::bold));
        g.drawText (label, b.toNearestInt(), juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent&) override { hovered=true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hovered=false; repaint(); }
    void mouseUp    (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick) onClick();
    }

private:
    bool hovered = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// AnalysisPanel  –  right-side detail view for the selected file
// ─────────────────────────────────────────────────────────────────────────────
class AnalysisPanel : public juce::Component
{
public:
    void setRecord (const juce::String& fname, const AudioAnalysis& rec)
    {
        hasData  = true;
        filename = fname;
        a        = rec;
        repaint();
    }
    void clear() { hasData = false; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient(
            juce::Colour(0xfff5fbff), b.getX(), b.getY(),
            juce::Colour(0xffe8f4fb), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (juce::Colour(0x40006090));
        g.drawRoundedRectangle (b.reduced(0.5f), 10.0f, 0.8f);
        g.setColour (juce::Colour(0x60ffffff));
        g.drawLine  (b.getX()+12.f, b.getY()+0.6f, b.getRight()-12.f, b.getY()+0.6f, 0.8f);

        if (!hasData)
        {
            g.setColour (AeroCol::muted());
            g.setFont   (juce::Font("Segoe UI", 12.0f, juce::Font::bold));
            g.drawText  ("Select a file to view analysis", b, juce::Justification::centred);
            return;
        }

        const int pad = 10;
        int y = pad;

        g.setColour (AeroCol::blue());
        g.setFont   (juce::Font("Segoe UI", 9.5f, juce::Font::bold));
        g.drawText  ("ANALYSIS", pad, y, getWidth()-pad*2, 13, juce::Justification::left);
        y += 14;

        g.setColour (AeroCol::text());
        g.setFont   (juce::Font("Segoe UI", 11.5f, juce::Font::bold));
        g.drawText  (filename, pad, y, getWidth()-pad*2, 15, juce::Justification::left, true);
        y += 18;

        g.setColour (juce::Colour(0x200060a0));
        g.fillRect  (pad, y, getWidth()-pad*2, 1);
        y += 6;

        const int cols  = 2;
        const int cw    = (getWidth() - pad*2 - 6) / cols;
        const int cellH = 44;

        auto lufsC = a.integratedLUFS > -9.f  ? AeroCol::red()
                   : a.integratedLUFS > -14.f ? AeroCol::amber()
                                              : AeroCol::green();
        auto tpC   = a.truePeak > 0.f         ? AeroCol::red()
                   : a.truePeak > -1.f        ? AeroCol::amber()
                                              : AeroCol::green();
        auto drC   = a.dynamicRange < 4.f     ? AeroCol::red()
                   : a.dynamicRange < 7.f     ? AeroCol::amber()
                                              : AeroCol::green();
        auto phC   = a.phaseCorrelation < 0.3f ? AeroCol::red()
                   : a.phaseCorrelation < 0.6f ? AeroCol::amber()
                                               : AeroCol::green();

        struct M { juce::String lbl, val, sub; juce::Colour vc; };
        juce::Array<M> metrics;
        metrics.add({ "LUFS",
                      juce::String(a.integratedLUFS, 1),
                      "Integrated",   lufsC });
        metrics.add({ "True Peak",
                      juce::String(a.truePeak, 1) + " dBTP",
                      "Inter-sample", tpC });
        metrics.add({ "Dyn Range",
                      juce::String(a.dynamicRange, 1) + " LU",
                      "DR score",     drC });
        metrics.add({ "Stereo",
                      juce::String((int)a.stereoWidth) + "%",
                      "Width",        AeroCol::blueMid() });
        metrics.add({ "Phase",
                      juce::String(a.phaseCorrelation, 2),
                      "-1w / +1m",    phC });
        metrics.add({ "L / R RMS",
                      juce::String(a.rmsLeft,1) + " / " + juce::String(a.rmsRight,1),
                      "dBFS",         AeroCol::text() });
        metrics.add({ "Sample Rate",
                      juce::String((int)(a.sampleRate / 1000.0)) + " kHz",
                      "",             AeroCol::blue() });
        metrics.add({ "Bit Depth",
                      juce::String(a.bitDepth) + "-bit",
                      "",             AeroCol::blue() });
        metrics.add({ "Duration",
                      fmtDur(a.duration),
                      "",             AeroCol::text() });
        metrics.add({ "Transients",
                      a.transientDensity,
                      "",             AeroCol::text() });

        for (int i = 0; i < metrics.size(); ++i)
        {
            int col = i % cols;
            int row = i / cols;
            juce::Rectangle<int> cr (pad + col*(cw+6), y + row*cellH, cw, cellH-4);

            g.setColour (juce::Colour(0x28ffffff));
            g.fillRoundedRectangle (cr.toFloat(), 6.0f);
            g.setColour (juce::Colour(0x280060a0));
            g.drawRoundedRectangle (cr.toFloat().reduced(0.5f), 6.0f, 0.6f);

            g.setColour (AeroCol::muted());
            g.setFont   (juce::Font("Segoe UI", 8.5f, juce::Font::bold));
            g.drawText  (metrics[i].lbl,
                         cr.getX()+5, cr.getY()+4, cr.getWidth()-6, 11,
                         juce::Justification::left);

            g.setColour (metrics[i].vc);
            g.setFont   (juce::Font("Segoe UI", 14.0f, juce::Font::bold));
            g.drawText  (metrics[i].val,
                         cr.getX()+5, cr.getY()+14, cr.getWidth()-6, 18,
                         juce::Justification::left, true);

            if (metrics[i].sub.isNotEmpty())
            {
                g.setColour (AeroCol::muted());
                g.setFont   (juce::Font("Segoe UI", 8.0f, juce::Font::plain));
                g.drawText  (metrics[i].sub,
                             cr.getX()+5, cr.getY()+31, cr.getWidth()-6, 10,
                             juce::Justification::left);
            }
        }

        int lastRow = (metrics.size() + cols - 1) / cols;
        juce::Rectangle<int> lr (pad, y + lastRow*cellH, getWidth()-pad*2, cellH-4);
        g.setColour (juce::Colour(0x28ffffff));
        g.fillRoundedRectangle (lr.toFloat(), 6.0f);
        g.setColour (juce::Colour(0x280060a0));
        g.drawRoundedRectangle (lr.toFloat().reduced(0.5f), 6.0f, 0.6f);

        g.setColour (AeroCol::muted());
        g.setFont   (juce::Font("Segoe UI", 8.5f, juce::Font::bold));
        g.drawText  ("LIMITING / CLIPPING",
                     lr.getX()+5, lr.getY()+4, lr.getWidth()-6, 11,
                     juce::Justification::left);

        juce::String ls;
        juce::Colour lc;
        if (a.hasClipping)
        {
            ls = juce::String(a.clippingCount) + " clips detected";
            lc = AeroCol::red();
        }
        else if (a.hasLimiting)
        {
            ls = "Limiter active  -" + juce::String(a.limitingAmount, 1) + " dB";
            lc = AeroCol::amber();
        }
        else
        {
            ls = "None \xe2\x80\x94 headroom preserved";
            lc = AeroCol::green();
        }
        g.setColour (lc);
        g.setFont   (juce::Font("Segoe UI", 12.0f, juce::Font::bold));
        g.drawText  (ls, lr.getX()+5, lr.getY()+14, lr.getWidth()-6, 18,
                     juce::Justification::left, true);
    }

private:
    bool          hasData = false;
    juce::String  filename;
    AudioAnalysis a;

    static juce::String fmtDur (double s)
    {
        int m   = (int)(s / 60.0);
        int sec = (int)s % 60;
        return juce::String(m) + ":" + juce::String(sec).paddedLeft('0', 2);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// InfoTooltip  –  floating bubble, shown as child of parent on hover
// ─────────────────────────────────────────────────────────────────────────────
class InfoTooltip : public juce::Component
{
public:
    juce::String text;

    InfoTooltip() { setInterceptsMouseClicks(false, false); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour (juce::Colour(0x28000030));
        g.fillRoundedRectangle (b.translated(2.f, 2.f), 8.0f);
        g.setGradientFill (juce::ColourGradient(
            juce::Colour(0xfff0f8ff), b.getX(), b.getY(),
            juce::Colour(0xffe0eef8), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (juce::Colour(0x800088cc));
        g.drawRoundedRectangle (b.reduced(0.5f), 8.0f, 0.8f);
        g.setColour (juce::Colour(0x60ffffff));
        g.drawLine  (b.getX()+8.f, b.getY()+0.8f, b.getRight()-8.f, b.getY()+0.8f, 0.8f);
        g.setColour (AeroCol::text());
        g.setFont   (juce::Font("Segoe UI", 10.5f, juce::Font::plain));
        g.drawFittedText (text, getLocalBounds().reduced(10, 6),
                          juce::Justification::topLeft, 6, 0.9f);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// InfoButton  –  small circle "i" that shows InfoTooltip on hover
// ─────────────────────────────────────────────────────────────────────────────
class InfoButton : public juce::Component
{
public:
    juce::String tooltipText;

    explicit InfoButton (const juce::String& tip) : tooltipText(tip) {}

    void paint (juce::Graphics& g) override
    {
        auto b   = getLocalBounds().toFloat().reduced(1.5f);
        auto col = hovered ? AeroCol::blue() : AeroCol::blueMid();
        g.setGradientFill (juce::ColourGradient(
            col.brighter(0.35f), b.getX(), b.getY(),
            col,                 b.getX(), b.getBottom(), false));
        g.fillEllipse    (b);
        g.setColour      (juce::Colour(0x50ffffff));
        g.drawEllipse    (b.reduced(0.5f), 0.8f);
        g.setColour      (juce::Colours::white);
        g.setFont        (juce::Font("Segoe UI", b.getHeight() * 0.60f, juce::Font::bold));
        g.drawText       ("i", b.toNearestInt(), juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent&) override { hovered=true;  repaint(); showTip(); }
    void mouseExit  (const juce::MouseEvent&) override { hovered=false; repaint(); hideTip(); }

private:
    bool hovered = false;
    std::unique_ptr<InfoTooltip> bubble;

    void showTip()
    {
        auto* parent = getParentComponent();
        if (!parent) return;

        bubble = std::make_unique<InfoTooltip>();
        bubble->text = tooltipText;

        const int bw = 260, bh = 88;
        int bx = getX() - 140;
        int by = getY() + getHeight() + 6;
        bx = juce::jlimit(4, parent->getWidth()  - bw - 4, bx);
        if (by + bh > parent->getHeight() - 8)
            by = getY() - bh - 4;

        bubble->setBounds (bx, by, bw, bh);
        parent->addAndMakeVisible (bubble.get());
        bubble->toFront (false);
    }

    void hideTip()
    {
        if (bubble)
        {
            if (auto* p = bubble->getParentComponent())
                p->removeChildComponent (bubble.get());
            bubble.reset();
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SettingsPanel
// ─────────────────────────────────────────────────────────────────────────────
class SettingsPanel : public juce::Component
{
public:
    juce::Label      portLabel, portHint;
    juce::TextEditor portEditor;
    juce::TextButton applyPortBtn { "Apply" };

    juce::Label    defFolderLabel;
    juce::ComboBox defFolderCombo;

    juce::Label  maxFilesLabel, maxFilesValLabel;
    juce::Slider maxFilesSlider;

    juce::ToggleButton autoAnalyseToggle { "Analyse files automatically on drop" };
    juce::ToggleButton showLufsToggle    { "Show LUFS badge in file list"        };
    juce::ToggleButton compactRowToggle  { "Compact file rows"                   };

    SongSenderProcessor* proc = nullptr;

    void init (SongSenderProcessor* p, const juce::StringArray& folders)
    {
        proc = p;
        portLabel.setText  ("Server Port", juce::dontSendNotification);
        portLabel.setFont  (juce::Font("Segoe UI", 11.5f, juce::Font::bold));
        portLabel.setColour (juce::Label::textColourId, AeroCol::text());
        addAndMakeVisible  (portLabel);

        portEditor.setText (juce::String(p->serverPort));
        portEditor.setInputRestrictions (5, "0123456789");
        portEditor.setFont  (juce::Font("Segoe UI", 11.0f, juce::Font::plain));
        portEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour(0xfff0f8ff));
        portEditor.setColour (juce::TextEditor::outlineColourId,    juce::Colour(0xff90c8e8));
        portEditor.setColour (juce::TextEditor::textColourId,       AeroCol::text());
        addAndMakeVisible   (portEditor);

        portHint.setText   ("Avoid 80, 443, 3000.  Requires plugin reload.", juce::dontSendNotification);
        portHint.setFont   (juce::Font("Segoe UI", 9.0f, juce::Font::italic));
        portHint.setColour (juce::Label::textColourId, AeroCol::muted());
        addAndMakeVisible  (portHint);

        applyPortBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour(0xffdceeff));
        applyPortBtn.setColour (juce::TextButton::textColourOffId, AeroCol::blue());
        applyPortBtn.setColour (juce::ComboBox::outlineColourId,   juce::Colour(0xff90c8e8));
        addAndMakeVisible (applyPortBtn);

        defFolderLabel.setText  ("Default Upload Folder", juce::dontSendNotification);
        defFolderLabel.setFont  (juce::Font("Segoe UI", 11.5f, juce::Font::bold));
        defFolderLabel.setColour (juce::Label::textColourId, AeroCol::text());
        addAndMakeVisible (defFolderLabel);

        for (int i = 0; i < folders.size(); ++i)
            defFolderCombo.addItem (folders[i], i+1);
        defFolderCombo.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (defFolderCombo);

        maxFilesLabel.setText  ("Max files to keep", juce::dontSendNotification);
        maxFilesLabel.setFont  (juce::Font("Segoe UI", 11.5f, juce::Font::bold));
        maxFilesLabel.setColour (juce::Label::textColourId, AeroCol::text());
        addAndMakeVisible (maxFilesLabel);

        maxFilesSlider.setRange (5, 100, 5);
        maxFilesSlider.setValue (50, juce::dontSendNotification);
        maxFilesSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        maxFilesSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        maxFilesSlider.setColour (juce::Slider::trackColourId, AeroCol::blue());
        maxFilesSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
        maxFilesSlider.onValueChange = [this]() {
            int v = (int)maxFilesSlider.getValue();
            maxFilesValLabel.setText (juce::String(v), juce::dontSendNotification);
            if (proc) proc->maxFilesToKeep = v;
        };
        addAndMakeVisible (maxFilesSlider);

        maxFilesValLabel.setText  ("50", juce::dontSendNotification);
        maxFilesValLabel.setFont  (juce::Font("Segoe UI", 11.5f, juce::Font::bold));
        maxFilesValLabel.setColour (juce::Label::textColourId, AeroCol::blue());
        addAndMakeVisible (maxFilesValLabel);

        for (auto* t : { &autoAnalyseToggle, &showLufsToggle, &compactRowToggle })
        {
            t->setColour (juce::ToggleButton::textColourId, AeroCol::text());
            t->setColour (juce::ToggleButton::tickColourId, AeroCol::blue());
            addAndMakeVisible (t);
        }
        // Restore from processor
        autoAnalyseToggle.setToggleState (p->autoAnalyse,   juce::dontSendNotification);
        showLufsToggle   .setToggleState (p->showLufsBadge, juce::dontSendNotification);
        compactRowToggle .setToggleState (p->compactRows,   juce::dontSendNotification);
        maxFilesSlider   .setValue       (p->maxFilesToKeep, juce::dontSendNotification);
        maxFilesValLabel .setText        (juce::String(p->maxFilesToKeep), juce::dontSendNotification);
        portEditor       .setText        (juce::String(p->serverPort));

        // onStateChange lambdas are set by SongSenderEditor after init()
        // so it can also call rebuildFileList() for immediate visual feedback.
    }

    void resized() override
    {
        const int m = 16, lh = 22, gap = 10;
        int y = m;

        portLabel.setBounds (m, y, getWidth()-m*2, lh);                       y += lh+2;
        portEditor.setBounds  (m, y, getWidth()-m*2-58, lh);
        applyPortBtn.setBounds (getWidth()-m-50, y, 50, lh);                  y += lh+4;
        portHint.setBounds (m, y, getWidth()-m*2, 14);                        y += 14+gap+4;

        defFolderLabel.setBounds (m, y, getWidth()-m*2, lh);                  y += lh+2;
        defFolderCombo.setBounds (m, y, getWidth()-m*2, lh);                  y += lh+gap+4;

        maxFilesLabel.setBounds (m, y, getWidth()-m*2, lh);                   y += lh+2;
        maxFilesSlider.setBounds  (m, y, getWidth()-m*2-40, lh);
        maxFilesValLabel.setBounds (getWidth()-m-34, y, 34, lh);              y += lh+gap+8;

        autoAnalyseToggle.setBounds (m, y, getWidth()-m*2, lh);               y += lh+gap;
        showLufsToggle.setBounds    (m, y, getWidth()-m*2, lh);               y += lh+gap;
        compactRowToggle.setBounds  (m, y, getWidth()-m*2, lh);
    }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient(
            juce::Colour(0xfff5fbff), 0.f, 0.f,
            juce::Colour(0xffe8f4fb), 0.f, (float)getHeight(), false));
        g.fillAll();
        g.setColour (juce::Colour(0x18005080));
        for (int yd : { 82, 148, 218 })
            g.drawLine (16.f, (float)yd, (float)(getWidth()-16), (float)yd, 0.5f);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Main Plugin Editor
// ─────────────────────────────────────────────────────────────────────────────
class SongSenderEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    SongSenderEditor  (SongSenderProcessor& p);
    ~SongSenderEditor () override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    SongSenderProcessor& processor;

    // ── Tab system ────────────────────────────────────────────────────────────
    enum Tab { TAB_SONG, TAB_SENDER, TAB_SETTINGS };
    Tab activeTab { TAB_SONG };

    juce::TextButton tabSong     { "Song"     };
    juce::TextButton tabSender   { "Sender"   };
    juce::TextButton tabSettings { "Settings" };

    // ── Song tab – left: drop zone + folder chips + file list ─────────────────
    DropZone dropZone;

    juce::Label      folderLabel;
    juce::ComboBox   folderCombo;
    juce::TextButton newFolderBtn { "+" };

    juce::OwnedArray<FolderChip> folderChips;
    juce::String                 activeFilter { "All" };

    juce::Viewport  fileListViewport;
    juce::Component fileListContainer;
    std::vector<std::unique_ptr<FileRowComponent>> fileRows;

    // Song tab – right: analysis panel
    AnalysisPanel analysisPanel;
    juce::String  selectedFileId;

    // ── Sender tab ────────────────────────────────────────────────────────────
    juce::Image      qrImage;
    juce::Label      urlLabel;           // text intentionally cleared; layout anchor only
    juce::TextButton copyUrlButton { "Copy URL" };

    juce::ToggleButton watchToggle    { "Auto-watch Last Exports" };
    juce::TextButton   watchFolderBtn { "Set Folder" };
    juce::Label        watchFolderLabel;
    InfoButton         watchInfoBtn   {
        "Set a folder, then enable Auto-watch.\n"
        "Any new audio file exported there will\n"
        "appear on your phone within 3 seconds.\n"
        "Default: Documents/SongSender/LastExports" };

    // ── Settings tab ──────────────────────────────────────────────────────────
    SettingsPanel settingsPanel;

    // ── Status bar ────────────────────────────────────────────────────────────
    juce::Label statusLabel;

    // ── State ─────────────────────────────────────────────────────────────────
    juce::StringArray folderList;

    // ── Private methods ───────────────────────────────────────────────────────
    void switchTab          (Tab t);
    void rebuildFileList    ();
    void rebuildFolderCombo ();
    void rebuildFolderChips ();
    void updateQR           ();
    void timerCallback      () override;
    void openBrowseDialog   ();
    void openNewFolderDialog ();
    void paintTabBar        (juce::Graphics& g);
    void paintSongTab       (juce::Graphics& g);
    void paintSenderTab     (juce::Graphics& g);

    int  W()  const { return getWidth();  }
    int  H()  const { return getHeight(); }
    // Left column width on the Song tab (file list side)
    int  LW() const { return juce::jmax(240, (int)(W() * 0.52f)); }

    static constexpr int kTabH    = 32;
    static constexpr int kStatusH = 22;
    static constexpr int kRowH    = 54;
    static constexpr int kMinW    = 560;
    static constexpr int kMinH    = 360;
    static constexpr int kMaxW    = 1000;
    static constexpr int kMaxH    = 700;

    float liveDotPhase = 0.0f;  // 0..1 drives the blinking animation

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongSenderEditor)
};
