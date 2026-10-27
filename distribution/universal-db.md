# universal-db submission reference

**local draft only — not submitted, accepted, hosted, or ready to paste into an
upstream issue.** the checked-out project has no Git remote. no public GitHub
owner/repository or download URL is confirmed. author credit `patausx` comes
from the Makefile, not from a verified GitHub account. the current project
version is `0.1.0-alpha.1`; do not advertise this candidate as stable.

## authoritative upstream instructions

reviewed by public HTTP GET on 2026-10-24; re-check before a future submission:

- actual repository: <https://github.com/Universal-Team/db>
- [contribution guidelines and app requirements](https://github.com/Universal-Team/db/blob/master/CONTRIBUTING.md)
- [app-request form](https://db.universal-team.net/app-request)
- [current issue form](https://github.com/Universal-Team/db/blob/master/.github/ISSUE_TEMPLATE/app-request.yml)
- [app-template wiki](https://github.com/Universal-Team/db/wiki/App-Template)
- [form field definitions](https://github.com/Universal-Team/db/blob/master/docs/assets/js/app-request.js)
- [app metadata example](https://github.com/Universal-Team/db/blob/master/source/apps/ctrmus.json)
- [generator](https://github.com/Universal-Team/db/blob/master/source/generate.py)
  and [generated UniStore format implementation](https://github.com/Universal-Team/db/blob/master/source/unistore.py)

there is no confirmed `source/schema.json` endpoint. the reference JSON below
uses fields verified in the live form's `appSchema` and generator/examples; it
has **not** been accepted by Universal-DB or validated against a published
formal JSON Schema. JSON syntax validity alone is not submission validity.
the wiki can lag the live form: its basic sample even contains a trailing comma;
do not copy it as strict JSON without checking.

## important llm policy

upstream currently requires disclosure of directly incorporated LLM-generated
content and does not allow LLM-generated contributions to Universal-DB itself
or LLM-written issue/PR descriptions. this documentation and its accompanying
JSON reference were prepared with an LLM. **do not submit them verbatim.** a
human maintainer must review the project's use of generated material, author
the actual metadata and submission text themselves, and comply with the then
current upstream policy. the present reference sets `llm_generation` to `yes`:
generated documentation is directly incorporated, and the narrower `minor`
criteria have not been established. do not change that to `no` merely because
the final submission is human-written.

## local reference fields

[universal-db-entry.json](universal-db-entry.json) is deliberately incomplete:
`REPLACE_OWNER/REPLACE_REPOSITORY` is a placeholder, and `example.invalid` URLs
must never be submitted. no actual hosted links are asserted. the human final
entry belongs at `source/apps/seance.json` in an upstream contribution.

| field | intended value / decision |
| --- | --- |
| `github` | confirmed `owner/repository`; not an inferred username |
| `title` | `seance` |
| `author` | `patausx`, subject to maintainer confirmation |
| `description` | short sampler/synth description; current form limit 256 characters |
| `systems` | `["3DS"]`; no DS support claim |
| `categories` | `["app", "media"]`, both accepted live-form categories |
| `llm_generation` | `yes` for the present generated-content state |
| `icon` | confirmed public 48×48 PNG URL for the actual release identity |
| `image` | confirmed public banner/screenshot PNG URL; form optional |
| `download_filter` | select only intended installable standalone release files |

**proposed short description, for human rewriting:** a patchable sampler and
synthesizer for Nintendo 3DS with granular playback, effects, microphone
recording, modulation, and a step sequencer.

no download, version/date, CIA title ID, or custom installer is invented in the
reference. release version and download metadata are normally derived from the
linked repository's releases. do not hard-code a local build date as a published
release date. if CIA publication is approved, check the **actual final CIA** and
collision policy before adding `unique_ids`; JSON uses decimal integers, not
hexadecimal literals. `seance.rsf` alone is not evidence that a CIA has been
built, installed, or is conflict-free.

## app entry vs universal-updater metadata

- `source/apps/seance.json` is the **input app entry** maintained in Universal-DB.
  a GitHub entry lets its generator read repository/release information.
- Universal-DB generates website pages and **UniStore update/install metadata**
  consumed by Universal-Updater. a local app-entry JSON is not a standalone
  `.unistore`, an update feed, or proof that the app appears in the updater.
- direct `.3dsx` and `.cia` release assets let upstream generate ordinary install
  scripts. the current form does **not** fully describe archive extraction or
  arbitrary scripts; ZIP-only releases need a human-authored `archive` mapping
  or custom `scripts` using the wiki and current generator semantics.
- keep application files separate from user projects, recordings, and sample
  folders. an installer/update must never overwrite user data. optional starter
  samples require an explicit separate choice, not an automatic destructive
  update.
- test what a **prerelease-only** repository exposes: `0.1.0-alpha.1` should stay
  a GitHub prerelease, but source discovery of a prerelease is not a guarantee
  that normal default-store users receive it. ask upstream if a stable release
  is required; do not relabel an alpha just to obtain listing/update behavior.

## future workflow — not executed here

1. clear [publication blockers](publication-checklist.md), particularly menu
   audio rights, corresponding source, final-package QA, and canonical URLs.
2. obtain explicit authorization to publish. create the canonical repository
   and a human-reviewed `0.1.0-alpha.1` prerelease only when authorized. publish
   matching source, notices, and checksums with installable assets; verify the
   resulting public URLs and exact asset names. local files are not URLs.
3. review the current Universal-DB rules: developers must authorize the request;
   GitHub Releases are strongly recommended for tracked downloads (not a blanket
   requirement that every app be hosted on GitHub). confirm all applicable
   requirements from upstream, including its current LLM policy.
4. a human maintainer uses the app-request form and writes/validates the actual
   metadata. the companion JSON is a field reference only. use real raw image
   URLs, a real repository, accurate generated-content disclosure, and only
   supported categories. omit unnecessary fields.
5. choose the upstream-supported route: a human-authored pull request adding
   `source/apps/seance.json`, or an issue using `app-request.yml` with the human
   JSON attached. upstream notes PRs are generally faster to review. **neither
   route has been performed** as part of this preparation.
6. optionally provide rights-cleared native screenshots. upstream recommends
   400×480 PNG, no inter-screen gap, 1× resolution, lower-screen side areas
   transparent, under `docs/assets/images/screenshots/seance/`. do not submit
   private device captures or promotional media without review.
7. have upstream review the generated download scripts and licensing delivery.
   a direct binary installation does not automatically install adjacent license
   files. ensure font/audio/runtime notices and the matching source access are
   delivered appropriately; use a reviewed custom install script if needed.
8. only after acceptance and separate authorization, inspect the generated page
   and UniStore, then test an actual clean install and update on hardware. record
   installation paths, source/license access, and preservation of existing user
   patches and samples. acceptance and hardware success remain pending.

## required inputs still missing

- confirmed repository owner/name, default branch, release tag/URLs, public icon
  and banner URLs;
- a reviewed complete corresponding-source distribution and dependency notices;
- final checksums and package contents from the actual candidate build;
- hardware clean-install/update verification and supported console matrix;
- a human-authored, policy-compliant Universal-DB submission and upstream review.
