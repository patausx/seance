# tests

optional host checks, fixtures, narrow console API stubs, and test-only reference
data. none of these files are required to compile or run the 3DS application.
run commands from the repository root:

```sh
make test-module-frame
python3 tests/test_nav_instant.py
python3 tests/test_package_release.py
python3 tests/test_audit_public_source.py
make test-branding
```

`tests/Makefile` contains the host targets and is loaded only for explicit
`test-*`, `sanitize*`, `check-module-frame` or named host-binary targets.
`make test-module-frame` includes sanitizer-backed native geometry checks;
other `test_*_core.py` and `test_*_ui.py` runners cover their named subsystems.
read a runner's prerequisites and scope before invoking it.

some historical regression helpers require separately retained baselines. they
are not part of the quick checks above. a missing baseline is a blocked check,
not a pass. there is no claim that every historical target passes on current
source. outputs belong in ignored `release/`, `ui_mockups/` or `/tmp` locations.

host checks do not establish console FPS, sound quality or hardware acceptance.
