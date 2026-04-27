#!/usr/bin/env python3
"""
Archive.org Indexer for ROMi
Fetches ROM listings from archive.org No-Intro collections and generates
TSV database files compatible with ROMi's database format.

This replaces the Myrient-based myrient_indexer.py since Myrient shut down
on March 31, 2026.

Usage:
    python3 archive_org_indexer.py                                    # All platforms
    python3 archive_org_indexer.py --platform NES                     # Only NES
    python3 archive_org_indexer.py --platform NES,SNES                # Multiple platforms
    python3 archive_org_indexer.py --platform PSX --per-platform 10   # 10 per platform
    python3 archive_org_indexer.py --compact-urls                    # Filename-only URLs
"""

import argparse
import asyncio
import json
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional
from urllib.parse import quote, unquote

import aiohttp

OUTPUT_DIR = Path(__file__).parent / "databases"

# Archive.org item IDs mapped to ROMi platform names.
# Each entry: (archive_item_id, subdirectory_filter or None, file_extension_filter)
#
# subdirectory_filter: if set, only include files from this subdirectory.
#   This is needed when archive.org items contain multiple platforms (e.g. Atari all-in-one).
#
# file_extension: primary extension to match (used for dedup when multiple exts exist)
#
# private_ok: if True, include files even if marked private (they won't be downloadable
#   but at least they appear in the database for reference)

PLATFORM_SOURCES = {
    # --- Cartridge-based (No-Intro) ---
    "NES": {
        "item": "No-Intro_NES",
        "ext": ".zip",
        "subdir": None,
        "skip_prefixes": ["Aftermarket/", "Hacks/", "[T-", "Source Code/"],
    },
    "SNES": {
        "item": "ef_nintendo_snes_no-intro_2024-04-20",
        "ext": ".zip",
        "subdir": None,
        "skip_prefixes": [],
    },
    "GB": {
        "item": "No-Intro_GB",
        "ext": ".7z",
        "subdir": None,
        "skip_prefixes": [],
    },
    "GBC": {
        "item": "No-Intro_GBC",
        "ext": ".7z",
        "subdir": None,
        "skip_prefixes": [],
    },
    "GBA": {
        "item": "No-Intro_GBA",
        "ext": ".7z",
        "subdir": None,
        "skip_prefixes": [],
    },
    "Genesis": {
        "item": "nointro.md",
        "ext": ".7z",
        "subdir": None,
        "skip_prefixes": [],
    },
    "SMS": {
        "item": "nointro.ms-mkiii",
        "ext": ".7z",
        "subdir": None,
        "skip_prefixes": [],
    },

    "Atari2600": {
        "item": "nointro.atari-2600",
        "ext": ".7z",
        "subdir": None,
        "skip_prefixes": [],
    },
    "Atari5200": {
        "item": "nointro.atari-5200",
        "ext": ".7z",
        "subdir": None,
        "skip_prefixes": [],
    },
    "Atari7800": {
        "item": "nointro.atari-7800",
        "ext": ".7z",
        "subdir": None,
        "skip_prefixes": [],
    },
    "AtariLynx": {
        "item": "NoIntro-Atari",
        "ext": ".zip",
        "subdir": "Atari - Lynx",
        "skip_prefixes": [],
    },

    # --- Disc-based (Redump) ---
    # NOTE: Most PS1/PS2/PS3/PSP files are private on archive.org.
    # PS1 has two public subcollections: NTSC-U-M and NTSC-U-S.
    # Downloads are larger (hundreds of MB per game) and use .zip format.
    "PSX": {
        "item": "Redump.orgSonyPlayStation-NTSC-U-M",
        "ext": ".zip",
        "subdir": None,
        "skip_prefixes": [],
        "extra_items": ["Redump.orgSonyPlayStation-NTSC-U-S"],
    },

}

# ROMi platform name mapping (ROMi internal name -> TSV platform name)
PLATFORM_FILENAME_MAP = {
    "Genesis": "Genesis",
    "Atari2600": "Atari2600",
    "Atari5200": "Atari5200",
    "Atari7800": "Atari7800",
    "AtariLynx": "AtariLynx",
}

