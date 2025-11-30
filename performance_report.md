# ROMi Indexer Performance Report

## Executive Summary

Full indexing of all 10 supported platforms would take **3-4 hours** and generate an estimated **25-30 million entries** due to extensive subdirectory structures containing regional variants.

## Subdirectory Discovery

Testing revealed that Myrient's structure has **extensive subdirectories** with regional variants:

| Platform | Main Directory Files | With Subdirectories | Subdirectory Depth |
|----------|---------------------|---------------------|-------------------|
| PS2      | 11,704              | ~7,000,000 (est.)   | ~600+ (est.)      |
| PSX      | 10,885              | ~5,500,000 (est.)   | ~500+ (est.)      |
| NES      | 4,434               | 2,650,000+          | 600+ levels       |
| PS3      | 4,440               | ~2,440,000 (est.)   | ~550+ (est.)      |
| SNES     | 4,086               | ~2,400,000 (est.)   | ~500+ (est.)      |
| GBA      | 3,477               | ~2,000,000 (est.)   | ~450+ (est.)      |
| Genesis  | 2,786               | ~1,600,000 (est.)   | ~400+ (est.)      |
| GB       | 1,958               | 670,000+            | 340+ levels       |
| GBC      | 1,958               | ~670,000 (est.)     | ~340+ (est.)      |
| SMS      | 699                 | ~350,000 (est.)     | ~300+ (est.)      |

## Processing Performance

**Crawling Speed:**
- Average: 0.6-0.9 seconds per batch (directory with ~2,000-4,000 files)
- Processing rate: ~2,000-3,000 files/second
- Network throughput: Stable with concurrent requests (MAX_CONCURRENT=10)

**Test Results:**
- NES: Reached 2.65 million entries in ~10 minutes (600+ batches)
- GB: Reached 670K entries in ~4 minutes (340+ batches)
- SMS: Processing ongoing

## Full Catalog Estimation

| Platform | Main Dir Files | Est. with Subdirs | Est. Time |
|----------|---------------|-------------------|-----------|
| PS2      | 11,704        | ~7,000,000        | 40-50 min |
| PSX      | 10,885        | ~5,500,000        | 30-45 min |
| NES      | 4,434         | ~2,650,000        | 15-20 min |
| PS3      | 4,440         | ~2,440,000        | 15-20 min |
| SNES     | 4,086         | ~2,400,000        | 15-20 min |
| GBA      | 3,477         | ~2,000,000        | 10-15 min |
| Genesis  | 2,786         | ~1,600,000        | 8-12 min  |
| GB       | 1,958         | ~670,000          | 4-6 min   |
| GBC      | 1,958         | ~670,000          | 4-6 min   |
| SMS      | 699           | ~350,000          | 2-4 min   |
| **TOTAL** | **46,427**   | **~25,280,000**   | **3-4 hours** |

## Recommendations

### For Practical Use

Use the `--per-platform` limit to generate balanced test databases:

```bash
# Small test set (20 per platform in ~10 seconds)
tools/.venv/bin/python tools/myrient_indexer.py \
  --platform PS2,PS3,PSX,NES,SNES,GBA,Genesis,GB,GBC,SMS \
  --per-platform 20

# Medium set (100 per platform in ~1 minute)
tools/.venv/bin/python tools/myrient_indexer.py \
  --platform PS2,PS3,PSX,NES,SNES,GBA,Genesis,GB,GBC,SMS \
  --per-platform 100

# Large set (1000 per platform in ~10 minutes)
tools/.venv/bin/python tools/myrient_indexer.py \
  --platform PS2,PS3,PSX,NES,SNES,GBA,Genesis,GB,GBC,SMS \
  --per-platform 1000
```

### For Full Indexing

Only recommended if you need the complete catalog with all regional variants:

```bash
# WARNING: 3-4 hour operation, generates ~25M entries, ~3GB+ database
tools/.venv/bin/python tools/myrient_indexer.py \
  --platform PS2,PS3,PSX,NES,SNES,GBA,Genesis,GB,GBC,SMS \
  --limit 999999999
```

## Key Insights

1. **Regional Variants Dominate**: Main directories contain only ~0.2% of total files when including all subdirectories
2. **Depth is Massive**: Some platforms have 600+ subdirectory levels with regional/language variants
3. **Practical Recommendation**: The main directories already contain the most common ROMs; subdirectories are mostly duplicates with different regions/languages
4. **Performance is Stable**: The indexer maintains consistent ~0.6-0.9s per directory throughout operation
5. **Database Size**: Full catalog would generate ~3-4GB TSV file

## Performance Optimizations Applied

1. **Direct Platform Paths**: Skip breadth-first search, go directly to known platform directories
2. **Concurrent Requests**: Process up to 10 directories in parallel (MAX_CONCURRENT=10)
3. **Proper HTTP Headers**: User-Agent and Referer to avoid Myrient throttling
4. **Batch Processing**: Queue-based approach processes directories efficiently
5. **Per-Platform Limits**: Early exit when limit reached to avoid unnecessary crawling

## Date

Generated: 2025-11-30
