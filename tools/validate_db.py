#!/usr/bin/env python3
"""
Database Format Validator

Validates that all TSV database files follow the correct format:
Platform\tRegion\tName\tURL\tSize

Usage:
    python3 tools/validate_db.py [db_dir]
"""
import sys
from pathlib import Path

db_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("tools/databases")
valid = True
failed_files = []

print("=== Database Format Validation ===\n")

for db_file in sorted(db_dir.glob("romi_*.tsv")):
    platform = db_file.stem.replace("romi_", "")
    is_combined = (platform == "db")
    print(f"📋 {platform}{' (combined)' if is_combined else ''}")
    file_valid = True

    with open(db_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    if not lines:
        print(f"  ✗ Empty file!")
        valid = False
        file_valid = False
        failed_files.append(db_file.name)
        continue

    data_lines = [line for line in lines if line.strip() and not line.strip().startswith('#')]

    if not data_lines:
        print(f"  ✗ No data lines (only comments)!")
        valid = False
        file_valid = False
        failed_files.append(db_file.name)
        continue

    field_counts = set()
    issues = []
    first_data_line = None
    last_data_line = None

    for i, line in enumerate(lines, 1):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue

        fields = line.rstrip('\n').split('\t')
        field_counts.add(len(fields))

        if len(fields) != 5:
            issues.append(f"Line {i}: {len(fields)} fields (expected 5)")
            if first_data_line is None or i == len(lines):
                print(f"  ✗ {'First' if first_data_line is None else 'Last'} data row: {len(fields)} fields")
                print(f"     {line.rstrip()[:100]}...")

        if first_data_line is None and len(fields) == 5:
            first_data_line = fields
            plat, region, name, url, size = fields
            print(f"  ✓ First row:")
            print(f"     Platform: {plat}")
            print(f"     Region: {region}")
            print(f"     Name: {name[:40]}...")
            print(f"     URL: {url[:60]}...")
            print(f"     Size: {size}")

            if not is_combined and plat != platform:
                print(f"  ✗ Platform mismatch: {plat} != {platform}")
                valid = False
                file_valid = False

        if len(fields) == 5:
            last_data_line = fields

    if last_data_line:
        plat, region, name, url, size = last_data_line
        print(f"  ✓ Last row:")
        print(f"     Platform: {plat}")
        print(f"     Region: {region}")
        print(f"     Name: {name[:40]}...")
        print(f"     URL: {url[:60]}...")
        print(f"     Size: {size}")

    if len(field_counts) == 1 and 5 in field_counts:
        print(f"  ✓ All {len(data_lines)} data rows have 5 fields")
    else:
        print(f"  ✗ Inconsistent field counts: {field_counts}")
        for issue in issues[:3]:
            print(f"     {issue}")
        if len(issues) > 3:
            print(f"     ... and {len(issues) - 3} more")
        valid = False
        file_valid = False

    if not file_valid:
        failed_files.append(db_file.name)

    print()

if valid:
    print("✅ All database files are valid!")
    sys.exit(0)
else:
    print("❌ Database files with issues:")
    for filename in failed_files:
        print(f"   - {filename}")
    sys.exit(1)