REGION_PATTERNS = {
    "USA": re.compile(r"\b(?:USA|US)\b", re.IGNORECASE),
    "EUR": re.compile(r"\b(?:Europe|EUR|Australia)\b", re.IGNORECASE),
    "JPN": re.compile(r"\b(?:Japan|JPN)\b", re.IGNORECASE),
    "World": re.compile(r"\bWorld\b", re.IGNORECASE),
    "ASA": re.compile(r"\b(?:Asia|ASA|Korea|Hong Kong|Taiwan)\b", re.IGNORECASE),
}

DEFAULT_REGION_PRIORITY = ["World", "USA", "EUR", "JPN", "ASA", "Unknown"]

VARIANT_PATTERNS = [
    re.compile(r"[\(\[]Demo[\)\]]", re.IGNORECASE),
    re.compile(r"[\(\[]Sample[\)\]]", re.IGNORECASE),
    re.compile(r"[\(\[]Taikenban[\)\]]", re.IGNORECASE),
    re.compile(r"[\(\[]Proto[\)\]]", re.IGNORECASE),
    re.compile(r"[\(\[]Prototype[\)\]]", re.IGNORECASE),
    re.compile(r"[\(\[]Beta[\)\]]", re.IGNORECASE),
    re.compile(r"[\(\[]Pirate[\)\]]", re.IGNORECASE),
    re.compile(r"[\(\[]Unl[\)\]]", re.IGNORECASE),
    re.compile(r"\[BIOS\]", re.IGNORECASE),
    re.compile(r"\[b\]", re.IGNORECASE),
]

DISC_PATTERN = re.compile(r"[\(\[]Disc\s*(\d+)[\)\]]", re.IGNORECASE)
REVISION_PATTERN = re.compile(r"[\(\[]Rev\s*([A-Z0-9]+)[\)\]]", re.IGNORECASE)


@dataclass
class RomEntry:
    platform: str
    region: str
    name: str
    url: str
    size: int


def detect_region(filename: str) -> str:
    """Detect region from filename, returning highest priority region if multiple found."""
    found_regions = []
    for region, pattern in REGION_PATTERNS.items():
        if pattern.search(filename):
            found_regions.append(region)

    if not found_regions:
        return "Unknown"

    for priority_region in DEFAULT_REGION_PRIORITY:
        if priority_region in found_regions:
            return priority_region

    return found_regions[0]


def clean_name(filename: str) -> str:
    name = unquote(filename)
    name = re.sub(r"\.(zip|7z|rar)$", "", name, flags=re.IGNORECASE)
    return name


def is_variant(name: str) -> bool:
    for pattern in VARIANT_PATTERNS:
        if pattern.search(name):
            return True
    return False


def extract_base_name(name: str) -> str:
    base = name
    base = re.sub(r"[\(\[][^\)\]]*[\)\]]", "", base)
    base = re.sub(r"\s+", " ", base).strip().lower()
    return base


def get_disc_number(name: str) -> Optional[int]:
    match = DISC_PATTERN.search(name)
    return int(match.group(1)) if match else None


def get_revision(name: str) -> Optional[str]:
    match = REVISION_PATTERN.search(name)
    return match.group(1).upper() if match else None


def deduplicate_entries(entries: list[RomEntry], region_priority: list[str]) -> list[RomEntry]:
    grouped: dict[tuple[str, str, Optional[int]], list[RomEntry]] = {}

    for entry in entries:
        base = extract_base_name(entry.name)
        disc = get_disc_number(entry.name)
        key = (entry.platform, base, disc)
        grouped.setdefault(key, []).append(entry)

    result = []
    for group in grouped.values():
        if len(group) == 1:
            result.append(group[0])
            continue

        def sort_key(e: RomEntry) -> tuple[int, str]:
            region_idx = region_priority.index(e.region) if e.region in region_priority else len(region_priority)
            rev = get_revision(e.name) or ""
            return (region_idx, rev)

        group.sort(key=sort_key)
        best = group[0]

        if len(group) > 1:
            rev_best = get_revision(best.name)
            for candidate in group[1:]:
                if candidate.region != best.region:
                    break
                rev_candidate = get_revision(candidate.name)
                if rev_candidate and rev_best and rev_candidate > rev_best:
                    best = candidate
                    rev_best = rev_candidate

        result.append(best)

    return result


def filter_variants(entries: list[RomEntry]) -> list[RomEntry]:
    return [e for e in entries if not is_variant(e.name)]


