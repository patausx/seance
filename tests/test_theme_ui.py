#!/usr/bin/env python3
"""Current palette roles/transition, preferences and cabinet; no media/old receipts."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    for command in (['python3','tests/test_theme_transition.py'],['python3','tests/test_theme_preferences.py'],['make','test-start-menu']):
        subprocess.run(command,cwd=ROOT,check=True)
if __name__=='__main__':main()
