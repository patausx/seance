# publication checklist

this is a release gate, not a record that its boxes have passed. documentation,
local builds, and local ZIPs do **not** authorize publication. no push, tag,
release upload, store request, deployment, or hardware installation is implied.
current candidate version: **0.1.0-alpha.1**. do not rename it stable without a
separate version decision.

## blocking rights review

- [ ] review bundled asset distribution terms alongside the overall
  corresponding-source review.
- [ ] preserve the OFL text and font copyright/reserved-name notices, original
  OTF, derivative source/generators, and binary-package font notice.
- [ ] retain exact CC0 instrument inputs, manifests, upstream credits/license
  texts, and optional starter-pack notices. exclude mixed-license private
  sample libraries and user recordings/projects.
- [ ] audit branding and any proposed screenshots/promotional audio for rights
  and personal data. sample-bank permission does not clear a demo recording.

## complete source and dependency review

- [ ] inspect an allowlisted public source export, not a dump of the development
  tree or its Git history. exclude local receipts, device/network details,
  captures, private libraries, generated build directories, and credentials.
- [ ] include all production `.inc` files and preferred editable asset sources,
  not just generated C/C++ headers. include preparation/build/install scripts
  needed to reproduce the released binaries.
- [ ] rebuild from the exported source and record its exact content identity.
- [x] locally retain original libctru 2.7.0, citro2d 1.7.0, citro3d 1.7.1 and
  devkitarm-crtls 1.2.6 source archives, verified against the effective checksums
  in saved recipes at commit `b9e8b0de6a6423557fd2a57dc100bf327c79dd4a`.
  archive/recipe hashes, installed versions and library hashes are recorded in
  `dependency-sources/manifest.json`; complete per-file notices stay in archives.
- [x] locally retain CC0 instrument/starter, OFL, exact zlib library notices,
  MPL-2.0 startup terms, GCC GPL/runtime exception texts and installed-header
  evidence in `licenses/`. this is not a completed linked-runtime audit.
- [ ] finish GPL corresponding-source coverage for the actual final link,
  including newlib's notices and treatment of each runtime component: see
  [dependency-sources.md](dependency-sources.md). do not assert a blanket System
  Libraries exception or extend the GCC exception to newlib/startup code.
- [ ] retain all required source, recipes and notices in the final packages and
  provide equivalent access alongside the binary downloads. local presence
  alone does not prove final package contents or public availability.
- [ ] verify license/source access for **every download route**, including
  standalone `.3dsx`, CIA, ZIP, and a future Universal-Updater install.

## candidate identity and technical checks

- [ ] record the final source revision/content manifest and toolchain versions.
- [ ] keep `VERSION`, package names, app metadata, proposed release tag, source
  archive, and release notes consistent. retain alpha/prerelease status.
- [ ] record actual build/test commands and exits for this candidate. distinguish
  skipped checks, missing private historical fixtures, and known failures.
- [ ] check native UI bounds and both themes; perform relevant input/recording/
  persistence/compatibility checks. no blanket "all tests pass" without evidence.
- [ ] inspect archives and checksums. verify that release files come from this
  isolated build, and that the source/dependency archives match them.
- [ ] verify CIA unique/title IDs against the final artifact and conflict policy
  if a CIA is offered. an RSF value is not a collision check or install test.

## hardware and user safety — not established by a host build

- [ ] record actual console model(s), firmware/homebrew environment and launch
  format(s); do not extrapolate one model's result to every Old/New 3DS.
- [ ] test clean Homebrew Launcher installation and launch; CIA installation only
  if offered and separately authorized. record failures/unsupported paths.
- [ ] test audio/listening, long-running playback, microphone permission/setup,
  recording save/load, and relevant resource limits on device.
- [ ] test upgrading without overwriting existing patches, samples or recordings.
  back up user data and document format/rollback limitations.
- [ ] for a future store install, test the **generated** install/update script,
  target paths, optional sample handling, and source/license delivery.
- [ ] publish honest known limitations; host render timing is not real-device
  frame rate and a successful link is not a listening test.

## canonical hosting and release — all pending

an optional private-first source review is described in
[repository preparation](repository.md). creating/pushing that private repository
and making it public are separate approved actions, not implied by local cleanup.

- [ ] confirm repository owner/name and default branch. no Git remote was
  configured at preparation time; author `patausx` is not a confirmed hosting URL.
- [ ] confirm source tag, release URL, downloadable filenames, icon and banner
  URLs. replace all `REPLACE_...`, `example.invalid`, and bracketed draft fields.
- [ ] obtain explicit authorization for repository creation/push/tag/upload.
- [ ] after that authorization only, publish the reviewed matching assets and
  source/notices, verify public downloads and SHA-256 checksums, and keep the
  alpha marked prerelease. none of these actions is executed by this checklist.

## universal-db — a separate human submission

- [ ] re-read the [current upstream guidelines](https://github.com/Universal-Team/db/blob/master/CONTRIBUTING.md)
  and [app-request form](https://db.universal-team.net/app-request).
- [ ] have an authorized **human** author the actual metadata and issue/PR text.
  upstream prohibits LLM-generated contributions to the database itself and
  LLM-written issue/PR descriptions. the local JSON is a field reference only;
  do not submit it verbatim.
- [ ] disclose generated content accurately (`llm_generation: yes` for the
  present candidate); `minor` has specific upstream criteria.
- [ ] verify accessible release assets, archive/custom-script needs, native
  screenshots, and prerelease discovery with current upstream behavior.
- [ ] obtain separate authorization to open the issue/PR. acceptance is pending
  and at upstream's discretion; a prepared JSON does not imply listing.
- [ ] after review, verify the generated page/UniStore and actual update behavior.

## completion record

[distribution/README.md](README.md) is the intended location for the actual local
artifact paths, hashes, commands run, and remaining gaps once preparation has
finished. leave a box unchecked unless it has evidence; record partial/local
success without turning unresolved rights, URLs, device QA, or upstream review
into a pass.
