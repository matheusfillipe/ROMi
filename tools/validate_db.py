#!/usr/bin/env python3
"""
Database Format Validator

Validates that all TSV database files follow the correct format:
Platform\tRegion\tName\tURL\tSize

Usage:
    python3 tools/validate_db.py
"""
import sys
from pathlib import Path

db_dir = Path("tools/databases")
valid = True
failed_files = []

print("=== Database Format Validation ===\n")

for db_file in sorted(db_dir.glob("romi_*.tsv")):
    platform = db_file.stem.replace("romi_", "")
    print(f"📋 {platform}")
    file_valid = True

    with open(db_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    if not lines:
        print(f"  ✗ Empty file!")
        valid = False
        file_valid = False
        failed_files.append(db_file.name)
        continue

    field_counts = set()
    issues = []

    for i, line in enumerate(lines, 1):
        fields = line.rstrip('\n').split('\t')
        field_counts.add(len(fields))

        if len(fields) != 5:
            issues.append(f"Line {i}: {len(fields)} fields (expected 5)")
            if i == 1 or i == len(lines):
                print(f"  ✗ {'First' if i == 1 else 'Last'} row: {len(fields)} fields")
                print(f"     {line.rstrip()[:100]}...")

        if i == 1:
            plat, region, name, url, size = fields
            print(f"  ✓ First row:")
            print(f"     Platform: {plat}")
            print(f"     Region: {region}")
            print(f"     Name: {name[:40]}...")
            print(f"     URL: {url[:60]}...")
            print(f"     Size: {size}")

            if plat != platform:
                print(f"  ✗ Platform mismatch: {plat} != {platform}")
                valid = False
                file_valid = False

        if i == len(lines):
            plat, region, name, url, size = fields
            print(f"  ✓ Last row:")
            print(f"     Platform: {plat}")
            print(f"     Region: {region}")
            print(f"     Name: {name[:40]}...")
            print(f"     URL: {url[:60]}...")
            print(f"     Size: {size}")

    if len(field_counts) == 1 and 5 in field_counts:
        print(f"  ✓ All {len(lines)} rows have 5 fields")
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
