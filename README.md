# Introduction

This is a save game editor for **The Legend of Zelda: The Wind Waker HD** (Wii U).
It reads the raw `cking.sav` file of one user profile and lets you change quest-log name, hearts, rupees, wallet size, magic, arrows, bombs, equipment and every major inventory item.
Everything here is free and open source (GPL v3) and comes with **absolutely zero warranty**.
You are editing your own save data at your own risk, so keep a backup of the original file before you write anything back to the console.
You must also legally own a Wii U, along with a legally owned copy of the game.

**List of features:**
- **Edit all three slots** in a single `cking.sav`, edited independently and shown side by side.
- **Remove special items such as the swift sail** correctly. The swift sail is a buggy/broken item, and the permanent on-screen button prompt breaks immersion while sailing.
- **Live change review** - every pending edit is listed as `old -> new` before you save, computed from the exact bytes a save would write.
- **Correct "obtained" masks.** Removing or downgrading an item also lowers the owned-variant mask, otherwise the game silently restores the better item on the next in-game save.
- **Automatic checksums.** The edited quest log is re-summed on the fly; one button re-sums all three if you are repairing a file.
- **Amount clamping** - current hearts never exceed max hearts, rupees never exceed the wallet capacity, etc.
- **Ships as an AppImage** for universal compatibility (AnyLinux AppImage build).
- **Command line version** also available.

<p align="center">
<img alt="wwedit" src="https://github.com/user-attachments/assets/1c28a4de-9109-4baa-a389-5e5bb5a2b8bc" width="90%" />
</p>

# What can the editor change?
Each of the three slots can be modified:
- **Name** (1-8 characters, written to the display name and the three internal copies).
- **Max hearts / current hearts**, in quarter-heart steps up to 20.
- **Rupees** and **wallet capacity** (200, 1000 or 5000).
- **Max magic meter** (0, 16 or 32) and **current magic**.
- **Arrows, max arrows, bombs, max bombs**.
- **Equipment:** sword (Hero's / Master Sword powerless, half, fully restored), shield (Hero's / Mirror), Power Bracelets.
- **Inventory:** Telescope, Sail (normal / swift), Wind Waker, Grappling Hook, Spoils Bag, Boomerang, Deku Leaf, Tingle Bottle, Picto Box (normal / deluxe), Iron Boots, Magic Armor, Bait Bag, Bow (Hero's / Fire & Ice / Light), Bombs, Delivery Bag, Hookshot, Skull Hammer and the four bottle slots (empty bottle, potions, elixir soup, water, fairy, forest firefly, forest water).

# Getting the editor
## Download
Grab the latest AppImage from the [releases page](https://github.com/vishnu350/windwaker-hd-editor/releases/latest), make the binary executable and double click to run it.

## Build from source
Dependencies: a C99/C++11 compiler, GLFW and X11 headers.

```bash
sudo apt-get install build-essential libglfw3-dev libx11-dev   # Debian/Ubuntu
git clone --recursive https://github.com/vishnu350/windwaker-hd-editor
cd windwaker-hd-editor        # or: git submodule update --init
make                          # builds the CLI, plus the GUI when GLFW/X11 are available
```

| Target | Result |
| --- | --- |
| `make` | `windwaker-hd-editor-cli` and `windwaker-hd-editor` (GUI, if GLFW is installed) |
| `make gui` | GUI only, with an install hint if the libraries are missing |
| `make info` | print the three quest logs (needs a sample `cking.sav`) |
| `make help` | CLI usage text |
| `make test` | parity + behaviour tests |
| `make clean` | remove build output |

Dear ImGui is a git submodule compiled straight into the GUI binary, per the upstream recommendation.

# Using the GUI editor
1. Start `windwaker-hd-editor` (double-click the AppImage, or run the binary; you can also pass a save file as the first argument to open it immediately).
2. Click **Browse...**, pick your `cking.sav`, and it loads right away. The status line turns green with `Loaded ...` or red with the reason it was rejected.
3. Select the quest log tab you want to work on. The left column holds the widgets, the right column lists **every pending change** as `old -> new`, plus how many bytes differ.
4. Edit what you like. Values are clamped for you: dragging hearts snaps to quarter hearts, rupees follow the wallet capacity, current never exceeds max.
5. Click **Save modified copy**. The file goes to the **Save as** path, which defaults to
   `<name>-modified.sav` next to the file you loaded. Your original is never overwritten.
6. Put that file back with whatever save manager you already use for save restore, and **test it in-game before deleting your backup**.

Other buttons:
- **Repair all checksums** - recalculates all three quest logs without changing anything else. Useful when a file was corrupted by another tool, or after a manual byte-level fix.
- **Discard edits** - reloads the working copy from the file you opened.
- **Scale** - 100% to 250%, applied to fonts and spacing. Text too small on a HiDPI screen? Set it here.

**Note**: Only the raw 8966-byte `cking.sav` is accepted.

# Thanks & How To Contribute
Like this project?
- Please ⭐️ this repository and follow me if this project helped you.
- Open an [issue](https://github.com/vishnu350/windwaker-hd-editor/issues) with the offending byte values if something is mislabelled. PRs are welcome.
- Buy me a cup of [coffee](https://github.com/sponsors/vishnu350?frequency=one-time)

Credits and thanks to the following folk:
- **ocornut** and contributors for [Dear ImGui](https://github.com/ocornut/imgui), the GUI framework this editor is built on.
- The authors of publicly documented Wind Waker HD save-format research [here](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp).
- The pkgforge team for the awesome [sharun](https://github.com/pkgforge-dev/Anylinux-AppImages) script for building AnyLinux AppImages.
