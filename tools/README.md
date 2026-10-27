# tools

these are build, asset-preparation and verification tools, not application
runtime dependencies. to use seance, install a built application as described
in the [user guide](../GUIDE.md). to compile it, see [building](../BUILDING.md).

## build and assets

- `generate_*`: checked-in fonts, title masks, branding and DSP/UI tables.
- `pixel_wordmark.py`: editable pixel artwork and mask verification.
- `prepare_inst_bank.py`, `prepare_menu_audio.py`: embedded audio preparation.
- `render_readme_wordmark.py`: the current README GIF, rendered from production
  geometry. `--check` verifies pixels/timing; `--install` explicitly rebuilds it.
- `prepare_public_release.py`, `package_release.py`: local source export and
  release packaging. neither command publishes anything.

normal application builds consume the prepared assets. do not regenerate them
as an installation step. asset READMEs document the original inputs and tools.

## checks

```sh
make test-module-frame
python3 tools/test_nav_instant.py
python3 tools/test_package_release.py
python3 tools/test_audit_public_source.py
python3 tools/audit_public_source.py
make test-branding
```

`test_*` runners and their C++ fixtures exercise engine and UI behavior; stub
headers provide narrow host substitutes for console APIs. these are development
checks, not device performance or listening tests. some historical checks still
need separately retained baselines; a missing baseline is not a pass.

## source admission

`public_tools.json` explicitly lists the tools included in source exports.
unlisted local files are never admitted by filename or extension alone. missing
listed inputs fail the export. add a new tool only when a build, asset generator
or supported verification path needs it, together with its dependencies.

standalone design renders, promo captures, browser previews and archived visual
experiments stay in the local development workspace, outside the repository
payload. native captures requested by a test runner remain available for scoped
UI verification. see the [source layout](../docs/source-layout.md).
