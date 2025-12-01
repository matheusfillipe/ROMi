#!/usr/bin/env python3
"""
Myrient Indexer for ROMi
Fetches ROM listings from myrient.erista.me and generates TSV database files.

Usage:
    python3 myrient_indexer.py                                    # All platforms
    python3 myrient_indexer.py --platform NES                     # Only NES
    python3 myrient_indexer.py --platform NES,SNES                # Multiple platforms
    python3 myrient_indexer.py --platform PSX,PS2 --per-platform 10  # 10 per platform
"""

import argparse
import asyncio
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional
from urllib.parse import unquote, urljoin

import aiohttp
from bs4 import BeautifulSoup

OUTPUT_DIR = Path(__file__).parent / "databases"

PLATFORM_URLS = {
    "PSX": "https://myrient.erista.me/files/Redump/Sony%20-%20PlayStation/",
    "PS2": "https://myrient.erista.me/files/Redump/Sony%20-%20PlayStation%202/",
    "PS3": "https://myrient.erista.me/files/Redump/Sony%20-%20PlayStation%203/",
    "NES": "https://myrient.erista.me/files/No-Intro/Nintendo%20-%20Nintendo%20Entertainment%20System%20%28Headered%29/",
    "SNES": "https://myrient.erista.me/files/No-Intro/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/",
    "GB": "https://myrient.erista.me/files/No-Intro/Nintendo%20-%20Game%20Boy/",
    "GBC": "https://myrient.erista.me/files/No-Intro/Nintendo%20-%20Game%20Boy%20Color/",
    "GBA": "https://myrient.erista.me/files/No-Intro/Nintendo%20-%20Game%20Boy%20Advance/",
    "GENESIS": "https://myrient.erista.me/files/No-Intro/Sega%20-%20Mega%20Drive%20-%20Genesis/",
    "SMS": "https://myrient.erista.me/files/No-Intro/Sega%20-%20Master%20System%20-%20Mark%20III/",
}

PLATFORM_FILENAME_MAP = {
    "GENESIS": "Genesis",
}

REGION_PATTERNS = {
    "USA": re.compile(r"[\(\[]USA[\)\]]|[\(\[]US[\)\]]|[\(\[]U[\)\]]", re.IGNORECASE),
    "EUR": re.compile(r"[\(\[]Europe[\)\]]|[\(\[]EUR[\)\]]|[\(\[]E[\)\]]", re.IGNORECASE),
    "JPN": re.compile(r"[\(\[]Japan[\)\]]|[\(\[]JPN[\)\]]|[\(\[]J[\)\]]", re.IGNORECASE),
    "World": re.compile(r"[\(\[]World[\)\]]", re.IGNORECASE),
    "ASA": re.compile(r"[\(\[]Asia[\)\]]", re.IGNORECASE),
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
    for region, pattern in REGION_PATTERNS.items():
        if pattern.search(filename):
            return region
    return "Unknown"


def parse_size(size_str: str) -> int:
    size_str = size_str.strip().upper()
    if size_str == "-" or not size_str:
        return 0

    multipliers = {
        "B": 1, "K": 1024, "KB": 1024, "KIB": 1024,
        "M": 1024**2, "MB": 1024**2, "MIB": 1024**2,
        "G": 1024**3, "GB": 1024**3, "GIB": 1024**3,
    }

    match = re.match(r"([\d.]+)\s*([A-Z]*)", size_str)
    if not match:
        return 0

    value = float(match.group(1))
    unit = match.group(2) or "B"
    return int(value * multipliers.get(unit, 1))


def clean_name(filename: str) -> str:
    name = unquote(filename)
    name = re.sub(r"\.zip$|\.7z$|\.rar$", "", name, flags=re.IGNORECASE)
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
    if match:
        return int(match.group(1))
    return None


def get_revision(name: str) -> Optional[str]:
    match = REVISION_PATTERN.search(name)
    if match:
        return match.group(1).upper()
    return None


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


async def fetch_page(session: aiohttp.ClientSession, url: str) -> Optional[str]:
    for attempt in range(3):
        try:
            start = time.time()
            async with session.get(url, timeout=aiohttp.ClientTimeout(total=60)) as resp:
                if resp.status == 200:
                    html = await resp.text()
                    elapsed = time.time() - start
                    print(f"  Fetched in {elapsed:.2f}s", file=sys.stderr)
                    return html
                print(f"  HTTP {resp.status}", file=sys.stderr)
        except asyncio.TimeoutError:
            print(f"  Timeout (attempt {attempt + 1})", file=sys.stderr)
        except aiohttp.ClientError as e:
            print(f"  Error: {e}", file=sys.stderr)
        await asyncio.sleep(1 * (attempt + 1))
    return None


def parse_platform_listing(html: str, platform: str, base_url: str, limit: int = 0) -> list[RomEntry]:
    soup = BeautifulSoup(html, "lxml")
    entries = []

    table = soup.select_one("#list tbody")
    if not table:
        return entries

    for row in table.select("tr"):
        cells = row.select("td")
        if len(cells) < 3:
            continue

        link = cells[0].select_one("a")
        if not link:
            continue

        href = link.get("href", "")
        name = link.get_text(strip=True)
        size_text = cells[1].get_text(strip=True)

        if href.startswith("..") or href.endswith("/") or not href:
            continue

        if not name.lower().endswith(".zip"):
            continue

        full_url = urljoin(base_url, href)
        region = detect_region(name)
        clean = clean_name(name)
        size = parse_size(size_text)

        entries.append(RomEntry(
            platform=platform,
            region=region,
            name=clean,
            url=full_url,
            size=size,
        ))

        if limit > 0 and len(entries) >= limit:
            break

    return entries


async def fetch_platform(
    session: aiohttp.ClientSession,
    semaphore: asyncio.Semaphore,
    platform: str,
    url: str,
    limit: int = 0,
) -> list[RomEntry]:
    print(f"[{platform}] Fetching {url}", file=sys.stderr)

    async with semaphore:
        html = await fetch_page(session, url)

    if not html:
        print(f"[{platform}] Failed to fetch", file=sys.stderr)
        return []

    entries = parse_platform_listing(html, platform, url, limit)
    print(f"[{platform}] Found {len(entries)} entries", file=sys.stderr)
    return entries


async def fetch_all_platforms(
    platforms_to_fetch: dict[str, str],
    limit_per_platform: int = 0,
) -> list[RomEntry]:
    headers = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
    }
    connector = aiohttp.TCPConnector(limit=10)
    semaphore = asyncio.Semaphore(10)

    async with aiohttp.ClientSession(connector=connector, headers=headers) as session:
        tasks = [
            fetch_platform(session, semaphore, platform, url, limit_per_platform)
            for platform, url in platforms_to_fetch.items()
        ]
        results = await asyncio.gather(*tasks)

    return [entry for entries in results for entry in entries]


