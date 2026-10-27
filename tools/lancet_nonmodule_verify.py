#!/usr/bin/env python3
"""Verify the current lancet scope with the explicit original-mixer exception.

The old hardcoded optimized/sanitized trio predates the user's mixer revert.
Those receipts are retained as historical evidence, never recertified as current.
This command is read-only and does not build or capture anything.
"""
from pathlib import Path
import runpy

if __name__=='__main__':
    path=Path(__file__).resolve().parents[1]/'release/lancet-everywhere/verify_current.py'
    runpy.run_path(str(path),run_name='__main__')
