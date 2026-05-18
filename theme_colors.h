// theme_colors.h
// Centralized color palette for the entire UI.
// All IM_COL32 constants live here; UI code references these by name.
#pragma once

#include "third_party/imgui/imgui.h"

// ============================================================
// Helper: pick dark or light variant based on current theme
// ============================================================
// Declared in UI.cpp — returns g_shell_ptr->dark_theme (defaults true).
namespace ui { bool IsDarkTheme(); }

struct ThemePair {
    ImU32 dark;
    ImU32 light;
    inline ImU32 Get() const { return ui::IsDarkTheme() ? dark : light; }
    inline operator ImU32() const { return Get(); }
};

// ============================================================
// FIXED COLORS — identical in both themes
// ============================================================
namespace colors
{
    // --- Status / Security Center icons ---
    constexpr ImU32 StatusWeak      = IM_COL32(174, 41, 41, 255);   // red
    constexpr ImU32 StatusReused    = IM_COL32(190, 156, 63, 255);  // gold
    constexpr ImU32 StatusExposed   = IM_COL32(142, 68, 173, 255);  // purple
    constexpr ImU32 StatusAging     = IM_COL32(39, 174, 157, 255);  // teal

    // --- Status row tints (semi-transparent backgrounds) ---
    constexpr ImU32 TintWeak        = IM_COL32(174, 41, 41, 30);
    constexpr ImU32 TintReused      = IM_COL32(190, 156, 63, 30);
    constexpr ImU32 TintExposed     = IM_COL32(142, 68, 173, 20);
    constexpr ImU32 TintAging       = IM_COL32(39, 174, 157, 20);

    // --- Toast notification backgrounds ---
    constexpr ImU32 ToastSuccess    = IM_COL32(46, 160, 67, 255);   // green
    constexpr ImU32 ToastError      = IM_COL32(180, 50, 50, 255);   // red
    constexpr ImU32 ToastInfo       = IM_COL32(56, 132, 196, 255);  // blue
    constexpr ImU32 ToastDefault    = IM_COL32(60, 60, 60, 255);    // gray

    // --- Password strength meter ---
    constexpr ImU32 StrengthStrong  = IM_COL32(76, 195, 100, 255);  // green
    constexpr ImU32 StrengthMedium  = IM_COL32(255, 183, 77, 255);  // amber
    constexpr ImU32 StrengthWeak    = IM_COL32(229, 80, 80, 255);   // red

    // --- Error / destructive actions ---
    constexpr ImU32 ErrorText       = IM_COL32(220, 50, 50, 255);
    constexpr ImU32 ErrorTextAlt    = IM_COL32(220, 80, 80, 255);
    constexpr ImU32 DeleteTint      = IM_COL32(180, 40, 40, 50);

    // --- Timer / expiry badges ---
    constexpr ImU32 TimerGood       = IM_COL32(80, 180, 90, 200);   // green
    constexpr ImU32 TimerWarning    = IM_COL32(240, 140, 60, 200);  // orange
    constexpr ImU32 TimerExpired    = IM_COL32(174, 80, 80, 200);   // red

    // --- Accent / change highlights ---
    constexpr ImU32 AccentOrange    = IM_COL32(255, 98, 4, 255);
    constexpr ImU32 ChangedRowTint  = IM_COL32(255, 98, 4, 25);     // ~10% orange
    constexpr ImU32 NewRowTint      = IM_COL32(255, 98, 4, 40);     // ~16% orange
    constexpr ImU32 FieldHighlight  = IM_COL32(255, 98, 4, 18);     // subtle orange wash

    // --- Strength bar colors (verify button) ---
    constexpr ImU32 VerifyGood      = IM_COL32(60, 180, 75, 255);
    constexpr ImU32 VerifyBad       = IM_COL32(220, 50, 47, 255);
    constexpr ImU32 VerifyWarn      = IM_COL32(230, 160, 30, 255);

    // --- Dimming overlay ---
    constexpr ImU32 DimOverlay      = IM_COL32(0, 0, 0, 150);
    constexpr ImU32 DimOverlayLight = IM_COL32(0, 0, 0, 140);

    // --- Transparent ---
    constexpr ImU32 Transparent     = IM_COL32(0, 0, 0, 0);

    // --- Sync status dots ---
    constexpr ImU32 SyncOnline      = IM_COL32(30, 80, 160, 255);
    constexpr ImU32 SyncOnlineLight = IM_COL32(160, 195, 240, 255);
    constexpr ImU32 SyncPending     = IM_COL32(50, 100, 180, 200);
    constexpr ImU32 SyncPendingLight= IM_COL32(130, 170, 220, 200);
    constexpr ImU32 SyncWarning     = IM_COL32(190, 156, 63, 200);
}

