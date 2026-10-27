# screenshots, video and audio

GitHub can host still images, animated GIFs, videos and audio files. use a small
amount of media to explain the instrument; keep large masters and repeated
captures out of the source history.

## recommended presentation

- **README:** one clear screenshot of both screens, a short GIF if it explains a
  gesture, and a preview image linking to the main video. an image link is a
  reliable fallback when a Markdown view does not show an inline video player.
- **video demo:** a short MP4 using H.264 video and AAC audio is a practical
  browser-compatible choice. a video with sound is more convenient than a GIF
  plus a separate audio download.
- **audio demo:** MP3 for listening and WAV when the original export matters.
  attach to a release or supported issue/discussion comment and link to it.
  do not depend on an HTML `<audio>` element working inside a GitHub README.
- **release downloads:** application packages, matching source, checksums and
  optional demo media belong in release assets, not in repeated Git commits.

no screenshots, recordings or demo assets are selected or uploaded by this guide.

## upload limits

GitHub's attachment documentation currently lists:

| attachment | maximum size |
| --- | --- |
| image or GIF | 10 MB |
| video in a repository owned by a free-plan account/organization | 10 MB |
| video in a repository owned by a paid-plan account/organization | 100 MB, subject to uploader access/plan rules |
| other attachments, including supported MP3/WAV uploads | 25 MB |

these are **comment/attachment limits**, not Git commit limits or release-asset
limits. release assets use their own rules. verify the current documentation
before preparing a large upload. uploading an attachment happens immediately,
even before the surrounding comment is submitted.

supported video extensions include `.mp4`, `.mov` and `.webm`; codec support
still depends on the browser. H.264 is GitHub's recommended compatibility choice.
private-repository attachments require repository access under the documented
current behavior; still review the files before uploading them.

## preparing a seance demo

1. use a small project and samples that can be shared. avoid private project
   names, network overlays, desktop notifications and personal paths.
2. keep both displays readable. state whether the footage is from a console,
   a native host capture or an authored design study.
3. distinguish instrument audio from an added soundtrack. do not present an
   editorial soundtrack as proof of device sound or latency.
4. trim and compress a copy; keep its original capture locally. GIF has no
   audio and is often much larger than an equivalent short MP4.
5. review the encoded result, including its audio, before attaching it. add
   actual links to the README only after the destination is confirmed.

## official references

- [attaching files and supported formats](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/attaching-files)
- [releases and downloadable assets](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)
