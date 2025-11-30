# ROMi Indexer Performance Report

## Executive Summary

Full indexing of 9 supported platforms (excluding PS3) takes **~5 minutes** and generates **~23,000 unique entries** after optimization. With all optimizations enabled (deduplication, variant exclusion, compact URLs), the total database size is **~2.6 MB**.

## Actual Test Results (9 Platforms)

**Test Configuration:**
- Platforms: PSX, PS2, NES, SNES, GB, GBC, GBA, Genesis, SMS
- Optimizations: `--deduplicate --exclude-variants --compact-urls`
- Region Priority: World > USA > EUR > JPN > ASA > Unknown

**Results:**
| Metric | Value |
|--------|-------|
| Total Time | 299.66 seconds (~5 min) |
| Raw Entries | 899,991 |
| After Variant Exclusion | 709,181 (-190,810 variants) |
| After Deduplication | 23,092 unique entries |
| Total Database Size | 2.60 MB |
| Compression Ratio | 97.4% reduction |

## Per-Platform Statistics

| Platform | Raw Entries | Unique After Optimization | Database Size |
|----------|-------------|---------------------------|---------------|
| PSX      | ~100K       | 6,726                     | 770.9 KB      |
| PS2      | ~100K       | 6,333                     | 781.3 KB      |
| NES      | ~100K       | 1,730                     | 170.5 KB      |
| SNES     | ~100K       | 2,204                     | 221.9 KB      |
| GBA      | ~60K        | 2,051                     | 256.6 KB      |
| GB       | ~50K        | 1,323                     | 142.6 KB      |
| GBC      | ~50K        | 1,128                     | 150.9 KB      |
| Genesis  | ~50K        | 1,214                     | 128.9 KB      |
| SMS      | ~20K        | 383                       | 41.6 KB       |

## Optimization Impact

### 1. Variant Exclusion (-21% entries)
Removes demos, prototypes, betas, samples, and pirate copies:
- Before: 899,991 entries
- After: 709,181 entries
- Removed: 190,810 variant entries

### 2. Region Deduplication (-97% entries)
Keeps one copy per game based on region priority:
- Before: 709,181 entries
- After: 23,092 unique entries
- Removed: 686,089 regional duplicates

### 3. Compact URLs (-60% database size)
Stores filename only instead of full URL:
- Full URL: `https://myrient.erista.me/files/Redump/Sony - PlayStation/Game.zip`
- Compact: `Game.zip`
- URL reconstructed at runtime from platform base URL

## Processing Performance

**Crawling Speed:**
- Average: 0.3-0.5 seconds per page
- Processing rate: ~3,000 entries/second
- Network throughput: Stable with concurrent requests (MAX_CONCURRENT=10)

**Per-Platform Timing:**
| Platform | Time    |
|----------|---------|
| PSX      | ~30s    |
| PS2      | ~30s    |
| NES      | ~20s    |
| SNES     | ~20s    |
| GBA      | ~20s    |
| GB       | ~15s    |
| GBC      | ~15s    |
| Genesis  | ~15s    |
| SMS      | ~10s    |

## Recommendations

### For Release Databases (Recommended)

Use all optimizations for smallest, most practical databases:

```bash
python tools/myrient_indexer.py \
  --deduplicate \
  --exclude-variants \
  --compact-urls \
  --region-priority "World,USA,EUR,JPN,ASA,Unknown" \
  --output release_databases
```

### For Development Testing

Use per-platform limits for quick iteration:

```bash
# Small test set (20 per platform)
python tools/myrient_indexer.py --per-platform 20

# Medium set (100 per platform)
python tools/myrient_indexer.py --per-platform 100
```

### For Full Raw Catalog (Not Recommended)

Without optimizations, generates ~900K entries in ~5 minutes:

```bash
# WARNING: Generates ~100MB+ raw database
python tools/myrient_indexer.py --limit 999999999
```

## Key Insights

1. **Regional Variants Dominate**: ~97% of entries are regional duplicates of the same game
2. **Deduplication is Essential**: Without it, databases are 40x larger with no practical benefit
3. **Variant Exclusion Helps**: Demos, protos, betas add ~20% bloat
4. **Compact URLs**: 60% size reduction with no functionality loss
5. **PS3 Size**: PS3 has ~4,440 base entries per page, many more pages than other platforms

## Automated Updates

The GitHub Actions workflow (`update-databases.yml`) runs weekly to:
1. Index all platforms with full optimizations
2. Create timestamped GitHub release with TSV files
3. Include per-platform statistics

## Date

Generated: 2025-11-30
