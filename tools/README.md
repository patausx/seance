# asset tools

resource preparation tools for fonts, artwork, embedded instrument audio and
DSP/UI lookup tables. normal builds use checked-in results; run a tool only
when changing its inputs.

- `generate_*`: fonts, artwork and lookup tables; use `--check` where supported.
- `pixel_wordmark.py`: editable wordmark master; defaults to verification.
- `prepare_inst_bank.py`: embedded instrument audio preparation.

asset READMEs cover inputs, licenses and regeneration commands, including
[fonts](../assets/fonts/README.md) and [instruments](../assets/instruments/README.md).
source export and packaging live in [release scripts](../scripts/README.md).
