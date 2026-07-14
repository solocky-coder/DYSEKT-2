#include "LogoBar.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include <BinaryData.h>

LogoBar::LogoBar (DysektProcessor& p) : processor (p) {}

void LogoBar::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().header);

    // ── Pick the PNG that matches the active theme ────────────────────────
    struct ThemePng
    {
        const char* id;
        const void* data;
        int         size;
    };

    static const ThemePng logos[] =
    {
        { "cr8",      BinaryData::DYSEKTSF_logo_cr8_png,      BinaryData::DYSEKTSF_logo_cr8_pngSize      },
        { "dark",     BinaryData::DYSEKTSF_logo_dark_png,     BinaryData::DYSEKTSF_logo_dark_pngSize     },
        { "dysekt",   BinaryData::DYSEKTSF_logo_dysekt_png,   BinaryData::DYSEKTSF_logo_dysekt_pngSize   },
        { "ghost",    BinaryData::DYSEKTSF_logo_ghost_png,    BinaryData::DYSEKTSF_logo_ghost_pngSize    },
        { "hack",     BinaryData::DYSEKTSF_logo_hack_png,     BinaryData::DYSEKTSF_logo_hack_pngSize     },
        { "lazy",     BinaryData::DYSEKTSF_logo_lazy_png,     BinaryData::DYSEKTSF_logo_lazy_pngSize     },
        { "midnight", BinaryData::DYSEKTSF_logo_midnight_png, BinaryData::DYSEKTSF_logo_midnight_pngSize },
        { "opendaw",  BinaryData::DYSEKTSF_logo_opendaw_png,  BinaryData::DYSEKTSF_logo_opendaw_pngSize  },
        { "pigments", BinaryData::DYSEKTSF_logo_pigments_png, BinaryData::DYSEKTSF_logo_pigments_pngSize },
        { "serum",    BinaryData::DYSEKTSF_logo_serum_png,    BinaryData::DYSEKTSF_logo_serum_pngSize    },
        { "shell",    BinaryData::DYSEKTSF_logo_shell_png,    BinaryData::DYSEKTSF_logo_shell_pngSize    },
        { "snow",     BinaryData::DYSEKTSF_logo_snow_png,     BinaryData::DYSEKTSF_logo_snow_pngSize     },
    };

    const auto& themeName = getTheme().name;
    const void* imgData = BinaryData::DYSEKTSF_logo_dysekt_png;   // fallback for custom themes
    int         imgSize = BinaryData::DYSEKTSF_logo_dysekt_pngSize;

    for (const auto& lp : logos)
    {
        if (themeName == lp.id)
        {
            imgData = lp.data;
            imgSize = lp.size;
            break;
        }
    }

    // ── Draw logo image ───────────────────────────────────────────────────
    auto img = juce::ImageCache::getFromMemory (imgData, imgSize);

    if (img.isValid())
    {
        // Inset by framePad so the logo sits inside the rounded border frame.
        const float framePad = 4.0f;
        const auto bounds = getLocalBounds().toFloat()
                                .withTrimmedTop (3.0f)
                                .reduced (framePad + 2.0f, framePad);

        g.drawImageWithin (img,
                           (int) bounds.getX(),
                           (int) bounds.getY(),
                           (int) bounds.getWidth(),
                           (int) bounds.getHeight(),
                           juce::RectanglePlacement::centred
                               | juce::RectanglePlacement::onlyReduceInSize);
    }

    // ── Frame border — drawn on top so it sits over the image ────────────
    const auto& accent = getTheme().accent;
    const juce::Rectangle<float> fr (getLocalBounds().toFloat().withTrimmedTop (3.0f));
    g.setColour (accent.withAlpha (0.18f));
    g.drawRoundedRectangle (fr.expanded (0.5f), 5.0f, 1.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (fr.reduced (0.5f), 4.0f, 1.5f);
    g.setColour (accent.withAlpha (0.15f));
    g.drawRoundedRectangle (fr.reduced (2.0f), 3.5f, 1.0f);
}
