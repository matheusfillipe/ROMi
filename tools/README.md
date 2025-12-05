# ROMi Database Indexer

Python script for generating ROM databases from Myrient ROM archives.

## ⚠️ Important Notes

- **Testing tool only** - For developers and advanced users
- **Not endorsed by Myrient** - This tool scrapes publicly available listings but is not officially supported
- **No guarantees** - Downloads may be slow, fail, or become unavailable at any time
- **Use responsibly** - Respect Myrient's bandwidth and terms of service

## Installation

```bash
# Install dependencies
pip install -r requirements.txt

# Or use the included virtual environment
.venv/bin/python myrient_indexer.py
```

## Basic Usage

```bash
# Fetch all platforms
python myrient_indexer.py

# Fetch specific platform(s)
python myrient_indexer.py --platform PSX
python myrient_indexer.py --platform NES,SNES,GB,GBC,GBA

# Limit entries per platform (for testing)
python myrient_indexer.py --platform PS2 --per-platform 10
```

## Deduplication

Deduplication reduces database size by removing regional duplicates. **Users should evaluate and classify games themselves** - the tool uses region priority as a heuristic only.

```bash
# Enable deduplication with default priority (World > USA > EUR > JPN > ASA)
python myrient_indexer.py --deduplicate

# Custom region priority
python myrient_indexer.py --deduplicate --region-priority "World,EUR,USA,JPN,ASA,Unknown"

# Exclude demos, prototypes, betas
python myrient_indexer.py --deduplicate --exclude-variants
```

## Filtering (Testing)

```bash
# Filter by game name
python myrient_indexer.py --name-filter "Street Fighter" --per-platform 5

# Test specific game deduplication
python myrient_indexer.py --platform PS3 --name-filter "Grand Theft Auto V" --deduplicate
```

## Output Options

```bash
# Custom output directory
python myrient_indexer.py --output ./my_databases

# Compact URLs (filename only, requires sources.txt)
python myrient_indexer.py --compact-urls
```

## Production Workflow

The GitHub Actions workflow generates databases weekly:

```bash
# Equivalent to automated workflow
python myrient_indexer.py \
  --deduplicate \
  --exclude-variants \
  --compact-urls \
  --region-priority "World,USA,EUR,JPN,ASA,Unknown" \
  --output ../release_databases
```

## Supported Platforms

- **PlayStation**: PSX, PS2, PS3
- **Nintendo**: NES, SNES, GB, GBC, GBA
- **Sega**: Genesis, SMS

## Using Generated Databases

### Option 1: Offline Package (Local TSV Files)

1. Copy generated `romi_*.tsv` files to PS3: `/dev_hdd0/game/ROMI00001/USRDIR/`
2. Copy `sources.txt` to same directory
3. ROMi loads databases from local files

### Option 2: Online Database (Remote TSV)

The automated GitHub Actions workflow publishes databases to GitHub Pages:

```
https://<username>.github.io/<repo-name>/romi_db.tsv
```

Configure ROMi to load from this URL for automatic updates without rebuilding the package.

See `.github/workflows/update-databases.yml` for the full automation pipeline.

## Output Files

- `romi_<platform>.tsv` - Platform-specific ROM databases
- `sources.txt` - URL configuration for offline mode
- `romi_db.tsv` - Combined database (workflow only)

## Database Format

TSV (Tab-Separated Values):
```
Platform    Region    Name    URL    Size
PSX         USA       Final Fantasy VII    <url>    <bytes>
```

## Troubleshooting

**ModuleNotFoundError**: Install dependencies with `pip install -r requirements.txt`

**Connection timeout**: Myrient servers may be slow or rate-limiting requests

**Empty results**: Check platform name spelling, verify Myrient URLs are accessible

**Wrong regions selected**: Adjust `--region-priority` or disable `--deduplicate`

## Disclaimer

This tool accesses publicly available ROM listings from myrient.erista.me. It is:
- **Not affiliated with or endorsed by Myrient**
- **Provided as-is with no warranties**
- **Subject to breakage if Myrient changes their site structure**
- **Your responsibility to use ethically and legally**

Respect Myrient's bandwidth and terms of service. Do not abuse their infrastructure.
