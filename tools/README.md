# ROMi Database Indexer

Python scripts for generating ROM databases from ROM archives.

## Indexers

### archive_org_indexer.py (Recommended)

Fetches ROM listings from archive.org No-Intro/Redump collections. This replaces the Myrient indexer since Myrient shut down on March 31, 2026.

**Supported platforms** (matching ROMi's built-in platform list):
- **Nintendo**: NES, SNES, GB, GBC, GBA
- **Sega**: Genesis, SMS
- **Atari**: 2600, 5200, 7800, Lynx

### myrient_indexer.py (Legacy)

**No longer functional** — Myrient (myrient.erista.me) shut down on March 31, 2026. Kept for reference.

## Installation

```bash
pip install aiohttp
```

## Basic Usage

```bash
# Fetch all working cartridge platforms
python archive_org_indexer.py --deduplicate --exclude-variants --full-urls

# Fetch specific platform(s)
python archive_org_indexer.py --platform NES,SNES,GB,GBC,GBA

# Limit entries per platform (for testing)
python archive_org_indexer.py --platform NES --per-platform 10
```

## URL Modes

```bash
# Full URLs (recommended) — complete archive.org URLs, no sources.txt needed
python archive_org_indexer.py --full-urls

# Compact URLs — filename only, requires sources.txt on PS3
python archive_org_indexer.py --compact-urls
```

## Deduplication & Filtering

```bash
# Enable deduplication (removes regional duplicates)
python archive_org_indexer.py --deduplicate

# Exclude demos, prototypes, betas, pirates
python archive_org_indexer.py --exclude-variants

# Custom region priority
python archive_org_indexer.py --deduplicate --region-priority "World,USA,EUR,JPN,ASA,Unknown"

# Filter by game name
python archive_org_indexer.py --name-filter "Street Fighter" --per-platform 5
```

## Production Build

```bash
# Generate production databases
python archive_org_indexer.py \
  --deduplicate \
  --exclude-variants \
  --full-urls
```

## Using Generated Databases

### Offline (Local TSV Files)

1. Copy generated `romi_*.tsv` files to PS3: `/dev_hdd0/game/ROMI00001/USRDIR/`
2. ROMi loads per-platform files automatically (e.g., `romi_NES.tsv`, `romi_GBA.tsv`)
3. Alternatively, copy `romi_db.tsv` (combined) — ROMi loads this first if present

### Online (Remote TSV)

Host `romi_db.tsv` on GitHub Pages or any static host, then configure ROMi:
```ini
url https://yourhost.github.io/your-repo/romi_db.tsv
```

## Database Format

TSV (Tab-Separated Values), 5 columns:
```
Platform    Region    Name    URL_or_Filename    Size_in_bytes
NES         USA       Super Mario Bros.    https://archive.org/.../file.zip    262144
```

When using `sources.txt`, column 4 is just a filename and the base URL is prepended at download time.

## Proxy Configuration

Configure in ROMi's `config.txt`:
```ini
proxy_url http://your-proxy:8080
proxy_user username         # optional
proxy_pass password         # optional
```

## Troubleshooting

**Empty results**: Verify archive.org item IDs are accessible (some may be removed)

**Slow downloads**: archive.org rate-limits — use `--full-urls` to avoid double-fetching

**500 errors**: Usually a transient archive.org CDN error — retry the request