def extract_filename_from_url(url: str) -> str:
    return url.split("/")[-1]


def write_sources_file(output_dir: Path, platforms_used: set[str]) -> None:
    # Write sources.txt to tools/ directory (not databases/) for Makefile compatibility
    sources_file = output_dir.parent / "sources.txt"

    with open(sources_file, "w", encoding="utf-8") as f:
        f.write("# ROMi ROM Sources Configuration\n")
        f.write("# Format: PLATFORM  BASE_URL\n")
        f.write("# The BASE_URL is prepended to filenames in the database\n")
        f.write("# Lines starting with # are comments\n")
        f.write("#\n")
        f.write("# To use a different ROM source, change the URLs below.\n")
        f.write("# URLs must end with / and filenames will be appended directly.\n")
        f.write("\n")

        for platform in sorted(PLATFORM_URLS.keys()):
            if platform in platforms_used:
                url = PLATFORM_URLS[platform]
                f.write(f"{platform:<7} {url}\n")

    print(f"Wrote sources configuration to {sources_file}")


def write_databases(entries: list[RomEntry], output_dir: Path, compact_urls: bool = False) -> None:
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
                url_field = extract_filename_from_url(entry.url) if compact_urls else entry.url
                platform_name = PLATFORM_FILENAME_MAP.get(entry.platform, entry.platform)
                line = f"{platform_name}\t{entry.region}\t{entry.name}\t{url_field}\t{entry.size}\n"
                f.write(line)

        file_size = output_file.stat().st_size
        total_bytes += file_size
        print(f"Wrote {len(platform_entries)} entries to {output_file} ({file_size / 1024:.1f} KB)")

    write_sources_file(output_dir, set(by_platform.keys()))

    print(f"\nTotal database size: {total_bytes / 1024:.1f} KB ({total_bytes / 1024 / 1024:.2f} MB)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch ROM listings from myrient.erista.me and generate TSV databases",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
Supported platforms:
  {', '.join(sorted(PLATFORM_URLS.keys()))}

Examples:
  %(prog)s --platform NES --per-platform 10      # Test with 10 NES ROMs
  %(prog)s --platform GB,GBC,GBA --per-platform 10  # 10 ROMs per Game Boy platform
  %(prog)s --deduplicate --exclude-variants      # Production: dedupe + exclude demos
  %(prog)s --compact-urls                        # Store filename only
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
        help="Store only filename in URL field",
    )
    return parser.parse_args()


async def main() -> None:
    args = parse_args()

    if args.platform:
        requested = {p.strip().upper() for p in args.platform.split(",")}
        unknown = requested - set(PLATFORM_URLS.keys())
        if unknown:
            print(f"Error: Unknown platforms: {', '.join(unknown)}", file=sys.stderr)
            print(f"Available: {', '.join(sorted(PLATFORM_URLS.keys()))}", file=sys.stderr)
            sys.exit(1)
        platforms_to_fetch = {p: PLATFORM_URLS[p] for p in requested}
    else:
        platforms_to_fetch = PLATFORM_URLS.copy()

    region_priority = [r.strip() for r in args.region_priority.split(",")]
    output_dir = Path(args.output) if args.output else OUTPUT_DIR

    print(f"Fetching {len(platforms_to_fetch)} platforms: {', '.join(sorted(platforms_to_fetch.keys()))}")
    if args.per_platform > 0:
        print(f"  Limit: {args.per_platform} per platform")
    if args.deduplicate:
        print(f"  Deduplication: ON (priority: {' > '.join(region_priority)})")
    if args.exclude_variants:
        print(f"  Variant exclusion: ON")
    if args.compact_urls:
        print(f"  Compact URLs: ON")
    print()

    total_start = time.time()
    entries = await fetch_all_platforms(platforms_to_fetch, args.per_platform)
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