// ============================================================
// THEMED COLORS — dark/light pairs
// ============================================================
namespace theme
{
    // --- Popup / dropdown backgrounds ---
    inline const ThemePair PopupBg          = { IM_COL32(26, 26, 28, 255),     IM_COL32(250, 250, 252, 255) };
    inline const ThemePair PopupBorder      = { IM_COL32(255, 255, 255, 25),   IM_COL32(0, 0, 0, 30) };

    // --- Modal backgrounds ---
    inline const ThemePair ModalBg          = { IM_COL32(36, 35, 39, 255),     IM_COL32(250, 250, 252, 255) };
    inline const ThemePair ModalBgAlt       = { IM_COL32(36, 35, 39, 245),     IM_COL32(250, 250, 252, 245) };

    // --- Dropdown body ---
    inline const ThemePair DropdownBg       = { IM_COL32(30, 30, 34, 245),     IM_COL32(235, 235, 240, 245) };
    inline const ThemePair DropdownShadow   = { IM_COL32(0, 0, 0, 80),         IM_COL32(0, 0, 0, 40) };

    // --- Beveled border (dropdown / raised container) ---
    inline const ThemePair BevelTL          = { IM_COL32(255, 255, 255, 25),   IM_COL32(0, 0, 0, 12) };
    inline const ThemePair BevelBR          = { IM_COL32(255, 255, 255, 8),    IM_COL32(0, 0, 0, 55) };
    inline const ThemePair BevelTR          = { IM_COL32(255, 255, 255, 8),    IM_COL32(0, 0, 0, 53) };
    inline const ThemePair BevelBL          = { IM_COL32(255, 255, 255, 8),    IM_COL32(0, 0, 0, 48) };

    // --- Pill / badge chip ---
    inline const ThemePair PillBg           = { IM_COL32(40, 40, 43, 255),     IM_COL32(235, 235, 238, 255) };
    inline const ThemePair PillHoverBg      = { IM_COL32(50, 50, 54, 255),     IM_COL32(225, 225, 228, 255) };
    inline const ThemePair PillInnerLip     = { IM_COL32(13, 18, 26, 92),      IM_COL32(71, 84, 102, 56) };
    inline const ThemePair PillDivider      = { IM_COL32(255, 255, 255, 12),   IM_COL32(0, 0, 0, 15) };

    // --- Card / accordion lifted surface ---
    inline const ThemePair CardSurface      = { IM_COL32(255, 255, 255, 6),    IM_COL32(0, 0, 0, 6) };
    inline const ThemePair CardBorderOuter  = { IM_COL32(0, 0, 0, 80),         IM_COL32(0, 0, 0, 35) };
    inline const ThemePair CardBorderInner  = { IM_COL32(255, 255, 255, 30),   IM_COL32(255, 255, 255, 180) };

    // --- Copy row / field highlight ---
    inline const ThemePair RowSeparatorTop  = { IM_COL32(0, 0, 0, 30),         IM_COL32(0, 0, 0, 18) };
    inline const ThemePair RowSeparatorBot  = { IM_COL32(0, 0, 0, 50),         IM_COL32(0, 0, 0, 30) };
    inline const ThemePair RowHighlight     = { IM_COL32(255, 255, 255, 18),   IM_COL32(255, 255, 255, 140) };

    // --- Toggle / scrollbar face ---
    inline const ThemePair ToggleNormal     = { IM_COL32(0x22, 0x22, 0x22, 255), IM_COL32(0xF0, 0xF0, 0xF0, 255) };
    inline const ThemePair ToggleHovered    = { IM_COL32(0x2A, 0x2A, 0x2A, 255), IM_COL32(0xE8, 0xE8, 0xE8, 255) };
    inline const ThemePair ToggleHeld       = { IM_COL32(0x1A, 0x1A, 0x1A, 255), IM_COL32(0xD8, 0xD8, 0xD8, 255) };
    inline const ThemePair ToggleShadow     = { IM_COL32(0, 0, 0, 100),        IM_COL32(0, 0, 0, 35) };
    inline const ThemePair ToggleGlintNorm  = { IM_COL32(255, 255, 255, 6),    IM_COL32(255, 255, 255, 140) };
    inline const ThemePair ToggleGlintHov   = { IM_COL32(255, 255, 255, 12),   IM_COL32(255, 255, 255, 200) };

    // --- Toggle track inactive background ---
    inline const ThemePair ToggleInactiveBg = { IM_COL32(42, 42, 46, 255),     IM_COL32(215, 215, 220, 255) };

    // --- Separator / border ---
    inline const ThemePair Separator        = { IM_COL32(255, 255, 255, 15),   IM_COL32(0, 0, 0, 15) };
    inline const ThemePair Border           = { IM_COL32(40, 40, 40, 255),     IM_COL32(200, 200, 200, 255) };

    // --- Scrollbar track ---
    inline const ThemePair ScrollTrack      = { IM_COL32(60, 60, 65, 255),     IM_COL32(224, 224, 224, 255) };

