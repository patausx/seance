# release scripts

these scripts are not application build or runtime dependencies.

```sh
python3 scripts/audit_public_source.py
python3 scripts/prepare_public_release.py --source-only --output release/new-source
python3 scripts/prepare_public_release.py --output release/new-candidate
```

choose a new output directory. without `--source-only`, the preparer builds
3DSX/CIA in an isolated source copy, packages it and writes a local receipt.
use `--makerom /path/to/makerom` if needed. nothing is uploaded or deployed.

`package_release.py` packages existing artifacts without compiling them:

```sh
python3 scripts/package_release.py --main
python3 scripts/package_release.py --source
python3 scripts/package_release.py --samples
```

`source-files.json` is the explicit admission list for asset tools, release
scripts. local tests and unlisted files never enter source packages;
missing listed inputs, unsafe paths and symlinks fail the export. application
sources, asset originals and license texts have their own directory allowlist.
