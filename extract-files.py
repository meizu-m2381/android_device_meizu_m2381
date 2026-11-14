#!/usr/bin/env python3
"""Import the verified local input set. See docs/local-inputs.md."""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[3]
raise SystemExit(subprocess.call([sys.executable, str(root / "tools/import_local_inputs.py"), *sys.argv[1:]]))
