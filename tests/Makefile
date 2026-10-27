# Optional host checks. Invoke from the repository root with make test-<name>.

# Module admission is a cheap source-only preflight, not a native capture or upload.
.PHONY: check-module-frame test-module-frame-contract test-module-frame
check-module-frame:
	python3 tests/check_module_frame_contract.py

test-module-frame-contract: check-module-frame
	python3 tests/test_module_frame_contract.py

test-module-frame: test-module-frame-contract
	python3 tests/test_module_frame.py

.PHONY: test-menu-audio
test-menu-audio:
	python3 tests/test_menu_audio.py --sanitize

# Real sequencer UI/controller, real portable engine/theme/font, host hardware shim.
SEQUENCER_TEST_SOURCES := tests/sequencer_test.cpp platform/3ds/ui_sequencer.cpp platform/3ds/ui_top.cpp \
  platform/3ds/ui_sigils.cpp platform/3ds/ui_patchbay.cpp platform/3ds/ui_nav.cpp platform/3ds/ui_performance.cpp platform/3ds/ui_transition.cpp platform/3ds/ui_order.cpp \
  platform/3ds/ui_param_lens.cpp platform/3ds/ui_module_focus_generators.cpp platform/3ds/ui_module_focus_motor.cpp platform/3ds/ui_module_focus_osc.cpp platform/3ds/ui_module_focus_noise.cpp platform/3ds/ui_module_focus_play.cpp platform/3ds/ui_module_focus_grain.cpp \
  platform/3ds/ui_play_editor.cpp platform/3ds/ui_play_export.cpp \
  platform/3ds/fm_context.cpp platform/3ds/menu_style.cpp \
  platform/3ds/ui_module_focus_common.cpp platform/3ds/ui_module_focus_ghost.cpp \
  platform/3ds/ui_module_focus_stranular.cpp platform/3ds/ui_module_focus_reverb.cpp \
  platform/3ds/module_inspector.cpp platform/3ds/ui_module_focus_vca.cpp platform/3ds/ui_module_focus_chorus.cpp \
  platform/3ds/ui_module_focus_delay.cpp platform/3ds/ui_module_focus_filter.cpp platform/3ds/ui_module_focus_fold.cpp platform/3ds/ui_module_focus_bit.cpp platform/3ds/ui_module_focus_tilt.cpp platform/3ds/ui_module_focus_ring.cpp platform/3ds/ui_module_focus_strain.cpp platform/3ds/ui_module_focus_mic.cpp platform/3ds/ui_module_focus_recorder.cpp \
  platform/3ds/ui_module_focus_bird.cpp platform/3ds/ui_module_focus_shift.cpp platform/3ds/ui_module_focus_fract.cpp platform/3ds/ui_module_focus_comp.cpp platform/3ds/ui_module_focus_mouth.cpp platform/3ds/ui_module_focus_arp.cpp platform/3ds/ui_module_focus_glitch.cpp platform/3ds/ui_module_focus_inst.cpp \
  platform/3ds/ui_theme.cpp core/ui/draw.cpp core/ui/font.cpp \
  $(wildcard core/engine/*.cpp) \
  core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
SEQUENCER_TEST_FLAGS := -std=gnu++17 -Wall -Wextra -Itests/menu_stage1_test_stubs -Itests/sequencer_test_stubs -Icore \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Wl,--wrap=_ZN6seance2ui4textEiiPKcji -Wl,--wrap=_ZN6seance8RingUnit7processEPsi -Wl,--wrap=_ZN6seance10StrainUnit7processEPsi -Wl,--wrap=_ZN6seance10ReverbUnit7processEPsi

.PHONY: test-cleanup-ui
test-cleanup-ui:
	python3 tests/test_cleanup_ui.py

.PHONY: test-pixel-strokes
test-pixel-strokes:
	python3 tests/test_pixel_strokes.py

.PHONY: test-step-locks
test-step-locks: /tmp/seance_step_locks_test
	/tmp/seance_step_locks_test

/tmp/seance_step_locks_test: tests/step_locks_test.cpp $(SEQUENCER_TEST_SOURCES) $(wildcard platform/3ds/*.h platform/3ds/*.inc core/ui/*.h core/engine/*.h core/engine/*.inc tests/*.h) Makefile
	g++ -O2 $(SEQUENCER_TEST_FLAGS) tests/step_locks_test.cpp $(filter-out tests/sequencer_test.cpp,$(SEQUENCER_TEST_SOURCES)) -o $@

.PHONY: test-sequencer-menu sanitize-sequencer-menu
test-sequencer-menu:
	g++ -O2 $(SEQUENCER_TEST_FLAGS) $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_sequencer_menu_test
	/tmp/seance_sequencer_menu_test --sequencer-only

sanitize-sequencer-menu:
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_sequencer_menu_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_sequencer_menu_san --sequencer-only

# Narrow two-voice chorus check; incremental binaries avoid repeated UI rebuilds.
CHORUS_TEST_HEADERS := $(wildcard core/engine/*.h core/engine/*.inc core/audio/*.h core/synth/*.h core/dsp/*.h core/ui/*.h platform/3ds/*.h tests/*.h tests/sequencer_test_stubs/*.h)
.PHONY: test-chorus test-chorus-dsp
test-chorus: test-chorus-dsp /tmp/seance_chorus_ui_test
	/tmp/seance_chorus_ui_test --module-inspector-only

test-chorus-dsp: /tmp/seance_chorus_dsp_test
	/tmp/seance_chorus_dsp_test

/tmp/seance_chorus_dsp_test: tests/chorus_dsp_test.cpp core/engine/units.cpp $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore tests/chorus_dsp_test.cpp core/engine/units.cpp -Wl,--gc-sections -o $@

/tmp/seance_chorus_ui_test: $(SEQUENCER_TEST_SOURCES) $(CHORUS_TEST_HEADERS) Makefile
	g++ -O2 $(SEQUENCER_TEST_FLAGS) $(SEQUENCER_TEST_SOURCES) -o $@

.PHONY: test-osc test-osc-ui test-osc-core test-osc-input
test-osc: test-osc-core test-osc-clean-core test-osc-input

# Exact main touch dispatch + real editor facade; no direct-controller bypass.
OSC_INPUT_SOURCES := tests/osc_input_dispatch_test.cpp $(filter-out tests/sequencer_test.cpp,$(SEQUENCER_TEST_SOURCES)) \
  platform/3ds/ui_editor.cpp platform/3ds/ui_module_focus.cpp platform/3ds/ui_picker.cpp
/tmp/seance_osc_input_test: $(OSC_INPUT_SOURCES) tests/sequencer_test.cpp tests/extract_osc_input.py \
  platform/3ds/main.cpp $(CHORUS_TEST_HEADERS) Makefile
	python3 tests/extract_osc_input.py
	g++ -O2 $(SEQUENCER_TEST_FLAGS) -DSEANCE_REAL_EDITOR -I/tmp/seance_osc_input_native $(OSC_INPUT_SOURCES) -o $@

test-osc-input: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test

# INST: new shared sample bank/voice core and real controller integration only.
.PHONY: test-inst test-inst-core test-inst-ui sanitize-inst-ui
test-inst: test-inst-core test-inst-ui
test-inst-core:
	python3 tests/test_inst_core.py --sanitize
test-inst-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --inst-only
sanitize-inst-ui:
	python3 tests/extract_osc_input.py
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -DSEANCE_REAL_EDITOR -I/tmp/seance_osc_input_native -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(OSC_INPUT_SOURCES) -o /tmp/seance_inst_ui_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_inst_ui_san --inst-only

# GLITCH: only slice DSP/persistence and the actual dual-screen editor; no media.
GLITCH_CORE_SOURCES := tests/glitch_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
.PHONY: test-glitch test-glitch-core test-glitch-ui sanitize-glitch-core sanitize-glitch-ui
test-glitch: test-glitch-core test-glitch-ui
/tmp/seance_glitch_core_test: $(GLITCH_CORE_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(GLITCH_CORE_SOURCES) -o $@
test-glitch-core: /tmp/seance_glitch_core_test
	/tmp/seance_glitch_core_test
sanitize-glitch-core:
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -Icore -fsanitize=address,undefined -fno-omit-frame-pointer $(GLITCH_CORE_SOURCES) -o /tmp/seance_glitch_core_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_glitch_core_san
test-glitch-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --glitch-only
sanitize-glitch-ui:
	python3 tests/extract_osc_input.py
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -DSEANCE_REAL_EDITOR -I/tmp/seance_osc_input_native -fsanitize=address,undefined -fno-omit-frame-pointer $(OSC_INPUT_SOURCES) -o /tmp/seance_glitch_ui_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_glitch_ui_san --glitch-only

# ARP: event timing/lifecycle and real editor input; no media or broad suite.
ARP_CORE_SOURCES := tests/arp_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
.PHONY: test-arp test-arp-core test-arp-ui sanitize-arp-core sanitize-arp-ui
test-arp: test-arp-core test-arp-ui
/tmp/seance_arp_core_test: $(ARP_CORE_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(ARP_CORE_SOURCES) -o $@
test-arp-core: /tmp/seance_arp_core_test
	/tmp/seance_arp_core_test
sanitize-arp-core:
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -Icore -fsanitize=address,undefined -fno-omit-frame-pointer $(ARP_CORE_SOURCES) -o /tmp/seance_arp_core_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_arp_core_san
test-arp-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --arp-only
sanitize-arp-ui:
	python3 tests/extract_osc_input.py
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -DSEANCE_REAL_EDITOR -I/tmp/seance_osc_input_native -fsanitize=address,undefined -fno-omit-frame-pointer $(OSC_INPUT_SOURCES) -o /tmp/seance_arp_ui_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_arp_ui_san --arp-only

# MOUTH: focused core + actual main/editor checks, never media exports.
MOUTH_CORE_SOURCES := tests/mouth_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
.PHONY: test-mouth test-mouth-core test-mouth-ui sanitize-mouth-core sanitize-mouth-ui
test-mouth: test-mouth-core test-mouth-ui
/tmp/seance_mouth_core_test: $(MOUTH_CORE_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(MOUTH_CORE_SOURCES) -o $@
test-mouth-core: /tmp/seance_mouth_core_test
	/tmp/seance_mouth_core_test
sanitize-mouth-core:
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -Icore -fsanitize=address,undefined -fno-omit-frame-pointer $(MOUTH_CORE_SOURCES) -o /tmp/seance_mouth_core_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_mouth_core_san
test-mouth-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --mouth-only
sanitize-mouth-ui:
	python3 tests/extract_osc_input.py
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -DSEANCE_REAL_EDITOR -I/tmp/seance_osc_input_native -fsanitize=address,undefined -fno-omit-frame-pointer $(OSC_INPUT_SOURCES) -o /tmp/seance_mouth_ui_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_mouth_ui_san --mouth-only

# COMP: focused core + native input/frozen UI only; no exports or full suite.
COMP_CORE_SOURCES := tests/comp_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
.PHONY: test-comp test-comp-core test-comp-ui
test-comp: test-comp-core test-comp-ui
/tmp/seance_comp_core_test: $(COMP_CORE_SOURCES) $(CHORUS_TEST_HEADERS) core/engine/comp_impl.inc
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(COMP_CORE_SOURCES) -o $@
test-comp-core: /tmp/seance_comp_core_test
	/tmp/seance_comp_core_test
test-comp-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --comp-only
/tmp/seance_osc_input_test: tests/comp_ui_test.h core/engine/comp_impl.inc

# FRACT: only the new core and actual main/editor gestures; never a render/full suite.
FRACT_CORE_SOURCES := tests/fract_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
.PHONY: test-fract test-fract-core test-fract-ui
test-fract: test-fract-core test-fract-ui
/tmp/seance_fract_core_test: $(FRACT_CORE_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(FRACT_CORE_SOURCES) -o $@
test-fract-core: /tmp/seance_fract_core_test
	/tmp/seance_fract_core_test
test-fract-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --fract-only

# Global master bounce: focused core plus real UI/IO/main-routing proof.
BOUNCE_CORE_SOURCES := tests/bounce_core_test.cpp core/engine/engine.cpp core/engine/units.cpp core/engine/patch_io.cpp core/engine/workspace_layout.cpp core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
BOUNCE_UI_SOURCES := tests/bounce_ui_test.cpp $(filter-out tests/sequencer_test.cpp,$(SEQUENCER_TEST_SOURCES)) platform/3ds/ui_bounce.cpp
.PHONY: test-bounce test-bounce-core test-bounce-ui sanitize-bounce-core sanitize-bounce-ui
test-bounce: test-bounce-core test-bounce-ui
	python3 tests/test_nav_instant.py

test-bounce-core: /tmp/seance_bounce_core_test
	/tmp/seance_bounce_core_test

/tmp/seance_bounce_core_test: $(BOUNCE_CORE_SOURCES) $(CHORUS_TEST_HEADERS) core/engine/bounce_impl.inc
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(BOUNCE_CORE_SOURCES) -o $@

/tmp/seance_bounce_ui_test: $(BOUNCE_UI_SOURCES) tests/sequencer_test.cpp tests/extract_bounce_main.py platform/3ds/main.cpp core/engine/bounce_impl.inc $(CHORUS_TEST_HEADERS) Makefile
	python3 tests/extract_bounce_main.py
	g++ -O2 $(SEQUENCER_TEST_FLAGS) -I/tmp/seance_bounce_native $(BOUNCE_UI_SOURCES) -o $@

# Current ribbon geometry and controller proof (also runs ASAN/UBSAN/leaks).
test-bounce-ui sanitize-bounce-ui:
	python3 tests/test_bounce_ribbon.py

sanitize-bounce-core:
	g++ -O1 -g -std=gnu++17 -Icore -fsanitize=address,undefined -fno-omit-frame-pointer $(BOUNCE_CORE_SOURCES) -o /tmp/seance_bounce_core_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_bounce_core_san

# Requires a current passing proof; no rebuild/tests, preserve each new capture.
.PHONY: test-bird test-bird-core test-bird-ui sanitize-bird-core
test-bird: test-bird-core test-bird-ui

test-bird-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --bird-only
	/tmp/seance_osc_input_test --module-inspector-only
	python3 tests/test_nav_instant.py

BIRD_CORE_SOURCES := tests/bird_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
/tmp/seance_bird_core_test: $(BIRD_CORE_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(BIRD_CORE_SOURCES) -o $@

test-bird-core: /tmp/seance_bird_core_test
	/tmp/seance_bird_core_test

sanitize-bird-core:
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -Icore -fsanitize=address,undefined -fno-omit-frame-pointer $(BIRD_CORE_SOURCES) -o /tmp/seance_bird_core_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_bird_core_san

.PHONY: test-shift test-shift-core test-shift-ui sanitize-shift-core
test-shift: test-shift-core test-shift-ui

test-shift-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --shift-only
	/tmp/seance_osc_input_test --module-inspector-only
	python3 tests/test_nav_instant.py

SHIFT_CORE_SOURCES := tests/shift_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
/tmp/seance_shift_core_test: $(SHIFT_CORE_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(SHIFT_CORE_SOURCES) -o $@

test-shift-core: /tmp/seance_shift_core_test
	/tmp/seance_shift_core_test

sanitize-shift-core:
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -Icore -fsanitize=address,undefined -fno-omit-frame-pointer $(SHIFT_CORE_SOURCES) -o /tmp/seance_shift_core_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_shift_core_san

# One narrow sanitizer build: REC retarget consent, shared MOD and affected rails.
.PHONY: test-module-audit
test-module-audit:
	python3 tests/extract_osc_input.py
	g++ -O0 -g1 $(SEQUENCER_TEST_FLAGS) -DSEANCE_REAL_EDITOR -I/tmp/seance_osc_input_native \
	  -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(OSC_INPUT_SOURCES) -o /tmp/seance_module_audit_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	  /tmp/seance_module_audit_san --module-audit-only

# Shared held-L modulation: reuse the integration binary, no DSP/full-suite build.
.PHONY: test-legacy-cleanup
test-legacy-cleanup: /tmp/seance_osc_input_test test-patch-ui
	/tmp/seance_osc_input_test --legacy-cleanup-only
	/tmp/seance_osc_input_test --mod-shortcut-only
	/tmp/seance_osc_input_test --module-inspector-only
	python3 tests/test_nav_instant.py

.PHONY: test-mod-shortcut
test-mod-shortcut: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --mod-shortcut-only
	/tmp/seance_osc_input_test --module-inspector-only

test-osc-core: /tmp/seance_osc_morph_core_test
	/tmp/seance_osc_morph_core_test

OSC_CORE_SOURCES := tests/osc_morph_core_test.cpp core/engine/engine.cpp core/engine/units.cpp core/engine/patch_io.cpp core/engine/workspace_layout.cpp core/synth/sampler.cpp core/synth/wav_loader.cpp
/tmp/seance_osc_morph_core_test: $(OSC_CORE_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(OSC_CORE_SOURCES) -o $@

# OSC v2 is opt-in; the frozen legacy/morph PCM suite stays a separate target.
OSC_CLEAN_SOURCES := tests/osc_clean_core_test.cpp $(filter-out tests/comp_core_test.cpp,$(COMP_CORE_SOURCES))
.PHONY: test-osc-clean-core sanitize-osc-clean-core test-osc-clean-ui
/tmp/seance_osc_clean_core_test: $(OSC_CLEAN_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(OSC_CLEAN_SOURCES) -o $@
test-osc-clean-core: /tmp/seance_osc_clean_core_test
	/tmp/seance_osc_clean_core_test
sanitize-osc-clean-core:
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Icore $(OSC_CLEAN_SOURCES) -o /tmp/seance_osc_clean_core_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_osc_clean_core_san
test-osc-clean-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --osc-clean-only

test-osc-ui: test-osc-input

.PHONY: test-gran test-gran-ui test-gran-core
test-gran: test-gran-core test-gran-clean-core test-gran-clean-ui

test-gran-core: /tmp/seance_gran_visual_core_test
	/tmp/seance_gran_visual_core_test

/tmp/seance_gran_visual_core_test: tests/gran_visual_core_test.cpp core/engine/units.cpp $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore tests/gran_visual_core_test.cpp core/engine/units.cpp -Wl,--gc-sections -o $@

# GRAN v2 opt-in DSP + actual main touch integration, no media exports.
GRAN_CLEAN_SOURCES := tests/gran_clean_core_test.cpp $(filter-out tests/comp_core_test.cpp,$(COMP_CORE_SOURCES))
.PHONY: test-gran-clean-core sanitize-gran-clean-core test-gran-clean-ui
/tmp/seance_gran_clean_core_test: $(GRAN_CLEAN_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(GRAN_CLEAN_SOURCES) -o $@
test-gran-clean-core: /tmp/seance_gran_clean_core_test
	/tmp/seance_gran_clean_core_test
sanitize-gran-clean-core:
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Icore $(GRAN_CLEAN_SOURCES) -o /tmp/seance_gran_clean_core_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_gran_clean_core_san
test-gran-clean-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --gran-clean-only

test-gran-ui: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --gran-only

.PHONY: test-play test-play-ui sanitize-play-ui test-play-core test-play-editor-core test-play-export
test-play: test-play-core test-play-ui test-play-editor-core test-play-export

test-play-editor-core:
	python3 tests/test_play_editor_core.py

test-play-export:
	python3 tests/test_play_export.py

test-play-core: /tmp/seance_play_core_test
	/tmp/seance_play_core_test

/tmp/seance_play_core_test: tests/play_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp $(wildcard core/engine/*.h core/audio/*.h core/synth/*.h core/dsp/*.h)
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore tests/play_core_test.cpp $(wildcard core/engine/*.cpp) core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp -o $@

# Current mode-rail/controller proof; old full-width UI fixtures remain historical.
test-play-ui sanitize-play-ui:
	python3 tests/test_play_modes_ui.py

# Requires a successful current focused proof; no implicit tests or compilation.
.PHONY: test-transition
test-transition: /tmp/seance_chorus_ui_test
	/tmp/seance_chorus_ui_test --transition-only

.PHONY: test-sequence-banks
test-sequence-banks: /tmp/seance_chorus_ui_test
	/tmp/seance_chorus_ui_test --sequence-banks-only

# Delay: incremental UI plus focused sound/compatibility oracles, never full suite.
.PHONY: test-delay test-delay-dsp test-delay-persistence sanitize-delay test-delay-ui
test-delay: test-delay-dsp test-delay-persistence test-delay-ui

test-delay-dsp: /tmp/seance_delay_dsp_test
	/tmp/seance_delay_dsp_test

/tmp/seance_delay_dsp_test: tests/delay_dsp_test.cpp core/engine/units.cpp $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore tests/delay_dsp_test.cpp core/engine/units.cpp -Wl,--gc-sections -o $@

DELAY_PERSIST_SOURCES := tests/delay_persistence_test.cpp core/engine/engine.cpp core/engine/units.cpp core/engine/patch_io.cpp core/engine/workspace_layout.cpp core/synth/sampler.cpp core/synth/wav_loader.cpp
test-delay-persistence: /tmp/seance_delay_persistence_test
	/tmp/seance_delay_persistence_test

/tmp/seance_delay_persistence_test: $(DELAY_PERSIST_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore $(DELAY_PERSIST_SOURCES) -Wl,--gc-sections -o $@

sanitize-delay:
	g++ -O1 -g -std=gnu++17 -Icore -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-omit-frame-pointer tests/delay_dsp_test.cpp core/engine/units.cpp -Wl,--gc-sections -o /tmp/seance_delay_dsp_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_delay_dsp_san

test-delay-ui: /tmp/seance_chorus_ui_test
	/tmp/seance_chorus_ui_test --delay-only

.PHONY: test-performance
test-performance: /tmp/seance_chorus_ui_test
	/tmp/seance_chorus_ui_test --performance-only

.PHONY: test-motor test-motor-core sanitize-motor-core test-motor-ui
# Current extended MOTOR. Historical September UI fixtures/receipts stay frozen.
test-motor: test-motor-core test-motor-ui
test-motor-core:
	python3 tests/test_motor_extended_core.py
sanitize-motor-core:
	python3 tests/test_motor_extended_core.py --sanitized
test-motor-ui:
	python3 tests/test_motor_extended_ui.py
.PHONY: test-noise test-noise-core sanitize-noise-core test-noise-ui
# Current extended NOISE; historical eight-control fixtures stay frozen.
test-noise: test-noise-core test-noise-ui
test-noise-core:
	python3 tests/test_noise_extended_core.py
sanitize-noise-core:
	python3 tests/test_noise_extended_core.py --sanitized
test-noise-ui:
	python3 tests/test_noise_extended_ui.py
.PHONY: test-rec-ui
test-rec-ui: /tmp/seance_chorus_ui_test
	/tmp/seance_chorus_ui_test --rec-only
	g++ -std=gnu++17 -Wall -Wextra -Itests/sequencer_test_stubs -Icore -fsyntax-only platform/3ds/ui_editor.cpp

.PHONY: test-mic test-mic-ui test-mic-core
test-mic: test-mic-core test-mic-ui

MIC_CORE_SOURCES := tests/mic_core_test.cpp core/engine/engine.cpp core/engine/units.cpp core/engine/patch_io.cpp core/engine/workspace_layout.cpp core/synth/sampler.cpp core/synth/wav_loader.cpp
/tmp/seance_mic_core_test: $(MIC_CORE_SOURCES) $(wildcard core/engine/*.h core/audio/*.h core/synth/*.h core/dsp/*.h)
	g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore $(MIC_CORE_SOURCES) -Wl,--gc-sections -o $@

test-mic-core: /tmp/seance_mic_core_test
	/tmp/seance_mic_core_test

test-mic-ui: /tmp/seance_chorus_ui_test
	/tmp/seance_chorus_ui_test --mic-only
	g++ -std=gnu++17 -Wall -Wextra -Itests/sequencer_test_stubs -Icore -fsyntax-only platform/3ds/ui_editor.cpp

.PHONY: test-strain test-strain-core sanitize-strain-core test-strain-ui sanitize-strain-ui
# Current extended Strain. Original six-control tests/receipts remain historical.
test-strain: test-strain-core test-strain-ui
test-strain-core:
	python3 tests/test_strain_extended_core.py
sanitize-strain-core:
	python3 tests/test_strain_extended_core.py --sanitized
test-strain-ui:
	python3 tests/test_strain_extended_ui.py
sanitize-strain-ui:
	python3 tests/test_strain_extended_ui.py --mode sanitized
.PHONY: test-ring test-ring-core sanitize-ring-core test-ring-ui sanitize-ring-ui
# Current extended Ring; original fixtures and receipts remain historical.
test-ring: test-ring-core test-ring-ui
test-ring-core:
	python3 tests/test_ring_extended_core.py
sanitize-ring-core:
	python3 tests/test_ring_extended_core.py --sanitized
test-ring-ui:
	python3 tests/test_ring_extended_ui.py
sanitize-ring-ui:
	python3 tests/test_ring_extended_ui.py --mode sanitized
.PHONY: test-tilt test-tilt-core test-tilt-ui test-tilt-dsp test-tilt-persistence sanitize-tilt sanitize-tilt-core sanitize-tilt-ui
# Current reciprocal revision; historical Clear/legacy fixtures remain retained.
test-tilt: test-tilt-core test-tilt-ui
test-tilt-core:
	python3 tests/test_tilt_extended_core.py
test-tilt-dsp test-tilt-persistence: test-tilt-core
test-tilt-ui:
	python3 tests/test_tilt_extended_ui.py
sanitize-tilt: sanitize-tilt-core sanitize-tilt-ui
sanitize-tilt-core:
	python3 tests/test_tilt_extended_core.py --sanitized
sanitize-tilt-ui:
	python3 tests/test_tilt_extended_ui.py --mode sanitized
.PHONY: test-bit test-bit-core sanitize-bit-core test-bit-ui sanitize-bit-ui
# Current extended BIT; original7-param fixtures/receipts remain historical.
test-bit: test-bit-core test-bit-ui
test-bit-core:
	python3 tests/test_bit_extended_core.py
sanitize-bit-core:
	python3 tests/test_bit_extended_core.py --sanitized
test-bit-ui:
	python3 tests/test_bit_extended_ui.py
sanitize-bit-ui:
	python3 tests/test_bit_extended_ui.py --mode sanitized
# Versioned Fold: frozen legacy PCM, extended DSP/storage and actual native UI.
.PHONY: test-fold test-fold-core sanitize-fold-core test-fold-ui sanitize-fold-ui
test-fold: test-fold-core test-fold-ui
test-fold-core:
	python3 tests/test_fold_extended_core.py
sanitize-fold-core:
	python3 tests/test_fold_extended_core.py --sanitized
test-fold-ui:
	python3 tests/test_fold_extended_ui.py
sanitize-fold-ui:
	python3 tests/test_fold_extended_ui.py --mode sanitized
# Approved six-module interaction fixes, current real-main host proof only.
.PHONY: test-module-usability sanitize-module-usability
test-module-usability sanitize-module-usability:
	python3 tests/test_module_usability_ui.py

# Filter: current real-main log swipe + frozen dual-screen response, isolated proof.
.PHONY: test-filter-ui sanitize-filter-ui
test-filter-ui sanitize-filter-ui:
	python3 tests/test_filter_interaction_ui.py

# Extended VCA plus the retained independent old AD/AHD/storage oracles.
VCA_PERSIST_SOURCES := tests/vca_persistence_test.cpp core/engine/engine.cpp core/engine/units.cpp core/engine/patch_io.cpp core/engine/workspace_layout.cpp core/synth/sampler.cpp core/synth/wav_loader.cpp
.PHONY: test-vca test-vca-core test-vca-ui test-vca-dsp test-vca-persistence sanitize-vca
test-vca: test-vca-dsp test-vca-persistence test-vca-core test-vca-ui

test-vca-core:
	python3 tests/test_vca_extended_core.py

test-vca-ui:
	python3 tests/test_vca_extended_ui.py

test-vca-dsp: /tmp/seance_vca_dsp_test
	/tmp/seance_vca_dsp_test

/tmp/seance_vca_dsp_test: tests/vca_dsp_test.cpp core/engine/units.cpp $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore tests/vca_dsp_test.cpp core/engine/units.cpp -Wl,--gc-sections -o $@

test-vca-persistence: /tmp/seance_vca_persistence_test
	/tmp/seance_vca_persistence_test

/tmp/seance_vca_persistence_test: $(VCA_PERSIST_SOURCES) $(CHORUS_TEST_HEADERS)
	g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore $(VCA_PERSIST_SOURCES) -Wl,--gc-sections -o $@

sanitize-vca:
	python3 tests/test_vca_extended_core.py --sanitized
	python3 tests/test_vca_extended_ui.py --sanitized

# First shared upper workflow: real VCA/CHORUS lower controllers and frozen inspector.
.PHONY: test-module-inspector sanitize-module-inspector
test-module-inspector:
	g++ -O2 $(SEQUENCER_TEST_FLAGS) $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_module_inspector_test
	/tmp/seance_module_inspector_test --module-inspector-only

sanitize-module-inspector:
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_module_inspector_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_module_inspector_san --module-inspector-only

# Shared sample cuts editor: real controller/PLAY bridge, adaptive PCM and frozen dual frames.
.PHONY: test-sequencer-cuts sanitize-sequencer-cuts
test-sequencer-cuts:
	g++ -O2 $(SEQUENCER_TEST_FLAGS) $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_sequencer_cuts_test
	/tmp/seance_sequencer_cuts_test --cuts-only

sanitize-sequencer-cuts:
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_sequencer_cuts_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_sequencer_cuts_san --cuts-only

# Eighth sequencer mode: real SLCE controller, bounded PCM capture and two screens.
.PHONY: test-sequencer-slices sanitize-sequencer-slices
test-sequencer-slices:
	g++ -O2 $(SEQUENCER_TEST_FLAGS) $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_sequencer_slices_test
	/tmp/seance_sequencer_slices_test --slice-only

sanitize-sequencer-slices:
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_sequencer_slices_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_sequencer_slices_san --slice-only

test: test-module-frame-contract test-pixel-strokes test-start-menu test-nav-clock test-clock-mod test-clock-mod-ui test-chorus-dsp
	g++ -O2 -std=gnu++17 -I core \
	  tests/host_test.cpp core/engine/*.cpp \
	  core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp \
	  -o /tmp/seance_host && /tmp/seance_host
	g++ -O2 $(SEQUENCER_TEST_FLAGS) $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_sequencer_test
	/tmp/seance_sequencer_test

# Focused first menu stage: real nav/picker/chains/workspace, not copied UI.
MENU_STAGE1_TOP := platform/3ds/ui_chains_top.cpp
MENU_STAGE1_SOURCES := tests/menu_stage1_test.cpp platform/3ds/ui_nav.cpp \
  platform/3ds/ui_picker.cpp platform/3ds/ui_chains.cpp platform/3ds/ui_workspace.cpp platform/3ds/ui_transition.cpp \
  platform/3ds/menu_style.cpp platform/3ds/project_names.cpp $(MENU_STAGE1_TOP) platform/3ds/ui_theme.cpp \
  core/ui/draw.cpp core/ui/font.cpp $(wildcard core/engine/*.cpp) \
  core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
MENU_STAGE1_FLAGS := -std=gnu++17 -Wall -Wextra -Itests/menu_stage1_test_stubs \
  -Itests/sequencer_test_stubs -Icore -ffunction-sections -fdata-sections \
  -Wl,--gc-sections -Wl,--wrap=_ZN6seance2ui4textEiiPKcji
# Narrow visual polish: real small controls + workspace mute, no full-suite/render.
.PHONY: test-visual-polish
test-visual-polish: /tmp/seance_osc_input_test /tmp/seance_mute_visual_test
	/tmp/seance_osc_input_test --small-buttons-only
	/tmp/seance_mute_visual_test --mute-visual-only
	python3 tests/test_theme_preferences.py

/tmp/seance_mute_visual_test: $(MENU_STAGE1_SOURCES) tests/theme_test_mode.h tests/theme_nav_test.h Makefile $(wildcard platform/3ds/*.h core/engine/*.h core/engine/*.inc core/dsp/*.h core/ui/*.h)
	g++ -O2 $(MENU_STAGE1_FLAGS) $(MENU_STAGE1_SOURCES) -o $@

# Sample browser: actual library/controller + dual-screen frozen frame in existing raster.
.PHONY: test-sample-browser
test-sample-browser: /tmp/seance_sample_browser_test
	python3 tools/generate_menu_labels.py --check
	/tmp/seance_sample_browser_test --sample-browser-only

/tmp/seance_sample_browser_test: $(MENU_STAGE1_SOURCES) tests/sample_browser_test.h \
  platform/3ds/ui_sample_library.cpp Makefile $(wildcard platform/3ds/*.h core/engine/*.h core/synth/*.h core/ui/*.h)
	g++ -O2 $(MENU_STAGE1_FLAGS) -DSEANCE_SAMPLE_BROWSER_TEST $(MENU_STAGE1_SOURCES) -o $@

# Small patch browser restyle: exact main.cpp functions, existing raster harness.
.PHONY: test-patch-ui
test-patch-ui: /tmp/seance_patch_menu_test
	python3 tools/generate_menu_labels.py --check
	/tmp/seance_patch_menu_test --patch-menu-only

/tmp/seance_patch_menu_test: $(MENU_STAGE1_SOURCES) tests/patch_menu_test.h tests/patch_menu_runtime.py \
  platform/3ds/main.cpp platform/3ds/project_library_ui.inc Makefile $(wildcard platform/3ds/*.h core/engine/*.h core/ui/*.h) \
  $(wildcard tests/menu_stage1_test_stubs/*.h tests/sequencer_test_stubs/*.h)
	python3 tests/patch_menu_runtime.py --extract
	g++ -O2 $(MENU_STAGE1_FLAGS) -DSEANCE_PATCH_MENU_TEST -I/tmp/seance_patch_menu_native \
	  $(MENU_STAGE1_SOURCES) -o $@

# Compatibility target: dual-theme proof supersedes fixed-graphite-only captures.
.PHONY: test-graphite-ui
test-graphite-ui: test-theme-ui

.PHONY: test-theme-constructor
test-theme-constructor: test-start-menu
	python3 tests/test_theme_preferences.py
	python3 tests/test_theme_transition.py
	g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
	  tests/start_menu_store_test.cpp platform/3ds/start_menu_store.cpp -o /tmp/seance_theme_store_test
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_theme_store_test
	python3 tests/test_nav_instant.py

.PHONY: test-theme-ui
test-theme-ui:
	python3 tests/test_theme_ui.py

# Shared focused build recipes; separate theme-switch artifacts preserve graphite evidence.
/tmp/seance_graphite_ui_test /tmp/seance_theme_ui_test: $(OSC_INPUT_SOURCES) tests/theme_test_mode.h tests/sequencer_test.cpp tests/extract_osc_input.py \
  platform/3ds/ui_bounce.cpp platform/3ds/ui_nav.cpp platform/3ds/main.cpp $(CHORUS_TEST_HEADERS) Makefile
	python3 tests/extract_osc_input.py
	g++ -O2 $(SEQUENCER_TEST_FLAGS) -DSEANCE_REAL_EDITOR -DSEANCE_GRAPHITE_UI \
	  -I/tmp/seance_osc_input_native $(OSC_INPUT_SOURCES) platform/3ds/ui_bounce.cpp -o $@

/tmp/seance_graphite_menu_test /tmp/seance_theme_menu_test: $(MENU_STAGE1_SOURCES) tests/theme_test_mode.h tests/theme_nav_test.h tests/patch_menu_test.h tests/patch_menu_runtime.py tests/sample_browser_test.h \
  platform/3ds/ui_sample_library.cpp platform/3ds/main.cpp platform/3ds/project_library_ui.inc Makefile $(wildcard platform/3ds/*.h core/engine/*.h core/ui/*.h) \
  $(wildcard tests/menu_stage1_test_stubs/*.h tests/sequencer_test_stubs/*.h)
	python3 tests/patch_menu_runtime.py --extract
	g++ -O2 $(MENU_STAGE1_FLAGS) -DSEANCE_PATCH_MENU_TEST -DSEANCE_SAMPLE_BROWSER_TEST \
	  -I/tmp/seance_patch_menu_native $(MENU_STAGE1_SOURCES) -o $@

# Current v9-v11 main-screen acceptance; no media export or full-suite run.
.PHONY: test-main-columns
test-main-columns:
	python3 tests/test_main_columns.py

.PHONY: test-menu-stage1 sanitize-menu-stage1
test-menu-stage1:
	python3 tools/generate_menu_labels.py --check
	g++ -O2 $(MENU_STAGE1_FLAGS) $(MENU_STAGE1_SOURCES) -o /tmp/seance_menu_stage1_test
	/tmp/seance_menu_stage1_test

sanitize-menu-stage1:
	g++ -O1 -g $(MENU_STAGE1_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(MENU_STAGE1_SOURCES) -o /tmp/seance_menu_stage1_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	  /tmp/seance_menu_stage1_san

# Direct GO TO manual clock: real nav controller, engine and frozen native raster.
.PHONY: test-nav-clock sanitize-nav-clock
test-nav-clock:
	g++ -O2 $(MENU_STAGE1_FLAGS) $(MENU_STAGE1_SOURCES) -o /tmp/seance_nav_clock_test
	/tmp/seance_nav_clock_test --nav-clock-only

sanitize-nav-clock:
	g++ -O1 -g $(MENU_STAGE1_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(MENU_STAGE1_SOURCES) -o /tmp/seance_nav_clock_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	  /tmp/seance_nav_clock_san --nav-clock-only

# Global Sigil project clock: real engine and independent UI/persistence suites.
CLOCK_MOD_SOURCES := tests/clock_mod_test.cpp $(wildcard core/engine/*.cpp) \
  core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
.PHONY: test-clock-mod sanitize-clock-mod test-clock-mod-ui sanitize-clock-mod-ui
test-clock-mod:
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(CLOCK_MOD_SOURCES) -o /tmp/seance_clock_mod_test
	/tmp/seance_clock_mod_test
sanitize-clock-mod:
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -Icore -fsanitize=address,undefined -fno-omit-frame-pointer $(CLOCK_MOD_SOURCES) -o /tmp/seance_clock_mod_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_clock_mod_san
test-clock-mod-ui:
	g++ -O2 $(MENU_STAGE1_FLAGS) $(MENU_STAGE1_SOURCES) -o /tmp/seance_clock_mod_ui_test
	/tmp/seance_clock_mod_ui_test --clock-mod-only
sanitize-clock-mod-ui:
	g++ -O1 -g $(MENU_STAGE1_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(MENU_STAGE1_SOURCES) -o /tmp/seance_clock_mod_ui_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_clock_mod_ui_san --clock-mod-only

# Focused startup menu UI, controller and sidecar. No audio/DSP regression compile.
START_MENU_UI_SOURCES := tests/start_menu_cabinet_test.cpp platform/3ds/ui_start_menu.cpp \
  platform/3ds/ui_theme.cpp platform/3ds/menu_style.cpp core/ui/draw.cpp core/ui/font.cpp
START_MENU_FLAGS := -std=c++17 -Wall -Wextra -Werror -Itests/sequencer_test_stubs \
  -Wl,--wrap=_ZN6seance2ui4textEiiPKcji
START_MENU_STORE_SOURCES := tests/start_menu_store_test.cpp platform/3ds/start_menu_store.cpp
.PHONY: test-start-menu sanitize-start-menu
test-start-menu:
	python3 tools/generate_start_menu_labels.py --check
	python3 tools/generate_cabinet_font.py --check
	g++ -O2 $(START_MENU_FLAGS) $(START_MENU_UI_SOURCES) -o /tmp/seance_start_menu_ui
	/tmp/seance_start_menu_ui
	SEANCE_TEST_THEME=pink /tmp/seance_start_menu_ui
	g++ -O2 -std=c++17 -Wall -Wextra -Werror $(START_MENU_STORE_SOURCES) -o /tmp/seance_start_menu_store
	/tmp/seance_start_menu_store

sanitize-start-menu:
	g++ -O1 -g $(START_MENU_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(START_MENU_UI_SOURCES) -o /tmp/seance_start_menu_ui_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_start_menu_ui_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 SEANCE_TEST_THEME=pink /tmp/seance_start_menu_ui_san
	g++ -O1 -g -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(START_MENU_STORE_SOURCES) -o /tmp/seance_start_menu_store_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_start_menu_store_san

# Focused Ghost iteration; avoid unrelated engine/UI/branding suites.
GHOST_TEST_SOURCES := tests/ghost_tail_test.cpp $(wildcard core/engine/*.cpp) \
  core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp
.PHONY: test-ghost sanitize-ghost
test-ghost:
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(GHOST_TEST_SOURCES) -o /tmp/seance_ghost_tail
	/tmp/seance_ghost_tail
	python3 tests/test_cleanup_ui.py --group ghost

sanitize-ghost:
	g++ -O1 -g -std=gnu++17 -Icore -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(GHOST_TEST_SOURCES) -o /tmp/seance_ghost_tail_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_ghost_tail_san

# Current ghost memory-traces controller, real main/editor, ASan/UBSan.
.PHONY: test-ghost-menu sanitize-ghost-menu
test-ghost-menu sanitize-ghost-menu:
	python3 tests/test_cleanup_ui.py --group ghost
# Universal sigils + sigilbay; the old four-tab fixture lives in the LFO archive.
.PHONY: test-sigils-menu sanitize-sigils-menu test-universal-sigils test-universal-sigils-core test-universal-sigils-ui
test-universal-sigils: test-universal-sigils-core test-universal-sigils-ui
test-universal-sigils-core:
	python3 tests/test_patchbay_core.py
test-universal-sigils-ui test-sigils-menu sanitize-sigils-menu:
	python3 tests/test_patchbay_ui.py

# Focused FM editor; reuse the exact main/editor integration binary.
.PHONY: test-fm-extended test-fm-core sanitize-fm-core test-fm-menu sanitize-fm-menu
test-fm-extended: test-fm-core test-fm-menu

FM_CORE_SOURCES := tests/fm_extended_core_test.cpp core/engine/engine.cpp core/engine/units.cpp core/engine/patch_io.cpp core/engine/workspace_layout.cpp core/synth/sampler.cpp core/synth/wav_loader.cpp
/tmp/seance_fm_extended_core_test: $(FM_CORE_SOURCES) $(wildcard core/engine/*.h core/audio/*.h core/synth/*.h core/dsp/*.h) tests/fm_legacy_reference.h
	g++ -O2 -std=gnu++17 -Wall -Wextra -Icore $(FM_CORE_SOURCES) -o $@

test-fm-core: /tmp/seance_fm_extended_core_test
	/tmp/seance_fm_extended_core_test

/tmp/seance_fm_extended_core_sanitize: $(FM_CORE_SOURCES) $(wildcard core/engine/*.h core/audio/*.h core/synth/*.h core/dsp/*.h) tests/fm_legacy_reference.h
	g++ -O1 -g -std=gnu++17 -Wall -Wextra -Icore -fsanitize=address,undefined -fno-omit-frame-pointer $(FM_CORE_SOURCES) -o $@

sanitize-fm-core: /tmp/seance_fm_extended_core_sanitize
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_fm_extended_core_sanitize

test-fm-menu: /tmp/seance_osc_input_test
	/tmp/seance_osc_input_test --fm-input-only
	/tmp/seance_osc_input_test --fm-only

sanitize-fm-menu:
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_fm_menu_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/seance_fm_menu_san --fm-only

# REV: dense+legacy DSP is separate; these targets cover only the real REV UI.
.PHONY: test-rev-visual test-rev-controls test-rev-menu sanitize-rev-menu test-rev-dense-ui
/tmp/seance_rev_dense_ui_san: $(OSC_INPUT_SOURCES) tests/sequencer_test.cpp tests/extract_osc_input.py platform/3ds/main.cpp $(CHORUS_TEST_HEADERS) Makefile
	python3 tests/extract_osc_input.py
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) -DSEANCE_REAL_EDITOR -I/tmp/seance_osc_input_native \
	  -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(OSC_INPUT_SOURCES) -o $@

test-rev-dense-ui:
	python3 tests/test_reverb_dense_ui.py

test-rev-controls test-rev-menu sanitize-rev-menu test-rev-visual: test-rev-dense-ui

# Focused STRN real UI/controllers only; no unrelated engine/branding suites.
.PHONY: test-strn sanitize-strn
test-strn:
	g++ -O2 $(SEQUENCER_TEST_FLAGS) $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_strn_test
	/tmp/seance_strn_test --strn-only

sanitize-strn:
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) \
	  -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_strn_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_strn_san --strn-only

# Pillow-based asset checks; normal 3DS builds do not require Python.
test-branding:
	python3 tools/generate_circle_logo.py --check
	python3 tests/generate_wordmark_reference.py --check
	python3 tools/generate_note_display.py --check
	python3 tests/test_branding.py

sanitize: sanitize-start-menu
	g++ -O1 -g -std=gnu++17 -I core \
	  -fsanitize=address,undefined -fno-omit-frame-pointer \
	  tests/host_test.cpp core/engine/*.cpp \
	  core/synth/sampler.cpp core/synth/wav_loader.cpp core/synth/wav_writer.cpp \
	  -o /tmp/seance_host_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_host_san
	g++ -O1 -g $(SEQUENCER_TEST_FLAGS) \
	  -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(SEQUENCER_TEST_SOURCES) -o /tmp/seance_sequencer_san
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/seance_sequencer_san
