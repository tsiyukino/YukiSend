# 2026-05-18 — Notion-style UI redesign

## Context

YukiSend's original visual language was derived from Telegram Desktop's day-blue
theme: blue accent backgrounds on selection, blue NavBar active state, blue
focus borders, and a high-chroma colour palette. While functional, the result
felt heavy relative to the app's purpose (local file transfer, no cloud accounts
needed).

The goal was to adopt Notion's visual language: warm whites, near-black text,
achromatic hover/selection states, and accent colour reserved only for
confirmations and focus rings.

## Decision

Replace all colour constants in `src/theme/Theme.h` with a Notion-aligned
palette. Adjust paintEvent implementations in the affected UI components to
match the new semantics. No structural changes to layout, components, or
behaviour.

**Specific changes:**

- `NavBarBg`: `#F5F5F5` → `#F7F6F3` (warm grey, matches Notion left panel)
- `ItemSelected`: blue `rgb(65,159,217)` → achromatic `rgb(232,232,231)`
- `ItemHover`: near-same grey, slightly lighter
- `TextOnAccent`: white → same as `TextPrimary` (dark text survives on the new
  achromatic selection background)
- `Accent`: Telegram blue `rgb(64,167,227)` → Notion blue `rgb(35,131,226)`
  (used only for progress bars, copy-flash ✓, focus rings, drop overlay)
- `NavBar` active indicator: blue filled rounded-rect → `ItemSelected` bg +
  2px near-black left stripe
- `PeerItemDelegate` selected indicator: blue bg + white text → `ItemSelected`
  bg + 3px near-black left stripe + unchanged text colours
- `SearchBar` focus border: 2px blue → 1px grey at 60% opacity
- `ChatInputBar` top border: animated blue on focus → static `Divider` hairline
- Text cursors in `SearchBar` and `ChatInputBar`: blue → `TextPrimary`
- Scroll-to-bottom button: blue circle → white circle with border + dark chevron
- Avatar colours: adjusted to lower saturation to match the calmer palette

## What was kept

- All layout constants (item heights, sidebar width, padding grid)
- All component structure and interaction logic
- Animated hover/selection transitions
- Coloured avatars (still per-peer deterministic, just slightly desaturated)
- Blue accent for actionable confirmations (progress fill, copy flash)

## Rejected alternatives

**Dark mode**: out of scope for v1; Notion defaults to light.

**Removing NavBar**: restructuring to a Notion-style expandable sidebar would
require layout changes across all components. Deferred.
