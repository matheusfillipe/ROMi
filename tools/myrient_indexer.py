#!/usr/bin/env python3
"""
Myrient Indexer for ROMi
Crawls myrient.erista.me and generates TSV database files for ROMi PS3 app.

Usage:
    python3 myrient_indexer.py                                    # Full crawl
    python3 myrient_indexer.py --platform NES                     # Only NES
    python3 myrient_indexer.py --platform NES,SNES                # Multiple platforms
    python3 myrient_indexer.py --limit 50                         # Stop after 50 entries
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

BASE_URL = "https://myrient.erista.me/files/"
OUTPUT_DIR = Path(__file__).parent / "databases"
MAX_CONCURRENT = 10

g_limit: int = 0
g_per_platform_limit: int = 0
g_entry_count: int = 0
g_platform_counts: dict[str, int] = {}
g_platforms: set[str] = set()
g_stop: bool = False
g_fetch_count: int = 0
g_fetch_time: float = 0.0
g_parse_time: float = 0.0

PLATFORM_PATTERNS = {
    "PSX": re.compile(r"playstation(?!\s*[2-9]|.*(?:vita|portable|psp))", re.IGNORECASE),
    "PS2": re.compile(r"playstation\s*2", re.IGNORECASE),
    "PS3": re.compile(r"playstation\s*3", re.IGNORECASE),
    "SNES": re.compile(r"super\s*nintendo|(?:^|-)snes(?:$|-)", re.IGNORECASE),
    "NES": re.compile(r"(?<!super\s)nintendo\s*entertainment\s*system|nintendo\s*-\s*nes(?!\s*classic)", re.IGNORECASE),
    "GB": re.compile(r"game\s*boy(?!\s*(advance|color))", re.IGNORECASE),
    "GBC": re.compile(r"game\s*boy\s*color", re.IGNORECASE),
    "GBA": re.compile(r"game\s*boy\s*advance", re.IGNORECASE),
    "GENESIS": re.compile(r"sega\s*genesis|mega\s*drive", re.IGNORECASE),
    "SMS": re.compile(r"master\s*system", re.IGNORECASE),
    "MAME": re.compile(r"(?:^|/)mame(?:$|/)|fbneo|fbalpha", re.IGNORECASE),
}

KNOWN_PLATFORM_PATHS = {
    "PSX": "Redump/Sony - PlayStation/",
    "PS2": "Redump/Sony - PlayStation 2/",
    "PS3": "Redump/Sony - PlayStation 3/",
    "NES": "No-Intro/Nintendo - Nintendo Entertainment System (Headered)/",
    "SNES": "No-Intro/Nintendo - Super Nintendo Entertainment System/",
    "GB": "No-Intro/Nintendo - Game Boy/",
    "GBC": "No-Intro/Nintendo - Game Boy Color/",
    "GBA": "No-Intro/Nintendo - Game Boy Advance/",
    "GENESIS": "No-Intro/Sega - Mega Drive - Genesis/",
    "SMS": "No-Intro/Sega - Master System - Mark III/",
}

REGION_PATTERNS = {
    "USA": re.compile(r"[\(\[]USA[\)\]]|[\(\[]US[\)\]]|[\(\[]U[\)\]]", re.IGNORECASE),
    "EUR": re.compile(r"[\(\[]Europe[\)\]]|[\(\[]EUR[\)\]]|[\(\[]E[\)\]]", re.IGNORECASE),
    "JPN": re.compile(r"[\(\[]Japan[\)\]]|[\(\[]JPN[\)\]]|[\(\[]J[\)\]]", re.IGNORECASE),
    "World": re.compile(r"[\(\[]World[\)\]]", re.IGNORECASE),
    "ASA": re.compile(r"[\(\[]Asia[\)\]]", re.IGNORECASE),
}


@dataclass
class RomEntry:
    platform: str
    region: str
    name: str
    url: str
    size: int


def detect_platform(path: str) -> Optional[str]:
    """Detect platform from directory path."""
    for platform, pattern in PLATFORM_PATTERNS.items():
        if pattern.search(path):
            return platform
    return None


def detect_region(filename: str) -> str:
    """Extract region from filename."""
    for region, pattern in REGION_PATTERNS.items():
        if pattern.search(filename):
            return region
    return "Unknown"


def parse_size(size_str: str) -> int:
    """Parse human-readable size to bytes."""
    size_str = size_str.strip().upper()
    if size_str == "-" or not size_str:
        return 0

    multipliers = {
        "B": 1,
        "K": 1024,
        "KB": 1024,
        "KIB": 1024,
        "M": 1024**2,
        "MB": 1024**2,
        "MIB": 1024**2,
        "G": 1024**3,
        "GB": 1024**3,
        "GIB": 1024**3,
    }

    match = re.match(r"([\d.]+)\s*([A-Z]*)", size_str)
    if not match:
        return 0

    value = float(match.group(1))
    unit = match.group(2) or "B"
    return int(value * multipliers.get(unit, 1))


def clean_name(filename: str) -> str:
    """Clean ROM name from filename."""
    name = unquote(filename)
    name = re.sub(r"\.zip$|\.7z$|\.rar$", "", name, flags=re.IGNORECASE)
    return name


async def fetch_page(session: aiohttp.ClientSession, url: str) -> Optional[str]:
    """Fetch a page with retry logic."""
    global g_fetch_count, g_fetch_time

    for attempt in range(3):
        try:
            start = time.time()
            async with session.get(url, timeout=aiohttp.ClientTimeout(total=30)) as resp:
                if resp.status == 200:
                    html = await resp.text()
                    elapsed = time.time() - start
                    g_fetch_count += 1
                    g_fetch_time += elapsed
                    print(f"[{g_fetch_count}] Fetched {url.split('/')[-2:]} in {elapsed:.2f}s", file=sys.stderr)
                    return html
                print(f"HTTP {resp.status} for {url}", file=sys.stderr)
        except asyncio.TimeoutError:
            elapsed = time.time() - start
            print(f"Timeout (attempt {attempt + 1}, {elapsed:.2f}s) for {url}", file=sys.stderr)
        except aiohttp.ClientError as e:
            print(f"Error fetching {url}: {e}", file=sys.stderr)
        await asyncio.sleep(1 * (attempt + 1))
    return None


def parse_directory_listing(html: str, base_url: str) -> tuple[list[str], list[tuple[str, str, int]]]:
    """Parse myrient directory listing HTML."""
    global g_parse_time
    start = time.time()

    soup = BeautifulSoup(html, "lxml")
    directories = []
    files = []

    table = soup.select_one("#list tbody")
    if not table:
        return directories, files

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

        if href.startswith(".."):
            continue

        full_url = urljoin(base_url, href)

        if href.endswith("/"):
            directories.append(full_url)
        else:
            size = parse_size(size_text)
            files.append((name, full_url, size))

    elapsed = time.time() - start
    g_parse_time += elapsed

    return directories, files


async def process_directory(
    session: aiohttp.ClientSession,
    semaphore: asyncio.Semaphore,
    url: str,
    current_platform: Optional[str],
) -> tuple[list[RomEntry], list[tuple[str, Optional[str]]]]:
    """Process a single directory and return entries and new directories to crawl."""
    global g_entry_count, g_stop

    if g_stop:
        return [], []

    async with semaphore:
        html = await fetch_page(session, url)

    if not html or g_stop:
        return [], []

    platform = detect_platform(url) or current_platform
    directories, files = parse_directory_listing(html, url)

    entries = []
    new_dirs = []

    if g_platforms and platform and platform.upper() not in g_platforms:
        for dir_url in directories:
            dir_platform = detect_platform(dir_url)
            if not dir_platform or dir_platform.upper() in g_platforms:
                new_dirs.append((dir_url, platform))
        return entries, new_dirs

    for name, file_url, size in files:
        if g_stop:
            break

        if not name.lower().endswith((".zip", ".7z")):
            continue

        file_platform = platform
        if not file_platform:
            continue

        if g_platforms and file_platform.upper() not in g_platforms:
            continue

        if g_per_platform_limit > 0:
            platform_count = g_platform_counts.get(file_platform, 0)
            if platform_count >= g_per_platform_limit:
                continue

        region = detect_region(name)
        clean = clean_name(name)

        entries.append(RomEntry(
            platform=file_platform,
            region=region,
            name=clean,
            url=file_url,
            size=size,
        ))
        g_entry_count += 1
        g_platform_counts[file_platform] = g_platform_counts.get(file_platform, 0) + 1

        if g_limit > 0 and g_entry_count >= g_limit:
            print(f"  Limit of {g_limit} reached, stopping...", file=sys.stderr)
            g_stop = True
            break

    for dir_url in directories:
        if g_platforms:
            dir_platform = detect_platform(dir_url)
            if dir_platform and dir_platform.upper() not in g_platforms:
                continue

        if g_per_platform_limit > 0 and platform:
            platform_count = g_platform_counts.get(platform, 0)
            if platform_count >= g_per_platform_limit:
                continue

        new_dirs.append((dir_url, platform))

    return entries, new_dirs


async def crawl_all(
    session: aiohttp.ClientSession,
    start_url: str,
    semaphore: asyncio.Semaphore,
    platform_hint: Optional[str] = None,
) -> list[RomEntry]:
    """Crawl directories in parallel batches."""
    global g_stop
    all_entries = []
    queue = [(start_url, platform_hint)]
    batch_num = 0

    while queue and not g_stop:
        batch_num += 1
        batch = queue[:MAX_CONCURRENT]
        queue = queue[MAX_CONCURRENT:]

        batch_start = time.time()
        print(f"\n--- Batch {batch_num}: Processing {len(batch)} directories ({len(queue)} in queue) ---", file=sys.stderr)

        tasks = [
            process_directory(session, semaphore, url, platform)
            for url, platform in batch
        ]
        results = await asyncio.gather(*tasks)

        batch_elapsed = time.time() - batch_start
        total_new_entries = sum(len(entries) for entries, _ in results)
        total_new_dirs = sum(len(new_dirs) for _, new_dirs in results)

        counts_str = ", ".join(f"{p}:{g_platform_counts.get(p, 0)}" for p in sorted(g_platform_counts.keys()))
        print(f"--- Batch {batch_num} complete in {batch_elapsed:.2f}s: +{total_new_entries} entries, +{total_new_dirs} dirs | Total: {g_entry_count} [{counts_str}] ---", file=sys.stderr)

        for entries, new_dirs in results:
            all_entries.extend(entries)
            queue.extend(new_dirs)
            if g_stop:
                break

    return all_entries


def write_databases(entries: list[RomEntry], output_dir: Path) -> None:
    """Write TSV database files per platform."""
    output_dir.mkdir(parents=True, exist_ok=True)

    by_platform: dict[str, list[RomEntry]] = {}
    for entry in entries:
        by_platform.setdefault(entry.platform, []).append(entry)

    for platform, platform_entries in by_platform.items():
        platform_entries.sort(key=lambda e: e.name.lower())

        output_file = output_dir / f"romi_{platform}.tsv"
        with open(output_file, "w", encoding="utf-8") as f:
            for entry in platform_entries:
                line = f"{entry.platform}\t{entry.region}\t{entry.name}\t{entry.url}\t{entry.size}\n"
                f.write(line)

        print(f"Wrote {len(platform_entries)} entries to {output_file}")


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Crawl myrient.erista.me and generate TSV databases for ROMi",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Supported platforms:
  PSX, PS2, PS3, NES, SNES, GB, GBC, GBA, Genesis, SMS, MAME

Examples:
  %(prog)s --platform NES --limit 10              # Test with 10 NES ROMs
  %(prog)s --platform GB,GBC,GBA --per-platform 10  # 10 ROMs per Game Boy platform
  %(prog)s --platform PSX,PS2,PS3 --per-platform 10 # Balanced test set across PlayStation
  %(prog)s --limit 100                            # First 100 ROMs of any platform
        """,
    )
    parser.add_argument(
        "-p", "--platform",
        type=str,
        default="",
        help="Comma-separated list of platforms to crawl (e.g., NES,SNES,GB)",
    )
    parser.add_argument(
        "-l", "--limit",
        type=int,
        default=0,
        help="Maximum number of entries to collect (0 = unlimited)",
    )
    parser.add_argument(
        "--per-platform",
        type=int,
        default=0,
        help="Maximum entries per platform (0 = unlimited). Use with --platform for balanced test sets",
    )
    parser.add_argument(
        "-o", "--output",
        type=str,
        default="",
        help="Output directory for TSV files (default: tools/databases)",
    )
    return parser.parse_args()


