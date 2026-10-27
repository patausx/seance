# asset tools

only generators for resources used by seance live here: fonts and title masks,
launcher icons and wordmark artwork, embedded audio, and DSP/UI lookup tables.
normal builds use the prepared files; no generator runs during installation or
an ordinary ARM build.

- `generate_*`: fonts, artwork and lookup tables; use `--check` where supported.
- `pixel_wordmark.py`: editable wordmark master; defaults to verification.
- `prepare_inst_bank.py`, `prepare_menu_audio.py`: embedded audio preparation.
- `render_readme_wordmark.py`: the current README GIF, not a design-study renderer.

asset READMEs describe the inputs, licenses and regeneration commands.
regeneration is an intentional source change, not a setup step.

[tests](../tests/README.md) and [release scripts](../scripts/README.md) are separate.
