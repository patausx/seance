"""Host-test imports; never loaded by the application build."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
for directory in ("tools", "scripts"):
    sys.path.insert(0, str(ROOT / directory))
