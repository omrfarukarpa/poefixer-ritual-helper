# Ritual Helper

**v1.1.0**

A PoeFixer plugin for **Path of Exile 2** that automates **Ritual defers**: pick the
items you care about from a live price catalog (or set a value threshold), and one click
defers every matching reward in the Favours window — so the valuable stuff stays
available for a later ritual instead of slipping by.

Unofficial third-party game tool. Maintainer: Ömer Faruk ARPA.

## Features

- **One-click defer** — a `DEFER (N)` button appears next to the hourglass whenever the
  open Favours window contains matching items. It enters defer mode, clicks the matched
  items and applies. Right-click cancels a running sequence at any time.
- **Pick items from a catalog** — a searchable checkbox list of every priced item from
  poe2scout: currency, omens, essences, fragments and **all uniques** (weapons, armour,
  accessories, jewels, flasks), each shown with its current price.
- **Value threshold** — optionally defer *any* revealed item worth at least X
  **Exalted or Divine** (live poe2scout prices, converted automatically). Catch the
  expensive surprise uniques without selecting them one by one.
- **Season / league selection** — choose the current or an older softcore league from
  the poe2scout list. The default **Auto (current league)** follows the live season.
- **Live prices, auto-refreshed** — prices update in the background on a 15–60 minute
  interval (default 30) plus a manual refresh button. Changing the selected league
  refreshes the catalog. Fetching runs off-thread; the game never stalls.
- **Value labels** — matched items show their worth (`2.3 div` / `15 ex`) right on the
  ritual window.
- **Safety first** — the click sequence verifies defer mode is actually active before
  touching any item (so it can never accidentally buy), aborts on focus loss, has a
  watchdog timeout, and restores your cursor position afterwards. A dry-run mode
  (under Debug) shows what would be clicked without sending input.

## Install

1. Download `RitualHelper.dll` from a release (or build it, see below).
2. Copy it to `…\fixer\Plugins\RitualHelper\RitualHelper.dll`.
3. Enable **Ritual Helper** in the PoeFixer plugin list, tick the items you want in
   **Defer items** (or set a min value), and open a ritual.

Prices come from poe2scout.com, so the price features need an internet connection;
everything else works offline.

## Build

Requires MSVC (v14x toolset, C++20), x64. From the plugin folder:

```
MSBuild.exe RitualHelper.sln -p:Configuration=Release -p:Platform=x64 -m
```

Output: `bin\Release\RitualHelper.dll`. The `sdk/`, `imgui/` and `third_party/` folders
are vendored so the plugin builds standalone.

## Notes

- Unrevealed ("Hidden Item") rewards are never auto-deferred — their identity can't be
  read, and spending tribute blindly is your call, not the plugin's.
- If the Favours window ever stops being detected after a game patch, enable **Debug
  mode** and use **Write ritual dump**; the dump shows exactly what the game exposes so
  the detection can be fixed quickly.
