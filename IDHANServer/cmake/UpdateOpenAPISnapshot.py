"""Format the compiled OpenAPI export for version control only."""

import json
import sys
from pathlib import Path


def main():
    source, snapshot = map(Path, sys.argv[1:])
    document = json.loads(source.read_text(encoding="utf-8"))
    formatted = json.dumps(document, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    if snapshot.exists() and snapshot.read_text(encoding="utf-8") == formatted:
        return

    snapshot.write_text(formatted, encoding="utf-8")


if __name__ == "__main__":
    main()
