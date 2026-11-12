# release scripts

these scripts are not application build or runtime dependencies.

```sh
python3 scripts/audit_public_source.py
python3 scripts/prepare_public_release.py --source-only --output release/new-source
python3 scripts/prepare_public_release.py --output release/new-candidate
```

choose a new output directory. without `--source-only`, the preparer builds
3DSX/CIA in an isolated source copy, packages it and writes a local receipt.
use `--jobs 1` or `--jobs 2` and `--makerom /path/to/makerom` if needed. nothing
is uploaded or deployed. the receipt binds the exported source inventory to the
fresh build, selected tool/rule/runtime hashes, complete log, final link map,
artifacts and download archives. these records remain local; actual linked
members, corresponding source, grants and hardware acceptance need separate review.
`publication_ready:false` is deliberate, including the unrecorded terms of the menu-audio pack.

`package_release.py` packages existing artifacts without compiling them:

```sh
python3 scripts/package_release.py --main
python3 scripts/package_release.py --source
python3 scripts/package_release.py --samples
```

`make release` is application + starter samples only; `make release-source`
creates source separately and does not prove matching existing artifacts. the
packager validates the package version name against VERSION, **not** a binary's
embedded version or build origin. use the isolated preparer and retain its matching
records for release admission; see [the procedure](../BUILDING.md#release-coverage-and-source-to-artifact-procedure).
a source-only export has no artifact provenance. packaging is not publication.

`source-files.json` is the explicit admission list for asset tools, release
scripts. local tests and unlisted files never enter source packages;
missing listed inputs, unsafe paths and symlinks fail the export. application
sources, asset originals and license texts have their own directory allowlist.