async def fetch_metadata(
    session: aiohttp.ClientSession,
    item_id: str,
) -> Optional[list[dict]]:
    """Fetch archive.org metadata JSON for an item."""
    url = f"https://archive.org/metadata/{item_id}"
    for attempt in range(3):
        try:
            async with session.get(url, timeout=aiohttp.ClientTimeout(total=30)) as resp:
                if resp.status == 200:
                    data = await resp.json(content_type=None)
                    return data.get("files", [])
                print(f"  Metadata HTTP {resp.status} for {item_id}", file=sys.stderr)
        except asyncio.TimeoutError:
            print(f"  Timeout (attempt {attempt + 1}) for {item_id}", file=sys.stderr)
        except Exception as e:
            print(f"  Error fetching {item_id}: {e}", file=sys.stderr)
        await asyncio.sleep(1 * (attempt + 1))
    return None


def parse_archive_files(
    files: list[dict],
    platform: str,
    item_id: str,
    subdir: Optional[str],
    skip_prefixes: list[str],
    target_ext: str,
) -> list[RomEntry]:
    """Parse archive.org file metadata into RomEntry list."""
    entries = []
    seen_names = set()

    for f in files:
        name = f.get("name", "")
        fmt = f.get("format", "").upper()
        size = int(f.get("size", 0))
        private = f.get("private") == "true"

        # Skip directories (no format or name ends with /)
        if not fmt and name.endswith("/"):
            continue

        # Apply subdirectory filter
        if subdir:
            if not name.startswith(subdir + "/"):
                continue
            # Strip subdirectory prefix for the filename
            name = name[len(subdir) + 1:]

        # Skip empty names or directories
        if not name or "/" in name:
            continue

        # Apply skip prefixes (on full path before subdir stripping)
        full_path = f.get("name", "")
        if any(full_path.startswith(prefix) for prefix in skip_prefixes):
            continue

        # Match by extension
        if not name.lower().endswith(target_ext):
            # Also check by format
            if fmt not in ("ZIP", "7Z", "RAR"):
                continue

        # Skip very small files (BIOS, patches, etc.) - less than 1KB
        if size > 0 and size < 1024:
            continue

        # Skip BIOS files
        if "[BIOS]" in name.upper():
            continue

        # Build the download URL
        encoded_name = quote(name, safe="")
        download_url = f"https://archive.org/download/{item_id}/{encoded_name}"

        # Clean the display name
        clean = clean_name(name)

        # Detect region
        region = detect_region(name)

        # Deduplicate by name (same game from different items)
        if clean.lower() in seen_names:
            continue
        seen_names.add(clean.lower())

        entries.append(RomEntry(
            platform=platform,
            region=region,
            name=clean,
            url=download_url,
            size=size,
        ))

    return entries


async def fetch_platform(
    session: aiohttp.ClientSession,
    semaphore: asyncio.Semaphore,
    platform: str,
    source: dict,
    limit: int = 0,
    name_filter: str = "",
) -> list[RomEntry]:
    """Fetch all items for a platform (primary + extra items)."""
    all_entries = []
    seen_names = set()

    items_to_fetch = [source["item"]] + source.get("extra_items", [])
    subdir = source.get("subdir")
    skip_prefixes = source.get("skip_prefixes", [])
    target_ext = source.get("ext", ".zip")

    for item_id in items_to_fetch:
        print(f"  Fetching metadata for {item_id}...", file=sys.stderr)

        async with semaphore:
            files = await fetch_metadata(session, item_id)

        if not files:
            print(f"  No files found for {item_id}", file=sys.stderr)
            continue

        entries = parse_archive_files(
            files, platform, item_id, subdir, skip_prefixes, target_ext
        )

        # Deduplicate across items
        new_entries = []
        for e in entries:
            if e.name.lower() not in seen_names:
                seen_names.add(e.name.lower())
                new_entries.append(e)

        all_entries.extend(new_entries)
        print(f"  {item_id}: {len(entries)} files, {len(new_entries)} new", file=sys.stderr)

        if limit > 0 and len(all_entries) >= limit:
            all_entries = all_entries[:limit]
            break

    # Apply name filter
    if name_filter:
        all_entries = [e for e in all_entries if name_filter.lower() in e.name.lower()]

    return all_entries


