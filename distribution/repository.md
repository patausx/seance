# repository preparation

seance can be developed in a private GitHub repository first and opened later.
keep the repository separate from the local development archive. making it
private is not a substitute for reviewing what you upload: its history will
normally become visible when the repository becomes public.

this guide does not create a repository, use GitHub credentials or upload files.

## 1. make a clean source copy

from the development workspace:

```sh
python3 tools/audit_public_source.py
python3 tools/test_package_release.py
python3 tools/prepare_public_release.py \
  --source-only --output release/repository-review-01
```

choose a new output name each time. `source/` inside the result is the repository
candidate. the adjacent inventories and local receipt are review records, not
repository contents. the export includes current code, documentation, editable
asset sources, required generated inputs, license texts and dependency sources.
it does not copy `.git`, local agent state, personal samples, device backups,
old release trees, screenshots or promotional captures.

inspect the copy itself, not just the workspace:

```sh
cd release/repository-review-01/source
python3 tools/audit_public_source.py
python3 tools/test_package_release.py
make test-module-frame
python3 tools/test_nav_instant.py
```

the audit checks English prose, a small set of privacy/credential patterns, and
relative Markdown links/anchors in the exported documentation. it is not a
comprehensive secret scanner or a rights audit, and does not inspect binary
asset contents or nested dependency archives. review those separately. deliberate
multilingual wordmark artwork and Unicode filename tests are retained.

## 2. review before the first push

- read the README and follow the guide as a new user.
- inspect the file list and sizes. keep compiled downloads out of source control.
- review third-party assets, embedded audio, source archives and license notices.
- use a dedicated secret scanner as well as a manual review. inspect any generated
  metadata, filenames and commit author email before publishing them.
- make a clean build using [BUILDING.md](../BUILDING.md). record actual test
  results and remaining hardware gaps; do not reuse an old binary as this build.

some retained historical tools need excluded local baselines/artwork. their
presence does not promise a fully portable historical test suite.

## 3. create the private repository when authorized

confirm the GitHub owner and repository name with the maintainer. work **inside
the reviewed `source/` copy**, never inside the original development checkout.
start a new history rather than pushing the old local history:

```sh
# local setup inside the reviewed source copy
# stop if this directory already has a .git directory

git init -b main
git status --short
git add .
git diff --cached --stat
git diff --cached --check
git diff --cached
# inspect git var GIT_AUTHOR_IDENT and GIT_COMMITTER_IDENT before committing
git commit -m "initial source import"
```

only after the owner/name and upload are explicitly approved, an authenticated
GitHub CLI can create and push a **private** repository:

```sh
# replace OWNER/REPOSITORY with the confirmed destination
gh repo create OWNER/REPOSITORY --private --source . --remote origin --push
```

verify that the resulting repository is private. do not add release artifacts,
workflow uploads, device access or automatic deployment as part of this step.
GPL-licensed code can be worked on privately; inviting others or distributing
binaries still requires attention to the applicable source and license terms.

## 4. use the private phase

keep one default branch and small focused changes. use issues for reproducible
bugs and a short list of next tasks, not copies of private chat logs. useful
first tasks are a clean-install walkthrough, small shareable example patches,
a few current screenshots and a console testing table. add CI only for checks
that run independently in a clean checkout.

## 5. open it deliberately

before changing visibility:

- review **all committed history**, not only the latest tree, for secrets, personal
  data, private samples and files that should never have been committed;
- also review branches, tags, issues, pull requests, releases, attachments,
  automation logs/artifacts and collaborator permissions;
- rotate any credential that was accidentally uploaded; deleting its latest
  file does not remove it from history or revoke it;
- replace draft repository/download URLs; make source, notices and actual
  release contents match;
- complete [the publication checklist](publication-checklist.md) for a release,
  retain alpha/prerelease status and report actual hardware coverage;
- obtain explicit approval to make the repository public.

opening source and publishing a binary release are separate actions. neither
step requires a Universal-DB submission or a device deployment.