async def main() -> None:
    """Main entry point."""
    global g_limit, g_per_platform_limit, g_platforms, g_entry_count, g_platform_counts, g_stop
    global g_fetch_count, g_fetch_time, g_parse_time

    args = parse_args()

    g_limit = args.limit
    g_per_platform_limit = args.per_platform
    g_entry_count = 0
    g_platform_counts = {}
    g_stop = False
    g_fetch_count = 0
    g_fetch_time = 0.0
    g_parse_time = 0.0
    if args.platform:
        g_platforms = {p.strip().upper() for p in args.platform.split(",")}

    output_dir = Path(args.output) if args.output else OUTPUT_DIR

    print(f"Starting crawl of {BASE_URL}")
    if g_platforms:
        print(f"  Platforms: {', '.join(sorted(g_platforms))}")
    if g_limit > 0:
        print(f"  Limit: {g_limit} entries")
    if g_per_platform_limit > 0:
        print(f"  Per-platform limit: {g_per_platform_limit} entries")

    semaphore = asyncio.Semaphore(MAX_CONCURRENT)

    headers = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Referer": BASE_URL,
    }
    connector = aiohttp.TCPConnector(limit=MAX_CONCURRENT)

    total_start = time.time()
    async with aiohttp.ClientSession(connector=connector, headers=headers) as session:
        if g_platforms:
            all_entries = []
            for platform in g_platforms:
                if platform in KNOWN_PLATFORM_PATHS:
                    platform_url = BASE_URL + KNOWN_PLATFORM_PATHS[platform]
                    print(f"\n🎯 Direct crawl: {platform} at {platform_url}", file=sys.stderr)
                    platform_entries = await crawl_all(session, platform_url, semaphore, platform)
                    all_entries.extend(platform_entries)
                else:
                    print(f"\n⚠️  No known path for {platform}, skipping", file=sys.stderr)
            entries = all_entries
        else:
            entries = await crawl_all(session, BASE_URL, semaphore)
    total_elapsed = time.time() - total_start

    print(f"\n=== Performance Summary ===")
    print(f"Total time: {total_elapsed:.2f}s")
    print(f"HTTP requests: {g_fetch_count} ({g_fetch_time:.2f}s total, {g_fetch_time/g_fetch_count:.2f}s avg)")
    print(f"HTML parsing: {g_parse_time:.2f}s total")
    print(f"Other overhead: {total_elapsed - g_fetch_time - g_parse_time:.2f}s")
    print(f"Found {len(entries)} ROM entries")

    write_databases(entries, output_dir)
    print("Done!")


if __name__ == "__main__":
    asyncio.run(main())
