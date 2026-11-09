#pragma once
#include <JuceHeader.h>
#include "qrcodegen.hpp"  // nayuki single-header — must be in Source/ folder

class QRCodeGenerator
{
public:
    // Generate a real scannable QR code as a JUCE Image.
    // pixelsPerModule: size of each QR module in pixels (8 = good for 140px panel)
    static juce::Image generate(const juce::String& text, int pixelsPerModule = 8)
    {
        try
        {
            using namespace qrcodegen;

            // Encode the URL — byte mode handles all URL characters safely
            auto qr = QrCode::encodeText(text.toRawUTF8(), QrCode::Ecc::MEDIUM);

            const int sz     = qr.getSize();
            const int margin = 2; // quiet zone in modules
            const int total  = (sz + margin * 2) * pixelsPerModule;

            juce::Image img(juce::Image::ARGB, total, total, true);
            juce::Graphics g(img);

            // Background — white so modules are scannable
            g.fillAll(juce::Colours::white);

            // Draw modules
            g.setColour(juce::Colour(0xff0c0c10)); // near-black — high contrast

            for (int y = 0; y < sz; ++y)
            {
                for (int x = 0; x < sz; ++x)
                {
                    if (qr.getModule(x, y))
                    {
                        g.fillRect((margin + x) * pixelsPerModule,
                                   (margin + y) * pixelsPerModule,
                                   pixelsPerModule,
                                   pixelsPerModule);
                    }
                }
            }

            return img;
        }
        catch (...)
        {
            // Fall back to placeholder if encoding fails (e.g. URL too long)
            return drawFallback(text, pixelsPerModule);
        }
    }

private:
    static juce::Image drawFallback(const juce::String& text, int ppm)
    {
        const int w = 29 * ppm + 8 * ppm;
        juce::Image img(juce::Image::ARGB, w, w, true);
        juce::Graphics g(img);
        g.setColour(juce::Colour(0xff1c1c24));
        g.fillRoundedRectangle(img.getBounds().toFloat(), 8.0f);
        g.setColour(juce::Colour(0xffa89dff));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), (float)ppm * 1.1f, juce::Font::plain));
        g.drawFittedText(text, img.getBounds().reduced(8), juce::Justification::centred, 4, 0.8f);
        return img;
    }
};