    // --- Text ---
    inline const ThemePair TextPrimary      = { IM_COL32(255, 255, 255, 255),  IM_COL32(30, 30, 30, 255) };
    inline const ThemePair TextMuted        = { IM_COL32(150, 150, 155, 255),  IM_COL32(120, 120, 120, 255) };

    // --- Table / three-pane header ---
    inline const ThemePair PanelHeaderBg    = { IM_COL32(33, 33, 36, 255),     IM_COL32(235, 235, 238, 255) };
    inline const ThemePair PanelLip         = { IM_COL32(15, 15, 18, 255),     IM_COL32(195, 195, 200, 255) };

    // --- Three-pane surface ---
    inline const ThemePair PanelBg          = { IM_COL32(38, 37, 42, 255),     IM_COL32(248, 248, 250, 255) };

    // --- Button / interactive area hover ---
    inline const ThemePair ButtonBg         = { IM_COL32(42, 42, 42, 102),     IM_COL32(220, 220, 220, 255) };
    inline const ThemePair ButtonHoverBg    = { IM_COL32(53, 54, 61, 150),     IM_COL32(200, 200, 200, 255) };

    // --- Tooltip ---
    inline const ThemePair TooltipBg        = { IM_COL32(35, 34, 37, 255),     IM_COL32(250, 250, 252, 255) };
    inline const ThemePair TooltipBorder    = { IM_COL32(60, 60, 66, 255),     IM_COL32(220, 220, 225, 255) };

    // --- Combo / input field ---
    inline const ThemePair InputBg          = { IM_COL32(40, 40, 46, 255),     IM_COL32(220, 220, 228, 255) };
    inline const ThemePair InputBorder      = { IM_COL32(60, 60, 60, 255),     IM_COL32(200, 200, 200, 255) };

    // --- Delete row error highlight ---
    inline const ThemePair ErrorHighlight   = { IM_COL32(255, 80, 80, 60),     IM_COL32(255, 0, 0, 30) };

    // --- Misc button text ---
    inline const ThemePair DeleteBtnText    = { IM_COL32(255, 100, 100, 255),  IM_COL32(220, 50, 50, 255) };
    inline const ThemePair DeleteBtnDisabled= { IM_COL32(160, 160, 160, 255),  IM_COL32(120, 120, 120, 255) };

    // --- Checkbox/radio mark overlay ---
    inline const ThemePair OverlayFaint     = { IM_COL32(255, 255, 255, 8),    IM_COL32(0, 0, 0, 6) };
    inline const ThemePair OverlaySubtle    = { IM_COL32(255, 255, 255, 20),   IM_COL32(0, 0, 0, 15) };

    // --- Context menu item highlight ---
    inline const ThemePair MenuItemHover    = { IM_COL32(255, 255, 255, 8),    IM_COL32(255, 255, 255, 120) };
    inline const ThemePair MenuItemBorder   = { IM_COL32(0, 0, 0, 30),         IM_COL32(0, 0, 0, 18) };

    // --- Splitter / resize handle ---
    inline const ThemePair SplitterHover    = { IM_COL32(255, 255, 255, 80),   IM_COL32(255, 255, 255, 8) };
    inline const ThemePair SplitterShadow   = { IM_COL32(0, 0, 0, 60),         IM_COL32(0, 0, 0, 30) };

    // --- Combo container text ---
    inline const ThemePair ComboText        = { IM_COL32(255, 255, 255, 255),  IM_COL32(55, 55, 60, 255) };
    inline const ThemePair ComboActiveText  = { IM_COL32(90, 90, 100, 255),    IM_COL32(210, 210, 218, 255) };
    inline const ThemePair ComboHoverBg     = { IM_COL32(45, 45, 50, 255),     IM_COL32(210, 210, 218, 255) };
    inline const ThemePair ComboActiveBg    = { IM_COL32(75, 75, 82, 255),     IM_COL32(230, 230, 235, 255) };

    // --- Combo shadow ring ---
    inline const ThemePair ComboShadow      = { IM_COL32(0, 0, 0, 40),         IM_COL32(0, 0, 0, 20) };

    // --- Window/child background (used in soft containers) ---
    inline const ThemePair WindowBg         = { IM_COL32(28, 27, 30, 255),     IM_COL32(245, 245, 247, 255) };
    inline const ThemePair ChildBg          = { IM_COL32(30, 30, 33, 255),     IM_COL32(252, 252, 254, 255) };

    // --- Scrollbar pill text (digits) ---
    inline const ThemePair ScrollDigitBg    = { IM_COL32(30, 30, 30, 230),     IM_COL32(255, 255, 255, 230) };
    inline const ThemePair ScrollDigitBorder= { IM_COL32(80, 80, 80, 140),     IM_COL32(255, 255, 255, 100) };
}
