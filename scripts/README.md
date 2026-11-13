# release scripts

run from the repository root with Python 3.10+. these helpers are not ordinary
application build or runtime dependencies.

## export and build

```sh
python3 -B scripts/prepare_public_release.py --source-only --output release/new-source
python3 -B scripts/prepare_public_release.py --output release/new-candidate --jobs 2 --makerom /path/to/makerom
```

choose a new output directory outside source-input directories. `--source-only`
writes an exported tree to `<output>/source/` without needing devkitARM or makerom.
otherwise, the script builds 3DSX/CIA in that copy and puts binaries and application/source
ZIPs in `<output>/github-release/`. `--jobs` accepts 1 or 2; `--makerom` selects the tool.
nothing is downloaded, uploaded, installed or deployed.

check exported text, paths and relative documentation links from the exported tree:

```sh
python3 -B scripts/audit_public_source.py
```

## package existing files

```sh
python3 -B scripts/package_release.py --main
python3 -B scripts/package_release.py --source
```

`--main` requires existing `seance.3dsx` and `seance.cia`; `--source` collects source.
outputs are `release/seance-<VERSION>.zip` and `release/seance-<VERSION>-source.zip`.
archive names follow `VERSION`; the packager does not check a binary's embedded version.
`make release` builds/packages the application; `make release-source` packages source only.
use the [isolated build](../BUILDING.md#release-coverage-and-source-to-artifact-procedure)
to package application and source from the same copy.

source export selects helpers through `scripts/source-files.json` and application
sources/resources through directory allowlists in `package_release.py`. local tests
are omitted; missing required inputs, unsafe paths and symlinks are rejected.
