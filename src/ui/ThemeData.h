#pragma once
#include <juce_graphics/juce_graphics.h>

struct ThemeData
{
    juce::String name;

    juce::Colour background;
    juce::Colour waveformBg;
    juce::Colour darkBar;
    juce::Colour foreground;
    juce::Colour header;
    juce::Colour waveform;
    juce::Colour selectionOverlay;
    juce::Colour lockActive;
    juce::Colour lockInactive;
    juce::Colour gridLine;
    juce::Colour accent;
    juce::Colour button;
    juce::Colour buttonHover;
    juce::Colour separator;

    juce::Colour slicePalette[16];

    static ThemeData darkTheme()
    {
        ThemeData t;
        t.name          = "dark";
        t.background       = juce::Colour (0xFF000000);
        t.waveformBg    = juce::Colour (0xFF060608);
        t.darkBar       = juce::Colour (0xFF0E0E13);
        t.foreground    = juce::Colour (0xFFCCD0D8);
        t.header    = juce::Colour (0xFF0D0D14);
        t.waveform = juce::Colour::fromFloatRGBA (0.70f, 0.78f, 0.85f, 1.0f);
        t.selectionOverlay = juce::Colour::fromFloatRGBA (0.25f, 0.35f, 0.55f, 1.0f);
        t.lockActive      = juce::Colour::fromFloatRGBA (0.90f, 0.35f, 0.22f, 1.0f);
        t.lockInactive       = juce::Colour::fromFloatRGBA (0.30f, 0.30f, 0.34f, 1.0f);
        t.gridLine      = juce::Colour::fromFloatRGBA (0.14f, 0.14f, 0.18f, 1.0f);
        t.accent        = juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 1.0f);
        t.button        = juce::Colour (0xFF1C2028);
        t.buttonHover   = juce::Colour (0xFF2A3040);
        t.separator     = juce::Colour::fromFloatRGBA (0.20f, 0.20f, 0.25f, 1.0f);
        t.slicePalette[0 ]= juce::Colour (0xFFFF00B4); // Hot Magenta
        t.slicePalette[1 ]= juce::Colour (0xFF00FF87); // Toxic Lime
        t.slicePalette[2 ]= juce::Colour (0xFFFF6B00); // Molten Orange
        t.slicePalette[3 ]= juce::Colour (0xFF00C9FF); // Ice Blue
        t.slicePalette[4 ]= juce::Colour (0xFFFFE800); // Radioactive Yellow
        t.slicePalette[5 ]= juce::Colour (0xFF8B00FF); // UV Violet
        t.slicePalette[6 ]= juce::Colour (0xFF00FFD4); // Absinthe
        t.slicePalette[7 ]= juce::Colour (0xFFFF002D); // Alarm Red
        t.slicePalette[8 ]= juce::Colour (0xFF39FF13); // Neon Green
        t.slicePalette[9 ]= juce::Colour (0xFFFF0090); // Acid Pink
        t.slicePalette[10]= juce::Colour (0xFF00A9FF); // Reactor Blue
        t.slicePalette[11]= juce::Colour (0xFFFFC400); // Hazard Amber
        t.slicePalette[12]= juce::Colour (0xFFFF3E7C); // Coral Crimson
        t.slicePalette[13]= juce::Colour (0xFF3ECEFF); // Steel Teal
        t.slicePalette[14]= juce::Colour (0xFFE3FF00); // Chartreuse
        t.slicePalette[15]= juce::Colour (0xFFA57CFF); // Lavender
        return t;
    }

    // ── SHELL ─────────────────────────────────────────────────────────────────
    // Deep green tint · Phosphor terminal · Organic warmth
    static ThemeData shellTheme()
    {
        ThemeData t;
        t.name             = "shell";
        t.background       = juce::Colour (0xFF000000);
        t.waveformBg       = juce::Colour (0xFF080F0C);
        t.darkBar          = juce::Colour (0xFF0E1A14);
        t.foreground       = juce::Colour (0xFF8ECBA0);
        t.header           = juce::Colour (0xFF0A1610);
        t.waveform         = juce::Colour (0xFF3DCC6A);
        t.selectionOverlay = juce::Colour (0xFF1A4428);
        t.lockActive       = juce::Colour (0xFF3DCC6A);
        t.lockInactive     = juce::Colour (0xFF1E3228);
        t.gridLine         = juce::Colour (0xFF142018);
        t.accent           = juce::Colour (0xFF3DCC6A);
        t.button           = juce::Colour (0xFF122018);
        t.buttonHover      = juce::Colour (0xFF1C3022);
        t.separator        = juce::Colour (0xFF1A2A1E);
        t.slicePalette[0 ] = juce::Colour (0xFFFF00B4); // Hot Magenta
        t.slicePalette[1 ] = juce::Colour (0xFF00FF87); // Toxic Lime
        t.slicePalette[2 ] = juce::Colour (0xFFFF6A00); // Molten Orange
        t.slicePalette[3 ] = juce::Colour (0xFF00C8FF); // Ice Blue
        t.slicePalette[4 ] = juce::Colour (0xFFFFE700); // Radioactive Yellow
        t.slicePalette[5 ] = juce::Colour (0xFF8B00FF); // UV Violet
        t.slicePalette[6 ] = juce::Colour (0xFF00FFD3); // Absinthe
        t.slicePalette[7 ] = juce::Colour (0xFFFF002D); // Alarm Red
        t.slicePalette[8 ] = juce::Colour (0xFF38FF14); // Neon Green
        t.slicePalette[9 ] = juce::Colour (0xFFFF0090); // Acid Pink
        t.slicePalette[10] = juce::Colour (0xFF00A8FF); // Reactor Blue
        t.slicePalette[11] = juce::Colour (0xFFFFC300); // Hazard Amber
        t.slicePalette[12] = juce::Colour (0xFFFF3C78); // Coral Crimson
        t.slicePalette[13] = juce::Colour (0xFF3CC8FF); // Steel Teal
        t.slicePalette[14] = juce::Colour (0xFFDBFF00); // Chartreuse
        t.slicePalette[15] = juce::Colour (0xFFA078FF); // Lavender
        return t;
    }


    // ── LAZY ─────────────────────────────────────────────────────────────────
    // Electric blue accent · Deep charcoal panels · High contrast
    static ThemeData lazyTheme()
    {
        ThemeData t;
        t.name             = "lazy";
        t.background       = juce::Colour (0xFF000000);
        t.waveformBg       = juce::Colour (0xFF13151A);
        t.darkBar          = juce::Colour (0xFF16181F);
        t.foreground       = juce::Colour (0xFFC8D8E8);
        t.header           = juce::Colour (0xFF13151A);
        t.waveform         = juce::Colour (0xFF4A9EFF);
        t.selectionOverlay = juce::Colour (0xFF1E3050);
        t.lockActive       = juce::Colour (0xFF4A9EFF);
        t.lockInactive     = juce::Colour (0xFF2A3545);
        t.gridLine         = juce::Colour (0xFF1E2028);
        t.accent           = juce::Colour (0xFF4A9EFF);
        t.button           = juce::Colour (0xFF1E2028);
        t.buttonHover      = juce::Colour (0xFF2A3545);
        t.separator        = juce::Colour (0xFF252830);
        t.slicePalette[0] = juce::Colour (0xFFEA00A5); // Hot Magenta
        t.slicePalette[1] = juce::Colour (0xFF00EA7C); // Toxic Lime
        t.slicePalette[2] = juce::Colour (0xFFEA6200); // Molten Orange
        t.slicePalette[3] = juce::Colour (0xFF00B8EA); // Ice Blue
        t.slicePalette[4] = juce::Colour (0xFFEAD500); // Radioactive Yellow
        t.slicePalette[5] = juce::Colour (0xFF7F00EA); // UV Violet
        t.slicePalette[6] = juce::Colour (0xFF00EAC3); // Absinthe
        t.slicePalette[7] = juce::Colour (0xFFEA002A); // Alarm Red
        t.slicePalette[8] = juce::Colour (0xFF34EA12); // Neon Green
        t.slicePalette[9] = juce::Colour (0xFFEA0084); // Acid Pink
        t.slicePalette[10]= juce::Colour (0xFF009AEA); // Reactor Blue
        t.slicePalette[11]= juce::Colour (0xFFEAB400); // Hazard Amber
        t.slicePalette[12]= juce::Colour (0xFFEA3870); // Coral Crimson
        t.slicePalette[13]= juce::Colour (0xFF38B8EA); // Steel Teal
        t.slicePalette[14]= juce::Colour (0xFFCAEA00); // Chartreuse
        t.slicePalette[15]= juce::Colour (0xFF9070EA); // Lavender
        return t;
    }

    // ── SNOW ─────────────────────────────────────────────────────────────────
    // Warm orange accent · Dark slate panels · Professional warmth
    static ThemeData snowTheme()
    {
        ThemeData t;
        t.name             = "snow";
        t.background       = juce::Colour (0xFF000000);
        t.waveformBg       = juce::Colour (0xFF1C1D21);
        t.darkBar          = juce::Colour (0xFF1E1F23);
        t.foreground       = juce::Colour (0xFFE8E0D4);
        t.header           = juce::Colour (0xFF1C1D21);
        t.waveform         = juce::Colour (0xFFE87C2A);
        t.selectionOverlay = juce::Colour (0xFF3A2810);
        t.lockActive       = juce::Colour (0xFFE87C2A);
        t.lockInactive     = juce::Colour (0xFF383940);
        t.gridLine         = juce::Colour (0xFF2A2B30);
        t.accent           = juce::Colour (0xFFE87C2A);
        t.button           = juce::Colour (0xFF2A2B30);
        t.buttonHover      = juce::Colour (0xFF383940);
        t.separator        = juce::Colour (0xFF2E3035);
        t.slicePalette[0] = juce::Colour (0xFFFF00B4); // Hot Magenta
        t.slicePalette[1] = juce::Colour (0xFF00FF87); // Toxic Lime
        t.slicePalette[2] = juce::Colour (0xFFFF6B00); // Molten Orange
        t.slicePalette[3] = juce::Colour (0xFF00C9FF); // Ice Blue
        t.slicePalette[4] = juce::Colour (0xFFFFE900); // Radioactive Yellow
        t.slicePalette[5] = juce::Colour (0xFF8B00FF); // UV Violet
        t.slicePalette[6] = juce::Colour (0xFF00FFD4); // Absinthe
        t.slicePalette[7] = juce::Colour (0xFFFF002E); // Alarm Red
        t.slicePalette[8] = juce::Colour (0xFF39FF14); // Neon Green
        t.slicePalette[9] = juce::Colour (0xFFFF0090); // Acid Pink
        t.slicePalette[10]= juce::Colour (0xFF00A7FF); // Reactor Blue
        t.slicePalette[11]= juce::Colour (0xFFFFC400); // Hazard Amber
        t.slicePalette[12]= juce::Colour (0xFFFF3D7B); // Coral Crimson
        t.slicePalette[13]= juce::Colour (0xFF3DC9FF); // Steel Teal
        t.slicePalette[14]= juce::Colour (0xFFDCFF00); // Chartreuse
        t.slicePalette[15]= juce::Colour (0xFF9C7BFF); // Lavender
        return t;
    }

    // ── GHOST ────────────────────────────────────────────────────────────────
    // Bright teal/mint accent · Near-black · Ultra-minimal
    static ThemeData ghostTheme()
    {
        ThemeData t;
        t.name             = "ghost";
        t.background       = juce::Colour (0xFF000000);
        t.waveformBg       = juce::Colour (0xFF090B0E);
        t.darkBar          = juce::Colour (0xFF0B0D10);
        t.foreground       = juce::Colour (0xFFC8E8E0);
        t.header           = juce::Colour (0xFF090B0E);
        t.waveform         = juce::Colour (0xFF2DD4A8);
        t.selectionOverlay = juce::Colour (0xFF0A2A20);
        t.lockActive       = juce::Colour (0xFF2DD4A8);
        t.lockInactive     = juce::Colour (0xFF1E2830);
        t.gridLine         = juce::Colour (0xFF141820);
        t.accent           = juce::Colour (0xFF2DD4A8);
        t.button           = juce::Colour (0xFF141820);
        t.buttonHover      = juce::Colour (0xFF1E2830);
        t.separator        = juce::Colour (0xFF1A1E24);
        t.slicePalette[0] = juce::Colour (0xFFFF00B5); // Hot Magenta
        t.slicePalette[1] = juce::Colour (0xFF00FF87); // Toxic Lime
        t.slicePalette[2] = juce::Colour (0xFFFF6A00); // Molten Orange
        t.slicePalette[3] = juce::Colour (0xFF00C9FF); // Ice Blue
        t.slicePalette[4] = juce::Colour (0xFFFFE900); // Radioactive Yellow
        t.slicePalette[5] = juce::Colour (0xFF8B00FF); // UV Violet
        t.slicePalette[6] = juce::Colour (0xFF00FFD4); // Absinthe
        t.slicePalette[7] = juce::Colour (0xFFFF002E); // Alarm Red
        t.slicePalette[8] = juce::Colour (0xFF39FF14); // Neon Green
        t.slicePalette[9] = juce::Colour (0xFFFF0090); // Acid Pink
        t.slicePalette[10]= juce::Colour (0xFF00A8FF); // Reactor Blue
        t.slicePalette[11]= juce::Colour (0xFFFFC400); // Hazard Amber
        t.slicePalette[12]= juce::Colour (0xFFFF3D7B); // Coral Crimson
        t.slicePalette[13]= juce::Colour (0xFF3DC9FF); // Steel Teal
        t.slicePalette[14]= juce::Colour (0xFFDFFF00); // Chartreuse
        t.slicePalette[15]= juce::Colour (0xFFA17BFF); // Lavender
        return t;
    }

    // ── HACK ─────────────────────────────────────────────────────────────────
    // Red accent · Pure black · Industrial — classic Akai MPC
    static ThemeData hackTheme()
    {
        ThemeData t;
        t.name             = "hack";
        t.background       = juce::Colour (0xFF000000);
        t.waveformBg       = juce::Colour (0xFF050505);
        t.darkBar          = juce::Colour (0xFF070707);
        t.foreground       = juce::Colour (0xFFC8C8C8);
        t.header           = juce::Colour (0xFF050505);
        t.waveform         = juce::Colour (0xFFCC2200);
        t.selectionOverlay = juce::Colour (0xFF2A0500);
        t.lockActive       = juce::Colour (0xFFCC2200);
        t.lockInactive     = juce::Colour (0xFF252525);
        t.gridLine         = juce::Colour (0xFF141414);
        t.accent           = juce::Colour (0xFFCC2200);
        t.button           = juce::Colour (0xFF0F0F0F);
        t.buttonHover      = juce::Colour (0xFF1A0000);
        t.separator        = juce::Colour (0xFF1A1A1A);
        t.slicePalette[0] = juce::Colour (0xFFFF00B3); // Hot Magenta
        t.slicePalette[1] = juce::Colour (0xFF00FF87); // Toxic Lime
        t.slicePalette[2] = juce::Colour (0xFFFF6A00); // Molten Orange
        t.slicePalette[3] = juce::Colour (0xFF00C9FF); // Ice Blue
        t.slicePalette[4] = juce::Colour (0xFFFFE800); // Radioactive Yellow
        t.slicePalette[5] = juce::Colour (0xFF8B00FF); // UV Violet
        t.slicePalette[6] = juce::Colour (0xFF00FFD4); // Absinthe
        t.slicePalette[7] = juce::Colour (0xFFFF002E); // Alarm Red
        t.slicePalette[8] = juce::Colour (0xFF38FF14); // Neon Green
        t.slicePalette[9] = juce::Colour (0xFFFF008F); // Acid Pink
        t.slicePalette[10]= juce::Colour (0xFF00A8FF); // Reactor Blue
        t.slicePalette[11]= juce::Colour (0xFFFFC400); // Hazard Amber
        t.slicePalette[12]= juce::Colour (0xFFFF3978); // Coral Crimson
        t.slicePalette[13]= juce::Colour (0xFF39C9FF); // Steel Teal
        t.slicePalette[14]= juce::Colour (0xFFDDFF00); // Chartreuse
        t.slicePalette[15]= juce::Colour (0xFF9573FF); // Lavender
        return t;
    }


    // ── MIDNIGHT ─────────────────────────────────────────────────────────────
    // Deep purple-black · Violet accent · Serum / Vital premium synth feel
    // Sharp 2px radius · Zero glow · Crisp 1px borders — Direction D
    static ThemeData midnightTheme()
    {
        ThemeData t;
        t.name             = "midnight";
        t.background       = juce::Colour (0xFF000000);   // deep purple chassis
        t.waveformBg       = juce::Colour (0xFF08060F);   // near-void
        t.darkBar          = juce::Colour (0xFF100E1A);   // panel bars
        t.foreground       = juce::Colour (0xFFD8D0F0);   // cool purple-tinted white
        t.header           = juce::Colour (0xFF0A0812);   // top bar
        t.waveform         = juce::Colour (0xFF9B6FFF);   // violet
        t.selectionOverlay = juce::Colour (0xFF1A0F38);   // selection tint
        t.lockActive       = juce::Colour (0xFF9B6FFF);
        t.lockInactive     = juce::Colour (0xFF1C1838);
        t.gridLine         = juce::Colour (0xFF161228);
        t.accent           = juce::Colour (0xFF9B6FFF);   // electric violet
        t.button           = juce::Colour (0xFF18152A);   // elevated panel
        t.buttonHover      = juce::Colour (0xFF221F38);
        t.separator        = juce::Colour (0xFF2A2545);
        t.slicePalette[0 ] = juce::Colour (0xFFFF2DA0); // Hot Magenta
        t.slicePalette[1 ] = juce::Colour (0xFF00FFAA); // Mint
        t.slicePalette[2 ] = juce::Colour (0xFFFF6B00); // Amber
        t.slicePalette[3 ] = juce::Colour (0xFF5CCCFF); // Cyan
        t.slicePalette[4 ] = juce::Colour (0xFFFFE000); // Solar Yellow
        t.slicePalette[5 ] = juce::Colour (0xFF9B6FFF); // Violet
        t.slicePalette[6 ] = juce::Colour (0xFF00FFD4); // Aqua
        t.slicePalette[7 ] = juce::Colour (0xFFFF2040); // Alarm Red
        t.slicePalette[8 ] = juce::Colour (0xFF40FF30); // Neon Green
        t.slicePalette[9 ] = juce::Colour (0xFFFF40A0); // Acid Pink
        t.slicePalette[10] = juce::Colour (0xFF00A8FF); // Sky Blue
        t.slicePalette[11] = juce::Colour (0xFFFFB800); // Gold
        t.slicePalette[12] = juce::Colour (0xFFFF5090); // Coral Crimson
        t.slicePalette[13] = juce::Colour (0xFF50C8FF); // Steel Teal
        t.slicePalette[14] = juce::Colour (0xFFD0FF00); // Chartreuse
        t.slicePalette[15] = juce::Colour (0xFFFF90D8); // Rose Quartz
        return t;
    }

    // ── CR8 ──────────────────────────────────────────────────────────────────
    // Burnt orange accent · Deep charcoal · Steel panels — Waves CR8 inspired
    static ThemeData cr8Theme()
    {
        ThemeData t;
        t.name             = "cr8";
        t.background       = juce::Colour (0xFF0C0D0E);   // near-black warm chassis
        t.waveformBg       = juce::Colour (0xFF131518);   // dark steel waveform bg
        t.darkBar          = juce::Colour (0xFF181A1D);   // panel bars
        t.foreground       = juce::Colour (0xFFDDD5C8);   // warm off-white
        t.header           = juce::Colour (0xFF101214);   // top bar
        t.waveform         = juce::Colour (0xFFD96010);   // burnt orange
        t.selectionOverlay = juce::Colour (0xFF2E1800);   // deep amber tint
        t.lockActive       = juce::Colour (0xFFD96010);   // burnt orange
        t.lockInactive     = juce::Colour (0xFF2A2C30);   // steel inactive
        t.gridLine         = juce::Colour (0xFF1C1E22);   // barely-visible warm grid
        t.accent           = juce::Colour (0xFFD96010);   // burnt orange
        t.button           = juce::Colour (0xFF22252A);   // elevated steel panel
        t.buttonHover      = juce::Colour (0xFF2E3238);   // hover lift
        t.separator        = juce::Colour (0xFF282B30);   // steel divider
        t.slicePalette[0 ] = juce::Colour (0xFFD96010); // Burnt Orange (accent)
        t.slicePalette[1 ] = juce::Colour (0xFF10A8D9); // Steel Blue
        t.slicePalette[2 ] = juce::Colour (0xFFD9C010); // Warm Yellow
        t.slicePalette[3 ] = juce::Colour (0xFF10D978); // Mint
        t.slicePalette[4 ] = juce::Colour (0xFFD91060); // Crimson
        t.slicePalette[5 ] = juce::Colour (0xFF8810D9); // Violet
        t.slicePalette[6 ] = juce::Colour (0xFF10D9C0); // Teal
        t.slicePalette[7 ] = juce::Colour (0xFFD93010); // Alarm Red
        t.slicePalette[8 ] = juce::Colour (0xFF50D910); // Lime
        t.slicePalette[9 ] = juce::Colour (0xFFD910A0); // Acid Pink
        t.slicePalette[10] = juce::Colour (0xFF1080D9); // Reactor Blue
        t.slicePalette[11] = juce::Colour (0xFFD98010); // Hazard Amber
        t.slicePalette[12] = juce::Colour (0xFFD94070); // Coral
        t.slicePalette[13] = juce::Colour (0xFF40A8D9); // Ice Blue
        t.slicePalette[14] = juce::Colour (0xFFA8D910); // Chartreuse
        t.slicePalette[15] = juce::Colour (0xFF9060D9); // Lavender
        return t;
    }

    // ── PIGMENTS ──────────────────────────────────────────────────────────────
    // Deep navy-black · Electric cyan accent · Arturia Pigments-inspired
    static ThemeData pigmentsTheme()
    {
        ThemeData t;
        t.name             = "pigments";
        t.background       = juce::Colour (0xFF000000);   // blue-tinted chassis
        t.waveformBg       = juce::Colour (0xFF09090F);   // deeper for waveform
        t.darkBar          = juce::Colour (0xFF0C0C16);   // panel bars
        t.foreground       = juce::Colour (0xFFDDE6F5);   // cool off-white
        t.header           = juce::Colour (0xFF0A0A12);   // top bar
        t.waveform         = juce::Colour (0xFF5CCCFF);   // Pigments cyan
        t.selectionOverlay = juce::Colour (0xFF0A1E38);   // selection tint
        t.lockActive       = juce::Colour (0xFF5CCCFF);
        t.lockInactive     = juce::Colour (0xFF1C2040);
        t.gridLine         = juce::Colour (0xFF14142A);
        t.accent           = juce::Colour (0xFF5CCCFF);   // electric cyan
        t.button           = juce::Colour (0xFF181828);   // elevated panel
        t.buttonHover      = juce::Colour (0xFF22223A);
        t.separator        = juce::Colour (0xFF28284A);
        t.slicePalette[0 ] = juce::Colour (0xFFFF2DA0); // Hot Magenta
        t.slicePalette[1 ] = juce::Colour (0xFF00FFAA); // Mint
        t.slicePalette[2 ] = juce::Colour (0xFFFF6B00); // Amber
        t.slicePalette[3 ] = juce::Colour (0xFF5CCCFF); // Pigments Cyan
        t.slicePalette[4 ] = juce::Colour (0xFFFFE000); // Solar Yellow
        t.slicePalette[5 ] = juce::Colour (0xFF9B59FF); // UV Violet
        t.slicePalette[6 ] = juce::Colour (0xFF00FFD4); // Aqua
        t.slicePalette[7 ] = juce::Colour (0xFFFF2040); // Alarm Red
        t.slicePalette[8 ] = juce::Colour (0xFF40FF30); // Neon Green
        t.slicePalette[9 ] = juce::Colour (0xFFFF40A0); // Acid Pink
        t.slicePalette[10] = juce::Colour (0xFF00A8FF); // Sky Blue
        t.slicePalette[11] = juce::Colour (0xFFFFB800); // Gold
        t.slicePalette[12] = juce::Colour (0xFFFF5070); // Coral Crimson
        t.slicePalette[13] = juce::Colour (0xFF5CFFCC); // Seafoam
        t.slicePalette[14] = juce::Colour (0xFFCCFF00); // Chartreuse
        t.slicePalette[15] = juce::Colour (0xFFAA70FF); // Lavender
        return t;
    }

    // ── DYSEKT-SF ────────────────────────────────────────────────────────────────
    // The signature industrial skin: near-void black chassis, neon teal accent,
    // charcoal panel bars.  Only active / selected elements glow — everything
    // else stays dim.  Matches the dark-mode UI mockup shown in the design docs.
    static ThemeData dysektTheme()
    {
        ThemeData t;
        t.name             = "dysekt";
        t.background       = juce::Colour (0xFF000000);   // absolute black chassis
        t.waveformBg       = juce::Colour (0xFF060609);   // near-void waveform panel
        t.darkBar          = juce::Colour (0xFF0D0D12);   // charcoal panel bars
        t.foreground       = juce::Colour (0xFFB8C8D0);   // cool blue-grey text
        t.header           = juce::Colour (0xFF0A0A0F);   // top bar
        t.waveform         = juce::Colour (0xFF00FFE0);   // neon teal waveform
        t.selectionOverlay = juce::Colour (0xFF003830);   // deep teal selection tint
        t.lockActive       = juce::Colour (0xFF00FFE0);   // teal lock-on
        t.lockInactive     = juce::Colour (0xFF1A2028);   // dim steel lock-off
        t.gridLine         = juce::Colour (0xFF101418);   // barely-visible grid
        t.accent           = juce::Colour (0xFF00FFE0);   // #00ffe0 — the single glow colour
        t.button           = juce::Colour (0xFF141A20);   // elevated panel button
        t.buttonHover      = juce::Colour (0xFF1C2830);   // teal-tinted hover
        t.separator        = juce::Colour (0xFF181E24);   // steel divider
        // Slice palette: 16 vivid neons that read clearly against near-black
        t.slicePalette[0 ] = juce::Colour (0xFFFF0090); // Hot Magenta
        t.slicePalette[1 ] = juce::Colour (0xFF00FF6C); // Toxic Lime
        t.slicePalette[2 ] = juce::Colour (0xFFFF5C00); // Molten Orange
        t.slicePalette[3 ] = juce::Colour (0xFF00D4FF); // Ice Blue
        t.slicePalette[4 ] = juce::Colour (0xFFFFE000); // Radioactive Yellow
        t.slicePalette[5 ] = juce::Colour (0xFF7000FF); // UV Violet
        t.slicePalette[6 ] = juce::Colour (0xFF00FFE0); // Absinthe (= accent)
        t.slicePalette[7 ] = juce::Colour (0xFFFF1A30); // Alarm Red
        t.slicePalette[8 ] = juce::Colour (0xFF30FF10); // Neon Green
        t.slicePalette[9 ] = juce::Colour (0xFFFF0070); // Acid Pink
        t.slicePalette[10] = juce::Colour (0xFF0090FF); // Reactor Blue
        t.slicePalette[11] = juce::Colour (0xFFFFAA00); // Hazard Amber
        t.slicePalette[12] = juce::Colour (0xFFFF3060); // Coral Crimson
        t.slicePalette[13] = juce::Colour (0xFF30B8FF); // Steel Teal
        t.slicePalette[14] = juce::Colour (0xFFC8FF00); // Chartreuse
        t.slicePalette[15] = juce::Colour (0xFF8060FF); // Lavender
        return t;
    }

    // ── SERUM ─────────────────────────────────────────────────────────────────
    // Cold steel panels · Cyan glow accent · Metallic blue-grey — Serum 2 vibe
    static ThemeData serumTheme()
    {
        ThemeData t;
        t.name             = "serum";
        t.background       = juce::Colour (0xFF000000);   // black void chassis
        t.waveformBg       = juce::Colour (0xFF0A0C12);   // deep cold steel
        t.darkBar          = juce::Colour (0xFF131720);   // steel panel bar
        t.foreground       = juce::Colour (0xFFC2D0E0);   // cold blue-grey text
        t.header           = juce::Colour (0xFF0D101A);   // top bar
        t.waveform         = juce::Colour (0xFF3ADDE8);   // Serum cyan
        t.selectionOverlay = juce::Colour (0xFF0C2234);   // cold teal selection
        t.lockActive       = juce::Colour (0xFF3ADDE8);   // cyan lock
        t.lockInactive     = juce::Colour (0xFF1C2838);   // steel lock
        t.gridLine         = juce::Colour (0xFF181E2C);   // barely-visible grid
        t.accent           = juce::Colour (0xFF3ADDE8);   // Serum cyan accent
        t.button           = juce::Colour (0xFF1C2438);   // elevated steel button
        t.buttonHover      = juce::Colour (0xFF263050);   // hover lift
        t.separator        = juce::Colour (0xFF252E40);   // steel divider
        t.slicePalette[0 ] = juce::Colour (0xFFFF2DA0); // Hot Magenta
        t.slicePalette[1 ] = juce::Colour (0xFF00FFC8); // Serum Mint
        t.slicePalette[2 ] = juce::Colour (0xFFFF7020); // Amber
        t.slicePalette[3 ] = juce::Colour (0xFF3ADDE8); // Serum Cyan (accent)
        t.slicePalette[4 ] = juce::Colour (0xFFFFE040); // Solar Yellow
        t.slicePalette[5 ] = juce::Colour (0xFF8855FF); // UV Violet
        t.slicePalette[6 ] = juce::Colour (0xFF00FFCC); // Aqua
        t.slicePalette[7 ] = juce::Colour (0xFFFF2040); // Alarm Red
        t.slicePalette[8 ] = juce::Colour (0xFF40FF50); // Neon Green
        t.slicePalette[9 ] = juce::Colour (0xFFFF40B0); // Acid Pink
        t.slicePalette[10] = juce::Colour (0xFF20A8FF); // Sky Blue
        t.slicePalette[11] = juce::Colour (0xFFFFBB00); // Gold
        t.slicePalette[12] = juce::Colour (0xFFFF5070); // Coral Crimson
        t.slicePalette[13] = juce::Colour (0xFF50D8FF); // Ice Blue
        t.slicePalette[14] = juce::Colour (0xFFC8FF00); // Chartreuse
        t.slicePalette[15] = juce::Colour (0xFFAA80FF); // Lavender
        return t;
    }

    // ── OPENDAW ───────────────────────────────────────────────────────────────
    // Neutral dark-gray chassis · Teal/cyan accent · Professional, flat, minimal
    // Inspired by OpenDAW: muted backgrounds, readable text, no glow, clean grid
    static ThemeData opendawTheme()
    {
        ThemeData t;
        t.name             = "opendaw";
        t.background       = juce::Colour (0xFF1A1D21);   // dark neutral gray chassis
        t.waveformBg       = juce::Colour (0xFF1E2228);   // slightly lighter panel
        t.darkBar          = juce::Colour (0xFF161920);   // header / bar strip
        t.foreground       = juce::Colour (0xFFCDD5DC);   // cool off-white — readable
        t.header           = juce::Colour (0xFF13161C);   // top bar
        t.waveform         = juce::Colour (0xFF2CC4A8);   // teal waveform (OpenDAW)
        t.selectionOverlay = juce::Colour (0xFF1A3A34);   // muted teal selection
        t.lockActive       = juce::Colour (0xFF2CC4A8);   // teal active
        t.lockInactive     = juce::Colour (0xFF2A3040);   // dim steel inactive
        t.gridLine         = juce::Colour (0xFF252A32);   // visible but subtle grid
        t.accent           = juce::Colour (0xFF2CC4A8);   // #2cc4a8 — OpenDAW teal
        t.button           = juce::Colour (0xFF252B34);   // elevated panel button
        t.buttonHover      = juce::Colour (0xFF2E3640);   // hover lift
        t.separator        = juce::Colour (0xFF2A3040);   // steel divider
        t.slicePalette[0 ] = juce::Colour (0xFFFF2DA0);
        t.slicePalette[1 ] = juce::Colour (0xFF00FFC8);
        t.slicePalette[2 ] = juce::Colour (0xFFFF7020);
        t.slicePalette[3 ] = juce::Colour (0xFF2CC4A8);
        t.slicePalette[4 ] = juce::Colour (0xFFFFE040);
        t.slicePalette[5 ] = juce::Colour (0xFF8855FF);
        t.slicePalette[6 ] = juce::Colour (0xFF00FFCC);
        t.slicePalette[7 ] = juce::Colour (0xFFFF2040);
        t.slicePalette[8 ] = juce::Colour (0xFF40FF50);
        t.slicePalette[9 ] = juce::Colour (0xFFFF40B0);
        t.slicePalette[10] = juce::Colour (0xFF20A8FF);
        t.slicePalette[11] = juce::Colour (0xFFFFBB00);
        t.slicePalette[12] = juce::Colour (0xFFFF5070);
        t.slicePalette[13] = juce::Colour (0xFF50D8FF);
        t.slicePalette[14] = juce::Colour (0xFFC8FF00);
        t.slicePalette[15] = juce::Colour (0xFFAA80FF);
        return t;
    }

    static juce::Colour parseHex (const juce::String& hex)
    {
        return juce::Colour ((juce::uint32) (0xFF000000 | hex.getHexValue32()));
    }

    static ThemeData fromThemeFile (const juce::String& text)
    {
        ThemeData t = darkTheme(); // defaults
        if (text.contains("name: midnight")) return midnightTheme();
        if (text.contains("name: pigments")) return pigmentsTheme();
        if (text.contains("name: cr8"))      return cr8Theme();
        if (text.contains("name: dysekt"))   return dysektTheme();
        if (text.contains("name: serum"))    return serumTheme();
        if (text.contains("name: opendaw"))  return opendawTheme();

        for (auto line : juce::StringArray::fromLines (text))
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWith ("#"))
                continue;

            int colonIdx = line.indexOf (":");
            if (colonIdx < 0)
                continue;

            auto key = line.substring (0, colonIdx).trim();
            auto val = line.substring (colonIdx + 1).trim().unquoted();

            // Strip inline comments (  # ...)
            int hashIdx = val.indexOf (" #");
            if (hashIdx >= 0)
                val = val.substring (0, hashIdx).trimEnd();

            if (key == "name")            t.name = val;
            else if (key == "background")    t.background = parseHex (val);
            else if (key == "waveformBg")    t.waveformBg = parseHex (val);
            else if (key == "darkBar")       t.darkBar = parseHex (val);
            else if (key == "foreground")    t.foreground = parseHex (val);
            else if (key == "header")    t.header = parseHex (val);
            else if (key == "waveform") t.waveform = parseHex (val);
            else if (key == "selectionOverlay") t.selectionOverlay = parseHex (val);
            else if (key == "lockActive")      t.lockActive = parseHex (val);
            else if (key == "lockInactive")       t.lockInactive = parseHex (val);
            else if (key == "gridLine")      t.gridLine = parseHex (val);
            else if (key == "accent")        t.accent = parseHex (val);
            else if (key == "button")        t.button = parseHex (val);
            else if (key == "buttonHover")   t.buttonHover = parseHex (val);
            else if (key == "separator")     t.separator = parseHex (val);
            else if (key.startsWith ("slice"))
            {
                int idx = key.substring (5).getIntValue() - 1;
                if (idx >= 0 && idx < 16)
                    t.slicePalette[idx] = parseHex (val);
            }
        }
        return t;
    }

    static juce::String colourToHex (juce::Colour c)
    {
        return juce::String::toHexString ((int) (c.getARGB() & 0x00FFFFFF)).paddedLeft ('0', 6);
    }

    juce::String toThemeFile() const
    {
        juce::String s;
        s << "name: " << name << "\n";
        s << "background: " << colourToHex (background) << "\n";
        s << "waveformBg: " << colourToHex (waveformBg) << "\n";
        s << "darkBar: " << colourToHex (darkBar) << "\n";
        s << "foreground: " << colourToHex (foreground) << "\n";
        s << "header: " << colourToHex (header) << "\n";
        s << "waveform: " << colourToHex (waveform) << "\n";
        s << "selectionOverlay: " << colourToHex (selectionOverlay) << "\n";
        s << "lockActive: " << colourToHex (lockActive) << "\n";
        s << "lockInactive: " << colourToHex (lockInactive) << "\n";
        s << "gridLine: " << colourToHex (gridLine) << "\n";
        s << "accent: " << colourToHex (accent) << "\n";
        s << "button: " << colourToHex (button) << "\n";
        s << "buttonHover: " << colourToHex (buttonHover) << "\n";
        s << "separator: " << colourToHex (separator) << "\n";
        for (int i = 0; i < 16; ++i)
            s << "slice" << (i + 1) << ": " << colourToHex (slicePalette[i]) << "\n";
        return s;
    }
};
