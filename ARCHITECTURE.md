# ROMi - ROM Downloader for PS3

## Overview

A PKGi-style ROM downloader for jailbroken PS3 that downloads ROMs from HTTP sources, extracts archives, and organizes files into the correct folders for each platform.

## Components

```
┌─────────────────────────────────────────────────────────┐
│                   Myrient Indexer (PC)                   │
│  Python script that crawls myrient, generates TSV DBs   │
│  Runs: cron job, GitHub Actions, or manual              │
└───────────────────────┬─────────────────────────────────┘
                        │ uploads
                        ▼
┌─────────────────────────────────────────────────────────┐
│               Database Host (GitHub/HTTP)               │
│  romi_psx.tsv, romi_ps2.tsv, romi_nes.tsv, etc.        │
└───────────────────────┬─────────────────────────────────┘
                        │ downloads
                        ▼
┌─────────────────────────────────────────────────────────┐
│                    ROMi PS3 App                          │
│  Loads DBs → Browse/Search → Download → Extract → Save  │
└─────────────────────────────────────────────────────────┘
```

## Target Folder Structure (PS3)

```
/dev_hdd0/PS3ISO/      - PS3 games
/dev_hdd0/PS2ISO/      - PS2 games
/dev_hdd0/PSXISO/      - PS1 games
/dev_hdd0/ROMS/        - RetroArch ROMs
  ├── NES/
  ├── SNES/
  ├── GB/
  ├── GBA/
  ├── Genesis/
  └── ...
```

## Database Format (TSV)

```
PLATFORM	REGION	NAME	URL	SIZE
PSX	USA	Crash Bandicoot	https://myrient.erista.me/files/...	123456789
PS2	EUR	Gran Turismo 4	https://myrient.erista.me/files/...	4500000000
NES	USA	Super Mario Bros	https://myrient.erista.me/files/...	24576
```

## PS3 App Modules

| Module | Purpose | Based On |
|--------|---------|----------|
| `romi.c` | Main app, state machine | `pkgi.c` |
| `romi_db.c` | ROM database parsing | `pkgi_db.c` |
| `romi_download.c` | HTTP download with resume | `pkgi_download.c` |
| `romi_extract.c` | ZIP extraction (minizip) | New |
| `romi_storage.c` | Path management by platform | New |
| `romi_config.c` | Settings and DB URLs | `pkgi_config.c` |
| `romi_menu.c` | UI menus | `pkgi_menu.c` |
| `romi_dialog.c` | Dialogs | `pkgi_dialog.c` |
| `romi_ps3.c` | Platform layer | `pkgi_ps3.c` |

## Platform Enum

```c
typedef enum {
    PlatformUnknown = 0,
    PlatformPSX,
    PlatformPS2,
    PlatformPS3,
    PlatformPSP,
    PlatformNES,
    PlatformSNES,
    PlatformN64,
    PlatformGB,
    PlatformGBC,
    PlatformGBA,
    PlatformDS,
    PlatformGC,
    PlatformGenesis,
    PlatformSMS,
    PlatformMAME,
    PlatformPC,
    PlatformCount
} RomiPlatform;
```

## Dependencies

### PS3 App (PSL1GHT)
- libcurl (HTTP downloads)
- ya2d (graphics)
- mini18n (localization)
- polarssl (crypto)
- minizip/zlib (ZIP extraction)

### Indexer Tool (Python 3.8+)
- aiohttp
- beautifulsoup4
- lxml

## Localization System

### Overview
ROMi uses mini18n for runtime internationalization with support for 9 languages:
- German (de)
- Spanish (es)
- Finnish (fi)
- French (fr)
- Indonesian (id)
- Italian (it)
- Polish (pl)
- Portuguese (pt)
- Turkish (tr)

### Translation Workflow

```
┌─────────────────────────────────────────────────────────┐
│  pkgfiles/USRDIR/LANG/*.po (Source of Truth)            │
│  Human-editable translation files in PO format          │
└───────────────────────┬─────────────────────────────────┘
                        │ compile (during build)
                        │ tools/po_to_yts.py
                        ▼
┌─────────────────────────────────────────────────────────┐
│  build/pkg/USRDIR/LANG/*.yts (Compiled)                 │
│  Binary translation files loaded by mini18n at runtime  │
└───────────────────────┬─────────────────────────────────┘
                        │ packaged into
                        ▼
┌─────────────────────────────────────────────────────────┐
│  src.pkg → RPCS3/PS3                                    │
│  Runtime loads .yts based on system language setting    │
└─────────────────────────────────────────────────────────┘
```

### File Formats

**PO Files (.po)** - Source format (editable):
```
msgid "Download"
msgstr "Herunterladen"

msgid "Cancel"
msgstr "Abbrechen"
```

**YTS Files (.yts)** - Compiled format (key|value pairs):
```
Download|Herunterladen
Cancel|Abbrechen
```

### Build Process

**Automatic Translation Compilation** - The Makefile has a `compile-translations` target that:
- Runs BEFORE every package build (dependency of `pkg` target)
- Compiles all .po files to .yts automatically
- Works for ALL build types: `docker-build`, `docker-build-debug`, `rpcs3-build`, `pkg`

```makefile
compile-translations:
	@cd $(PKGFILES)/USRDIR/LANG && for po in *.po; do \
		python3 $(CURDIR)/tools/po_to_yts.py "$$po" "$${po%.po}.yts" || true; \
	done

pkg: $(BUILD) compile-translations $(OUTPUT).pkg
```

### Adding New Translations

1. Edit `pkgfiles/USRDIR/LANG/*.po` files (all languages)
2. Add new msgid/msgstr pairs following existing format
3. Run `make docker-build` - translations compile automatically
4. .yts files are generated from .po files on every build - no manual steps

### Translation Macro Usage

In C code, wrap translatable strings with `_()`:
```c
romi_dialog_message(_("Download failed"));
romi_snprintf(text, sizeof(text), "%s %s", _("Press"), _("to continue"));
```

### Important Notes
- **.po files** are the single source of truth - edit these only
- **.yts files** are automatically generated during build - do not edit manually
- Build system ensures .yts files match .po files
- Source .yts files in pkgfiles/ may be outdated - build/ versions are always current

## Data Source

Primary: https://myrient.erista.me/files/
- No-Intro: Cartridge-based ROMs (NES, SNES, GB, GBA, Genesis, etc.)
- Redump: Disc-based games (PSX, PS2, PS3, GameCube, etc.)

Platform detection via regex on directory paths (e.g., "Sony - PlayStation 2" → PS2)
