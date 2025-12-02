#!/usr/bin/env python3
import sys
import re

def po_to_yts(po_file, yts_file):
    """Convert .po file to .yts format (key|value)"""
    try:
        with open(po_file, 'r', encoding='utf-8') as f:
            content = f.read()
    except UnicodeDecodeError:
        # Try with latin-1 encoding for old files
        with open(po_file, 'r', encoding='latin-1') as f:
            content = f.read()

    # Extract msgid/msgstr pairs
    pattern = r'msgid\s+"(.*)"\s+msgstr\s+"(.*)"'
    matches = re.findall(pattern, content, re.MULTILINE)

    with open(yts_file, 'w', encoding='utf-8') as f:
        for msgid, msgstr in matches:
            # Skip empty msgid (header)
            if not msgid:
                continue
            # Escape pipes and backslashes
            msgid_escaped = msgid.replace('\\', '\\\\').replace('|', '\\|')
            msgstr_escaped = msgstr.replace('\\', '\\\\').replace('|', '\\|')
            # Write in yts format: key|value
            f.write(f"{msgid_escaped}|{msgstr_escaped}\n")

    print(f"Converted {len([m for m in matches if m[0]])} translations from {po_file} to {yts_file}")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: po_to_yts.py input.po output.yts")
        sys.exit(1)
    po_to_yts(sys.argv[1], sys.argv[2])