async def fetch_all_platforms(
    platforms_to_fetch: dict[str, dict],
    limit_per_platform: int = 0,
    name_filter: str = "",
) -> list[RomEntry]:
    headers = {
        "User-Agent": "ROMi/1.0 (Archive.org Indexer)",
    }
    connector = aiohttp.TCPConnector(limit=5)
    semaphore = asyncio.Semaphore(3)

    async with aiohttp.ClientSession(connector=connector, headers=headers) as session:
        tasks = [
            fetch_platform(session, semaphore, platform, source, limit_per_platform, name_filter)
            for platform, source in platforms_to_fetch.items()
        ]
        results = await asyncio.gather(*tasks)

    return [entry for entries in results for entry in entries]


def write_sources_file(output_dir: Path, platforms_used: set[str]) -> None:
    """Write sources.txt for ROMi to use when constructing download URLs."""
    sources_file = output_dir.parent / "sources.txt"

    with open(sources_file, "w", encoding="utf-8") as f:
        f.write("# ROMi ROM Sources Configuration\n")
        f.write("# Format: PLATFORM  BASE_URL\n")
        f.write("# The BASE_URL is prepended to filenames in the database.\n")
        f.write("# Lines starting with # are comments.\n")
        f.write("#\n")
        f.write("# Archive.org direct download URLs.\n")
        f.write("# NOTE: PS1/PS2/PS3/PSP files may be private and return errors.\n")
        f.write("#\n\n")

        for platform in sorted(PLATFORM_SOURCES.keys()):
            if platform in platforms_used:
                source = PLATFORM_SOURCES[platform]
                item = source["item"]
                # For compact mode, use just the item base URL.
                # For full URL mode, ROMi will use the URL as-is from the TSV.
                # We write the base URL so ROMi can construct: base + filename
                base_url = f"https://archive.org/download/{item}/"
                if source.get("subdir"):
                    base_url += quote(source["subdir"] + "/", safe="/")
                f.write(f"{platform:<7} {base_url}\n")

    print(f"Wrote sources configuration to {sources_file}")


