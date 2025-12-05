# ROMi PS3

![Build](https://github.com/matheusfillipe/ROMi/workflows/Build/badge.svg)
![License](https://img.shields.io/github/license/matheusfillipe/ROMi)

ROM and ISO downloader for PlayStation 3 - A PKGi adaptation for retro gaming.

## Features

- Multi-platform ROM downloads (PSX, PS2, PS3, NES, SNES, GB, GBC, GBA, Genesis, SMS)
- Integrated with Myrient ROM archive (No-Intro, Redump)
- Multiple storage device support (HDD, USB 0-7, NTFS 0-7)
- RetroArch ROM organization by platform
- Background music with 9 language translations
- Download queue with resume support and progress tracking
- Search and filter by platform, region, name
- Remote database updates via config URL

## Installation

1. Download the latest `src.pkg` from [Releases](https://github.com/matheusfillipe/ROMi/releases/latest)
2. Install via Package Manager on your jailbroken PS3
3. Launch ROMi from the XMB

## Important Notes

### RetroArch Limitations (PS3HEN)
RetroArch cores on PS3HEN only support ROMs on the **internal HDD** (`/dev_hdd0/`).
External USB/NTFS drives do NOT work for RetroArch emulation - this is a PS3HEN RetroArch limitation, not a ROMi limitation.

**For RetroArch**: Download all ROMs to internal HDD only.

### Other Platforms
PS3 ISOs, PS2 ISOs, and PSX ISOs work from any device (HDD, USB, NTFS).

## Usage

| Button | Action |
|--------|--------|
| **X** | Download selected ROM |
| **Triangle** | Open menu (sort, filter, storage) |
| **Square** | View ROM details |
| **Circle** | Cancel/back |
| **Start** | Refresh database |

## Configuration

Edit `config.txt` in `/dev_hdd0/game/ROMI00001/USRDIR/` to customize settings:

```ini
url https://example.com/romi_db.tsv    # Remote database URL
sort name                              # Sort field: name, region, platform, size
order asc                              # Sort order: asc, desc
filter USA,EUR,JPN,World               # Active region filters
platform PSX                           # Selected platform
storage_device /dev_usb000/            # Storage device path
no_music 1                             # Music disabled (0=on, 1=off)
```

## ROM Storage Organization

ROMs are automatically organized by platform:

```
{storage_device}/
├── PS3ISO/          # PlayStation 3 games
├── PS2ISO/          # PlayStation 2 games
├── PSXISO/          # PlayStation 1 games
└── ROMS/
    ├── NES/         # Nintendo Entertainment System
    ├── SNES/        # Super Nintendo
    ├── GB/          # Game Boy
    ├── GBC/         # Game Boy Color
    ├── GBA/         # Game Boy Advance
    ├── Genesis/     # Sega Genesis / Mega Drive
    └── SMS/         # Sega Master System
```

## Database Generation

ROMi uses TSV databases generated from Myrient ROM archives. See [tools/README.md](tools/README.md) for the myrient indexer documentation and database generation instructions.

## Building

### Prerequisites

- Docker (for PS3 cross-compilation environment)

### Build Commands

```bash
# Build release package
make docker-build

# Build with debug logging
make docker-build-debug

# Clean build artifacts
make docker-clean

# Deploy to RPCS3 emulator
make rpcs3-deploy

# Deploy to PS3 via FTP
make ps3-deploy PS3_IP=192.168.1.100
```

### Development Cycle

1. Make changes to source code
2. Test locally: `make rpcs3-deploy` (opens in RPCS3)
3. Build release: `make docker-build`
4. Test on PS3: `make ps3-deploy PS3_IP=<your_ps3_ip>`

## Credits

- Fork of [PKGi PS3](https://github.com/bucanero/pkgi-ps3) by bucanero
- ROM databases: [Myrient](https://myrient.erista.me/) (No-Intro, Redump)
- PS3 toolchain: PSL1GHT, ps3dev
- Libraries: libcurl, mini18n, ya2d, mbedTLS

## License

See [LICENSE](LICENSE) for details.
