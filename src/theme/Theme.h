#pragma once

#include <QColor>

// All visual constants derived directly from Telegram Desktop's day-blue theme.
// Source: Telegram/Resources/day-blue.tdesktop-theme and dialogs.style
namespace Theme {

namespace Color {

// Backgrounds
inline const QColor WindowBg      { 255, 255, 255 };
inline const QColor SidebarBg     { 255, 255, 255 };
inline const QColor NavBarBg      { 245, 245, 245 };
inline const QColor ItemHover     { 241, 241, 241 };
inline const QColor ItemSelected  {  65, 159, 217 };

// Text — normal state
inline const QColor TextPrimary   {  34,  34,  34 };
inline const QColor TextSecondary { 153, 153, 153 };

// Text — selected state
inline const QColor TextOnAccent  { 255, 255, 255 };

// Accent
inline const QColor Accent        {  64, 167, 227 };
inline const QColor AccentHover   {  50, 140, 200 };
inline const QColor AccentPressed {  40, 115, 170 };

// Dividers
inline const QColor Divider       { 224, 224, 224 };

// Search bar
inline const QColor SearchBg      { 241, 241, 241 };
inline const QColor SearchBorder  {  84, 195, 243 };
inline const QColor SearchIcon    { 153, 153, 153 };
inline const QColor Placeholder   { 170, 170, 170 };

// Transfer progress bar
inline const QColor ProgressBg    { 224, 224, 224 };
inline const QColor ProgressFg    {  64, 167, 227 };

} // namespace Color

namespace Space {

inline constexpr int WindowMinWidth   =  820;
inline constexpr int WindowMinHeight  =  520;
inline constexpr int WindowInitWidth  = 1024;
inline constexpr int WindowInitHeight =  680;
inline constexpr int NavBarWidth     = 48;
inline constexpr int SidebarWidth    = 320;

inline constexpr int ItemHeight      = 52;
inline constexpr int ItemPadLeft     =  8;
inline constexpr int ItemPadTop      =  8;
inline constexpr int AvatarSize      = 36;

// Text block centred within ItemHeight=52.
// nameFont(13px)≈17px + 3px gap + captionFont(12px)≈16px = 36px block
// blockTop = (52-36)/2 = 8 → NameTop=8, SubtitleTop=8+17+3=28
inline constexpr int NameTop         =  8;
inline constexpr int SubtitleTop     = 28;

inline constexpr int TextLeft        = 52; // 8 pad + 36 avatar + 8 gap

inline constexpr int PaddingS  =  4;
inline constexpr int PaddingM  =  8;
inline constexpr int PaddingL  = 16;
inline constexpr int PaddingXL = 24;

inline constexpr int IconSize     = 20;
inline constexpr int DividerThick =  1;

inline constexpr int RadiusS =  4;
inline constexpr int RadiusM =  8;
inline constexpr int RadiusL = 12;

inline constexpr int SearchHeight   = 28;
inline constexpr int SearchRadius   = 14;
inline constexpr int SearchBorderW  =  2;
inline constexpr int SearchPadLeft  = 12;
inline constexpr int SearchVPad     =  6;

inline constexpr int InputBarHeight  = 44;
inline constexpr int InputBtnW       = 40;
inline constexpr int InputBtnH       = 24;

// Image message preview dimensions (logical pixels)
inline constexpr int ImagePreviewH      = 180;
inline constexpr int ImagePreviewMaxW   = 320;
inline constexpr int ImagePreviewRadius =   8;

// Chat header
inline constexpr int ChatHeaderH        =  44;

// Peer panel
inline constexpr int PeerPanelWidth     = 280;
inline constexpr int PeerPanelHeaderH   =  64;
inline constexpr int PeerPanelRowH      =  52;
inline constexpr int PeerPanelLabelH    =  36;
inline constexpr int PanelPadH          =  16; // symmetric horizontal padding

// Message row layout (multiples of 4px)
inline constexpr int MsgAvatarSize      =  32;
inline constexpr int MsgRowPadV         =   8;
inline constexpr int MsgRowPadH         =  12;
inline constexpr int MsgNameH           =  20; // line height for 12pt sender name
inline constexpr int MsgNameGap         =   4; // gap between sender name and content

// Scroll-to-bottom button in ChatView
inline constexpr int ScrollBtnSize      =  36;
inline constexpr int ScrollBtnMarginR   =  16;
inline constexpr int ScrollBtnMarginB   =  16;

} // namespace Space

namespace Font {

inline constexpr int SizeSmall   = 11; // timestamps, tertiary hints
inline constexpr int SizeCaption = 12; // peer subtitle, secondary labels
inline constexpr int SizeBody    = 13; // primary content, inputs, search
inline constexpr int SizeNormal  = 13; // alias kept for SearchBar
inline constexpr int SizeSubtitle = 15; // section sub-headings
inline constexpr int SizeLarge   = 18; // chat header peer name

} // namespace Font

} // namespace Theme