def write_databases(
    entries: list[RomEntry],
    output_dir: Path,
    compact_urls: bool = False,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    by_platform: dict[str, list[RomEntry]] = {}
    for entry in entries:
        by_platform.setdefault(entry.platform, []).append(entry)

    total_bytes = 0
    for platform, platform_entries in by_platform.items():
        platform_entries.sort(key=lambda e: e.name.lower())

        filename_platform = PLATFORM_FILENAME_MAP.get(platform, platform)
        output_file = output_dir / f"romi_{filename_platform}.tsv"

        with open(output_file, "w", encoding="utf-8") as f:
            for entry in platform_entries:
                if compact_urls:
                    # Extract just the filename from the URL
                    url_field = entry.url.split("/")[-1]
                    # URL-decode it so it's a clean filename
                    url_field = unquote(url_field)
                else:
                    url_field = entry.url

                platform_name = PLATFORM_FILENAME_MAP.get(entry.platform, entry.platform)
                line = f"{platform_name}\t{entry.region}\t{entry.name}\t{url_field}\t{entry.size}\n"
                f.write(line)

        file_size = output_file.stat().st_size
        total_bytes += file_size
        print(f"Wrote {len(platform_entries):>5} entries to {output_file.name} ({file_size / 1024:.1f} KB)")

    write_sources_file(output_dir, set(by_platform.keys()))

    print(f"\nTotal database size: {total_bytes / 1024:.1f} KB ({total_bytes / 1024 / 1024:.2f} MB)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch ROM listings from archive.org No-Intro/Redump collections and generate TSV databases",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
Supported platforms:
  {', '.join(sorted(PLATFORM_SOURCES.keys()))}

Notes:
  - Cartridge-based platforms (NES, SNES, GB, GBC, GBA, etc.) have working
    direct downloads from archive.org.
  - Disc-based platforms (PS1, PS2, PS3, PSP) files are marked private on
    archive.org and downloads will fail. These are included for database
    completeness only.
  - Use --compact-urls to store only filenames (requires sources.txt on PS3).
  - Use --full-urls to store complete download URLs (works without sources.txt).

Examples:
  %(prog)s --platform NES --per-platform 10           # Test with 10 NES ROMs
  %(prog)s --platform GB,GBC,GBA --per-platform 10    # 10 ROMs per GB platform
  %(prog)s --deduplicate --exclude-variants            # Production: dedupe + exclude demos
  %(prog)s --compact-urls                              # Filename-only mode (with sources.txt)
  %(prog)s --full-urls                                 # Full URL mode (no sources.txt needed)
        """,
    )
    parser.add_argument(
        "-p", "--platform",
        type=str,
        default="",
        help="Comma-separated list of platforms (e.g., NES,SNES,GB). Default: all",
    )
    parser.add_argument(
        "--per-platform",
        type=int,
        default=0,
        help="Maximum entries per platform (0 = unlimited)",
    )
    parser.add_argument(
        "-o", "--output",
        type=str,
        default="",
        help="Output directory for TSV files (default: tools/databases)",
    )
    parser.add_argument(
        "--deduplicate",
        action="store_true",
        help="Remove regional duplicates, keeping preferred region",
    )
    parser.add_argument(
        "--region-priority",
        type=str,
        default=",".join(DEFAULT_REGION_PRIORITY),
        help="Comma-separated region priority for deduplication",
    )
    parser.add_argument(
        "--exclude-variants",
        action="store_true",
        help="Exclude demos, prototypes, betas, and other non-retail variants",
    )
    parser.add_argument(
        "--compact-urls",
        action="store_true",
        help="Store only filename in URL field (requires sources.txt on PS3)",
    )
    parser.add_argument(
        "--full-urls",
        action="store_true",
        help="Store complete archive.org URL in URL field",
    )
    parser.add_argument(
        "--name-filter",
        type=str,
        default="",
        help="Filter ROMs by name (case-insensitive substring match)",
    )
    return parser.parse_args()


async def main() -> None:
    args = parse_args()

    if args.platform:
        # Case-insensitive platform matching
        upper_to_canonical = {k.upper(): k for k in PLATFORM_SOURCES.keys()}
        requested_raw = [p.strip() for p in args.platform.split(",")]
        requested = set()
        invalid = []
        for p in requested_raw:
            key = upper_to_canonical.get(p.upper())
            if key:
                requested.add(key)
            else:
                invalid.append(p)
        if invalid:
            print(f"Error: Unknown platforms: {', '.join(invalid)}", file=sys.stderr)
            print(f"Available: {', '.join(sorted(PLATFORM_SOURCES.keys()))}", file=sys.stderr)
            sys.exit(1)
        platforms_to_fetch = {p: PLATFORM_SOURCES[p] for p in requested}
    else:
        platforms_to_fetch = PLATFORM_SOURCES.copy()

    region_priority = [r.strip() for r in args.region_priority.split(",")]
    output_dir = Path(args.output) if args.output else OUTPUT_DIR

    print(f"Archive.org ROM Indexer for ROMi")
    print(f"================================")
    print(f"Fetching {len(platforms_to_fetch)} platforms: {', '.join(sorted(platforms_to_fetch.keys()))}")
    if args.per_platform > 0:
        print(f"  Limit: {args.per_platform} per platform")
    if args.name_filter:
        print(f"  Name filter: '{args.name_filter}'")
    if args.deduplicate:
        print(f"  Deduplication: ON (priority: {' > '.join(region_priority)})")
    if args.exclude_variants:
        print(f"  Variant exclusion: ON")
    if args.compact_urls:
        print(f"  Compact URLs: ON (filename only, needs sources.txt)")
    elif args.full_urls:
        print(f"  Full URLs: ON (complete archive.org URLs)")
    else:
        print(f"  URLs: full (default)")
    print()

    total_start = time.time()
    entries = await fetch_all_platforms(platforms_to_fetch, args.per_platform, args.name_filter)
    fetch_elapsed = time.time() - total_start

    print(f"\n=== Summary ===")
    print(f"Fetch time: {fetch_elapsed:.2f}s")
    print(f"Raw entries: {len(entries)}")

    if args.exclude_variants:
        before = len(entries)
        entries = filter_variants(entries)
        print(f"After variant exclusion: {len(entries)} (-{before - len(entries)})")

    if args.deduplicate:
        before = len(entries)
        entries = deduplicate_entries(entries, region_priority)
        print(f"After deduplication: {len(entries)} (-{before - len(entries)})")

    print()
    write_databases(entries, output_dir, compact_urls=args.compact_urls)
    print("\nDone!")


if __name__ == "__main__":
    asyncio.run(main())
