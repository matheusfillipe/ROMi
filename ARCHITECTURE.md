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

## Data Source

Primary: https://myrient.erista.me/files/
- No-Intro: Cartridge-based ROMs (NES, SNES, GB, GBA, Genesis, etc.)
- Redump: Disc-based games (PSX, PS2, PS3, GameCube, etc.)

Platform detection via regex on directory paths (e.g., "Sony - PlayStation 2" → PS2)
