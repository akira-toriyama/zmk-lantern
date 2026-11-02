#!/usr/bin/env bash
#
# The host harness: the module's real sources compiled with clang for the
# Mac over stubbed Zephyr and ZMK (stubs.h, stubinc/; central/cstub for
# src/layer.c), under AddressSanitizer and UndefinedBehaviorSanitizer, each
# program checking what it draws or sends and ending in a summary line
# ("fails 0", "0 failure(s)"). Never run by CI; CLAUDE.md says when to run it.
#
#   ./scripts/harness/run.sh                # every step below but equiv
#   ./scripts/harness/run.sh effects tempo  # the named steps
#   ./scripts/harness/run.sh equiv [REF]    # frames of REF's src (default
#                                           # origin/main) against this tree's
#
# Steps (the programs, both halves where a half matters; the flags are each
# program's Kconfig combination, written out here):
#   effects  harness_effect, _dart, _water, _beams, _heatmap and _star_waves
#            over unity.c: canon's halves' effects and intros
#   colour   harness_colour (the pick's colour) over the bands and the no
#            picture, and harness_next (the effect-next cycle)
#   colours  harness_colours: the conf's colours (CONFIG_LANTERN_COLOUR_0 to _5)
#   intro    harness_intro: the intro's pick, the star waves or the bands the
#            default
#   layer    harness_layer: the layer keys drawn from a state
#   tempo    harness_tempo
#   policy   policy.c over src/on_usb_power.c: the rail rule's and the wake's
#            scenes, with and without the wake and the rail
#   combos   combo.c -fsyntax-only under Kconfig combinations (the stubs drop
#            the handlers' registrations, so key_listener, tick and
#            board_init warn as unused: no failure)
#   central  central/harness_central over src/layer.c: the central over
#            canon's keymap (central/canon_keymap.h, from central/gen_keymap.py)
#            or a synthetic 76-key one, and the half
#   lk76     harness_lk76: src/effects/layer_keys.c on a 76-key keymap
#   equiv    harness_dump over unity.c, REF's src/ and this tree's: the same
#            frames to the byte, for a change that must not move a pixel
#
# Exit 0 when every step passed, 1 when one failed (a compile error, a
# program's non-zero exit, frames that differ), 2 for bad usage. Outputs (the
# programs, their logs and outputs, the CSVs) go to $LANTERN_HARNESS_OUT
# (default ~/.cache/zmk-lantern/harness), never into the repository; nothing
# in them is personal (no keystroke, no palette of the day). sheet.py draws a
# frames CSV, the star waves' say, as a contact sheet PNG:
#   python3 -I scripts/harness/sheet.py <frames.csv> <out.png> [every_ms] [cols]
#
# Environment: LANTERN_HARNESS_OUT, CC (default clang).
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SRC="$REPO/src"
OUT="${LANTERN_HARNESS_OUT:-$HOME/.cache/zmk-lantern/harness}"
CC="${CC:-clang}"
CFLAGS=(-std=gnu11 -O1 -g -Wall -Wextra "-fsanitize=address,undefined" -fno-sanitize-recover=all)
FAILED=()

usage() {
  awk 'NR>1 && /^#/{sub(/^# ?/,""); print; next} NR>1{exit}' "${BASH_SOURCE[0]}"
}

fail() { # step: a line, and the run fails
  echo "FAIL $*"
  FAILED+=("$1")
}

# The effects' and intros' base, canon's halves (no picture, every effect, both
# intros, the star waves the default), and the lines most programs share.
COLOURS_0=(-DCONFIG_LANTERN_COLOUR_0=0 -DCONFIG_LANTERN_COLOUR_1=0 -DCONFIG_LANTERN_COLOUR_2=0
  -DCONFIG_LANTERN_COLOUR_3=0 -DCONFIG_LANTERN_COLOUR_4=0 -DCONFIG_LANTERN_COLOUR_5=0)
LEVELS=(-DCONFIG_LANTERN_PALETTE=1 -DCONFIG_LANTERN_BRIGHTNESS=16 -DCONFIG_LANTERN_PICTURE_BRIGHTNESS=16)
INTROS_BOTH=(-DCONFIG_LANTERN_INTRO=1 -DCONFIG_LANTERN_INTROS=1 -DCONFIG_LANTERN_INTRO_BANDS=1
  -DCONFIG_LANTERN_INTRO_STAR_WAVES=1)
SEVEN=(-DCONFIG_LANTERN_THIN_RING=1 -DCONFIG_LANTERN_DISC=1 -DCONFIG_LANTERN_STAIN=1
  -DCONFIG_LANTERN_CROSS_RING=1 -DCONFIG_LANTERN_BEAMS=1 -DCONFIG_LANTERN_WATER=1 -DCONFIG_LANTERN_DART=1)
ENGINE=(-DCONFIG_LANTERN_HIT=1 -DCONFIG_LANTERN_EFFECT=1 -DCONFIG_LANTERN_EFFECT_DEFAULT_CROSS_RING=1
  -DCONFIG_LANTERN_EFFECTS=1 -DCONFIG_LANTERN_FRAMES=1 -DCONFIG_LANTERN_GEOMETRY=1)
TEMPO=(-DCONFIG_LANTERN_TEMPO=1 -DCONFIG_LANTERN_TEMPO_PICK=1 -DCONFIG_LANTERN_TEMPO_DEFAULT=1)

# build NAME STUBS SOURCE FLAGS...: compile SOURCE over the effects' stubs into
# $OUT/NAME; the log keeps the warnings. False on a compile error.
build() {
  local name=$1 stubs=$2 source=$3
  shift 3
  if ! "$CC" "${CFLAGS[@]}" -I "$HERE/stubinc" -I "$SRC" -include "$HERE/$stubs" "$@" \
      "$HERE/$source" -o "$OUT/$name" 2>"$OUT/$name.log"; then
    fail "$name: compile error: $(grep -m1 error "$OUT/$name.log")"
    return 1
  fi
}

# run NAME ARGS...: run $OUT/NAME, its output in $OUT/NAME.txt; the line is the
# output's last, and a non-zero exit fails the step.
run() {
  local name=$1
  shift
  if "$OUT/$name" "$@" >"$OUT/$name.txt" 2>&1; then
    echo "ok   $name | $(tail -1 "$OUT/$name.txt")"
  else
    fail "$name | $(tail -1 "$OUT/$name.txt")"
  fi
}

step_effects() {
  local fl=(-DCONFIG_LANTERN_KEYS=1 "${COLOURS_0[@]}" -DCONFIG_LANTERN_ON_USB_POWER=1 "${INTROS_BOTH[@]}"
    -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 -DCONFIG_LANTERN_NO_PICTURE=1 "${SEVEN[@]}"
    -DCONFIG_LANTERN_HEATMAP=1 "${ENGINE[@]}" -DCONFIG_LANTERN_EFFECT_COLOUR=0x0 "${LEVELS[@]}")
  local prog half
  for prog in effect dart water beams; do
    for half in LEFT RIGHT; do
      build "h_${prog}_$half" stubs.h "harness_$prog.c" "-DHALF_$half" "${fl[@]}" && run "h_${prog}_$half"
    done
  done
  for half in LEFT RIGHT; do
    build "h_heatmap_$half" stubs_skew.h harness_heatmap.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_LAYER_KEYS=1 && run "h_heatmap_$half"
    build "h_star_waves_$half" stubs.h harness_star_waves.c "-DHALF_$half" "${fl[@]}" &&
      run "h_star_waves_$half" "$OUT/frames_star_waves_$half.csv" 1 0x1234567
  done
}

step_colour() {
  local fl=(-DCONFIG_LANTERN_KEYS=1 "${COLOURS_0[@]}" -DCONFIG_LANTERN_ON_USB_POWER=1 "${INTROS_BOTH[@]}"
    -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 "${SEVEN[@]}" "${ENGINE[@]}" "${LEVELS[@]}")
  local half
  for half in LEFT RIGHT; do
    build "hc_bands_$half" stubs.h harness_colour.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_BANDS=1 -DCONFIG_LANTERN_EFFECT_COLOUR=0x0 && run "hc_bands_$half"
    build "hc_nopic_$half" stubs.h harness_colour.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_NO_PICTURE=1 -DCONFIG_LANTERN_EFFECT_COLOUR=0x0 && run "hc_nopic_$half"
    build "hc_nopic-red_$half" stubs.h harness_colour.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_NO_PICTURE=1 -DCONFIG_LANTERN_EFFECT_COLOUR=0xFF0000 -DEXPECT_DEFAULT_RED=1 &&
      run "hc_nopic-red_$half"
  done
  build h_next stubs.h harness_next.c && run h_next
}

step_colours() {
  local fl=(-DCONFIG_LANTERN_KEYS=1 -DCONFIG_LANTERN_ON_USB_POWER=1 "${INTROS_BOTH[@]}"
    -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 -DCONFIG_LANTERN_BANDS=1 "${SEVEN[@]}"
    -DCONFIG_LANTERN_HEATMAP=1 -DCONFIG_LANTERN_LAYER_KEYS=1 "${TEMPO[@]}" "${ENGINE[@]}"
    -DCONFIG_LANTERN_EFFECT_COLOUR=0x0 "${LEVELS[@]}")
  local rest=(-DCONFIG_LANTERN_COLOUR_2=0 -DCONFIG_LANTERN_COLOUR_3=0 -DCONFIG_LANTERN_COLOUR_4=0
    -DCONFIG_LANTERN_COLOUR_5=0)
  local half
  for half in LEFT RIGHT; do
    build "hc2_conf_$half" stubs_skew.h harness_colours.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_COLOUR_0=0xFF0000 -DCONFIG_LANTERN_COLOUR_1=0x00FF00 -DCONFIG_LANTERN_COLOUR_2=0x0000FF \
      -DCONFIG_LANTERN_COLOUR_3=0xFFFF00 -DCONFIG_LANTERN_COLOUR_4=0xFF00FF -DCONFIG_LANTERN_COLOUR_5=0x00FFFF &&
      run "hc2_conf_$half"
    build "hc2_two_$half" stubs_skew.h harness_colours.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_COLOUR_0=0xFF0000 -DCONFIG_LANTERN_COLOUR_1=0x00FF00 "${rest[@]}" && run "hc2_two_$half"
    build "hc2_zero_$half" stubs_skew.h harness_colours.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_COLOUR_0=0 -DCONFIG_LANTERN_COLOUR_1=0 "${rest[@]}" -DEXPECT_WHITE=1 &&
      run "hc2_zero_$half"
  done
}

step_intro() {
  local fl=(-DCONFIG_LANTERN_KEYS=1 "${COLOURS_0[@]}" -DCONFIG_LANTERN_ON_USB_POWER=1 "${INTROS_BOTH[@]}"
    -DCONFIG_LANTERN_NO_PICTURE=1 "${SEVEN[@]}" "${ENGINE[@]}" -DCONFIG_LANTERN_EFFECT_COLOUR=0x0
    "${LEVELS[@]}")
  local half
  for half in LEFT RIGHT; do
    build "hi_STAR_WAVES_$half" stubs.h harness_intro.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 && run "hi_STAR_WAVES_$half"
    build "hi_BANDS_$half" stubs.h harness_intro.c "-DHALF_$half" "${fl[@]}" \
      -DCONFIG_LANTERN_INTRO_DEFAULT_BANDS=1 -DEXPECT_DEFAULT_BANDS=1 && run "hi_BANDS_$half"
  done
}

step_layer() {
  local fl=(-DCONFIG_LANTERN_KEYS=1 "${COLOURS_0[@]}" -DCONFIG_LANTERN_ON_USB_POWER=1 "${INTROS_BOTH[@]}"
    -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 -DCONFIG_LANTERN_NO_PICTURE=1 "${SEVEN[@]}"
    -DCONFIG_LANTERN_LAYER_KEYS=1 "${ENGINE[@]}" -DCONFIG_LANTERN_EFFECT_COLOUR=0x0 "${LEVELS[@]}")
  local half
  for half in LEFT RIGHT; do
    build "h_layer_$half" stubs.h harness_layer.c "-DHALF_$half" "${fl[@]}" && run "h_layer_$half"
  done
}

step_tempo() {
  local fl=(-DCONFIG_LANTERN_KEYS=1 "${COLOURS_0[@]}" -DCONFIG_LANTERN_ON_USB_POWER=1 "${INTROS_BOTH[@]}"
    -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 -DCONFIG_LANTERN_NO_PICTURE=1 "${SEVEN[@]}"
    -DCONFIG_LANTERN_HEATMAP=1 -DCONFIG_LANTERN_LAYER_KEYS=1 "${TEMPO[@]}" "${ENGINE[@]}"
    -DCONFIG_LANTERN_EFFECT_COLOUR=0x0 "${LEVELS[@]}")
  local half
  for half in LEFT RIGHT; do
    build "h_tempo_$half" stubs_skew.h harness_tempo.c "-DHALF_$half" "${fl[@]}" && run "h_tempo_$half"
  done
}

# policy.c includes the rule's file as policy_src.c, its #include and
# DT_DRV_COMPAT lines dropped; its stubs serve every build, so a build that
# leaves one unused is no failure (-Wno-unused-function).
step_policy() {
  grep -v '^#include\|^#define DT_DRV_COMPAT' "$SRC/on_usb_power.c" >"$OUT/policy_src.c"
  local cf=("${CFLAGS[@]}" -Wno-unused-function -I "$OUT" -DCONFIG_LANTERN_INTROS=1
    -DCONFIG_LANTERN_ON_USB_POWER=1 -DCONFIG_USB_DEVICE_STACK=1 -DCONFIG_SETTINGS=1
    -DCONFIG_ZMK_IDLE_TIMEOUT=300000 -DCONFIG_LANTERN_OFF_WHEN_IDLE=1)
  local name flags
  for name in wake-rail wake-norail nowake-rail nowake-norail; do
    flags=()
    case "$name" in wake-*) flags+=(-DCONFIG_LANTERN_WAKE_ON_HIT=1) ;; esac
    case "$name" in *-rail) flags+=(-DCONFIG_LANTERN_RAIL=1) ;; esac
    if "$CC" "${cf[@]}" "${flags[@]}" "$HERE/policy.c" -o "$OUT/p_$name" 2>"$OUT/p_$name.log"; then
      if "$OUT/p_$name" >"$OUT/p_$name.txt" 2>&1; then
        echo "ok   p_$name | $(tail -1 "$OUT/p_$name.txt" | sed 's/; last log:.*//')"
      else
        fail "p_$name | $(tail -1 "$OUT/p_$name.txt" | sed 's/; last log:.*//')"
      fi
    else
      fail "p_$name: compile error: $(grep -m1 error "$OUT/p_$name.log")"
    fi
  done
}

# combo NAME FLAGS...: combo.c -fsyntax-only for both halves.
combo() {
  local name=$1 half
  shift
  for half in LEFT RIGHT; do
    if "$CC" -std=gnu11 -O1 -Wall -Wextra -fsyntax-only -I "$HERE/stubinc" -I "$SRC" -include "$HERE/stubs.h" \
        "-DHALF_$half" "${CBASE[@]}" "$@" "$HERE/combo.c" >"$OUT/combo_${name}_$half.log" 2>&1; then
      echo "ok   combo $name $half ($(grep -c 'warning:' "$OUT/combo_${name}_$half.log") warnings)"
    else
      fail "combo $name $half: $(grep -m1 error "$OUT/combo_${name}_$half.log")"
    fi
  done
}

step_combos() {
  CBASE=(-DCONFIG_LANTERN_KEYS=1 "${COLOURS_0[@]}" -DCONFIG_LANTERN_ON_USB_POWER=1 "${LEVELS[@]}")
  local eff=(-DCONFIG_LANTERN_EFFECT_COLOUR=0x0 -DCONFIG_LANTERN_EFFECTS=1 -DCONFIG_LANTERN_FRAMES=1
    -DCONFIG_LANTERN_GEOMETRY=1 -DCONFIG_LANTERN_EFFECT=1)
  local bands=(-DCONFIG_LANTERN_INTROS=1 -DCONFIG_LANTERN_INTRO_BANDS=1 -DCONFIG_LANTERN_INTRO_DEFAULT_BANDS=1)
  local star=(-DCONFIG_LANTERN_INTROS=1 -DCONFIG_LANTERN_INTRO_STAR_WAVES=1
    -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 -DCONFIG_LANTERN_FRAMES=1 -DCONFIG_LANTERN_GEOMETRY=1)
  local both=(-DCONFIG_LANTERN_INTROS=1 -DCONFIG_LANTERN_INTRO_BANDS=1 -DCONFIG_LANTERN_INTRO_STAR_WAVES=1
    -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 -DCONFIG_LANTERN_FRAMES=1 -DCONFIG_LANTERN_GEOMETRY=1)
  local every=("${SEVEN[@]}" -DCONFIG_LANTERN_HEATMAP=1 -DCONFIG_LANTERN_HIT=1
    -DCONFIG_LANTERN_EFFECT_DEFAULT_CROSS_RING=1)
  combo bare-noboot -DCONFIG_LANTERN_BANDS=1
  combo bands "${bands[@]}" -DCONFIG_LANTERN_BANDS=1
  combo bands-pick "${bands[@]}" -DCONFIG_LANTERN_INTRO=1 -DCONFIG_LANTERN_BANDS=1
  combo bands-countdown "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 -DCONFIG_LANTERN_COUNTDOWN=1 \
    -DCONFIG_LANTERN_COUNTDOWN_STAGE_MS=10000 -DCONFIG_ZMK_IDLE_TIMEOUT=300000
  combo solid "${bands[@]}" -DCONFIG_LANTERN_SOLID=1
  combo gradient "${bands[@]}" -DCONFIG_LANTERN_GRADIENT=1 -DCONFIG_LANTERN_GEOMETRY=1
  combo scatter "${bands[@]}" -DCONFIG_LANTERN_SCATTER=1 -DCONFIG_LANTERN_SCATTER_MS=10000
  combo nopic "${bands[@]}" -DCONFIG_LANTERN_NO_PICTURE=1
  combo nopic-noboot -DCONFIG_LANTERN_NO_PICTURE=1
  combo star "${star[@]}" -DCONFIG_LANTERN_BANDS=1
  combo both "${both[@]}" -DCONFIG_LANTERN_BANDS=1
  combo both-pick "${both[@]}" -DCONFIG_LANTERN_INTRO=1 -DCONFIG_LANTERN_BANDS=1
  combo nopic-star "${star[@]}" -DCONFIG_LANTERN_NO_PICTURE=1
  combo ring "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_THIN_RING=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_THIN_RING=1
  combo disc "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_DISC=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_DISC=1
  combo stain "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_THIN_RING=1 \
    -DCONFIG_LANTERN_STAIN=1 -DCONFIG_LANTERN_EFFECT_DEFAULT_STAIN=1
  combo cross "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_CROSS_RING=1 \
    -DCONFIG_LANTERN_HIT=1 -DCONFIG_LANTERN_EFFECT_DEFAULT_CROSS_RING=1
  combo beams "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_BEAMS=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_BEAMS=1
  combo water "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_WATER=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_WATER=1
  combo dart "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_DART=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_DART=1
  combo heatmap "${bands[@]}" -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_HEATMAP=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_HEATMAP=1
  combo heatmap-nopic-noboot -DCONFIG_LANTERN_NO_PICTURE=1 "${eff[@]}" -DCONFIG_LANTERN_HEATMAP=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_HEATMAP=1
  combo ring-noboot -DCONFIG_LANTERN_BANDS=1 "${eff[@]}" -DCONFIG_LANTERN_THIN_RING=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_THIN_RING=1
  combo disc-nohit-gradient "${bands[@]}" -DCONFIG_LANTERN_GRADIENT=1 "${eff[@]}" -DCONFIG_LANTERN_DISC=1 \
    -DCONFIG_LANTERN_EFFECT_DEFAULT_DISC=1
  combo all-nonodes -DCONFIG_LANTERN_NO_PICTURE=1 "${both[@]}" -DCONFIG_LANTERN_EFFECTS=1 \
    -DCONFIG_LANTERN_EFFECT_COLOUR=0x0 "${every[@]}"
  # canon's halves: every effect, both intros, the no picture, the tempo
  combo product -DCONFIG_LANTERN_NO_PICTURE=1 "${both[@]}" -DCONFIG_LANTERN_INTRO=1 "${eff[@]}" "${every[@]}" \
    -DCONFIG_LANTERN_LAYER_KEYS=1 -DCONFIG_LANTERN_LAYER=1 "${TEMPO[@]}"
}

step_central() {
  local name flags
  for name in canon canon-keys synth76 synth76-default half synth76-default-word1; do
    case "$name" in
      canon) flags=(-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1) ;;
      canon-keys) flags=(-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1 -DCONFIG_LANTERN_LAYER_KEYS=1) ;;
      synth76) flags=(-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1 -DSYNTH76) ;;
      synth76-default) flags=(-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1 -DSYNTH76 -DLIST_DEFAULT -DCONFIG_LANTERN_LAYER_KEYS=1) ;;
      half) flags=(-DCONFIG_LANTERN_LAYER_KEYS=1) ;;
      synth76-default-word1)
        flags=(-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1 -DSYNTH76 -DLIST_DEFAULT -DCONFIG_LANTERN_LAYER_KEYS=1 -DINIT_WORD=1) ;;
    esac
    if "$CC" "${CFLAGS[@]}" -I "$HERE/central/cstub" -I "$SRC" -include "$HERE/central/cstub_central.h" \
        -DCONFIG_LANTERN_LAYER=1 "${flags[@]}" "$HERE/central/harness_central.c" -o "$OUT/hc_$name" \
        2>"$OUT/hc_$name.log"; then
      run "hc_$name"
    else
      fail "hc_$name: compile error: $(grep -m1 error "$OUT/hc_$name.log")"
    fi
  done
}

step_lk76() {
  build h_lk76 stubs.h harness_lk76.c -DHALF_LEFT -DCONFIG_LANTERN_KEYS=1 -DCONFIG_LANTERN_EFFECTS=1 \
    -DCONFIG_LANTERN_FRAMES=1 -DCONFIG_LANTERN_LAYER_KEYS=1 "${LEVELS[@]}" && run h_lk76
}

# equiv [REF]: REF's src/ (git archive) and this tree's under the same
# program; it holds the old program's signatures for an old tree
# (harness_dump.c).
step_equiv() {
  local ref=${1:-origin/main}
  local base="$OUT/equiv-base"
  rm -rf "$base"
  mkdir -p "$base"
  if ! git -C "$REPO" archive "$ref" src | tar -x -C "$base"; then
    fail "equiv: cannot read $ref's src"
    return
  fi
  local fl=(-DCONFIG_LANTERN_KEYS=1 "${COLOURS_0[@]}" -DCONFIG_LANTERN_ON_USB_POWER=1 "${INTROS_BOTH[@]}"
    -DCONFIG_LANTERN_INTRO_DEFAULT_STAR_WAVES=1 -DCONFIG_LANTERN_NO_PICTURE=1 "${SEVEN[@]}" "${ENGINE[@]}"
    -DCONFIG_LANTERN_EFFECT_COLOUR=0x0 "${LEVELS[@]}")
  local half tree src
  for half in LEFT RIGHT; do
    for tree in base this; do
      src=$SRC
      [ "$tree" = base ] && src=$base/src
      if ! "$CC" "${CFLAGS[@]}" -I "$HERE/stubinc" -I "$src" -include "$HERE/stubs.h" "-DHALF_$half" "${fl[@]}" \
          -DHARNESS_SRC='"unity.c"' "$HERE/harness_dump.c" -o "$OUT/hdump_${tree}_$half" \
          2>"$OUT/hdump_${tree}_$half.log"; then
        fail "equiv $tree $half: compile error: $(grep -m1 error "$OUT/hdump_${tree}_$half.log")"
        continue 2
      fi
      "$OUT/hdump_${tree}_$half" "$OUT/dump_${tree}_$half.csv" 2>"$OUT/dump_${tree}_$half.err" ||
        { fail "equiv $tree $half: run failed: $(tail -1 "$OUT/dump_${tree}_$half.err")"; continue 2; }
    done
    if cmp -s "$OUT/dump_base_$half.csv" "$OUT/dump_this_$half.csv"; then
      echo "ok   equiv $half: $(tail -1 "$OUT/dump_this_$half.err") identical with $ref"
    else
      fail "equiv $half: frames differ from $ref from line $(cmp "$OUT/dump_base_$half.csv" \
        "$OUT/dump_this_$half.csv" | sed -n 's/.* line //p') of dump_this_$half.csv"
    fi
  done
}

ALL=(effects colour colours intro layer tempo policy combos central lk76)
steps=()
while [ $# -gt 0 ]; do
  case "$1" in
    -h | --help) usage; exit 0 ;;
    equiv)
      if [ $# -gt 1 ] && [[ "$2" != -* ]] && ! [[ " ${ALL[*]} equiv " == *" $2 "* ]]; then
        steps+=("equiv:$2")
        shift
      else
        steps+=("equiv:")
      fi
      ;;
    effects | colour | colours | intro | layer | tempo | policy | combos | central | lk76) steps+=("$1") ;;
    *) echo "run.sh: unknown step $1 (see --help)" >&2; exit 2 ;;
  esac
  shift
done
[ ${#steps[@]} -gt 0 ] || steps=("${ALL[@]}")
command -v "$CC" >/dev/null || { echo "run.sh: $CC not found" >&2; exit 2; }
mkdir -p "$OUT"

for s in "${steps[@]}"; do
  echo "== $s"
  case "$s" in
    equiv:*) step_equiv "${s#equiv:}" ;;
    *) "step_$s" ;;
  esac
done

if [ ${#FAILED[@]} -gt 0 ]; then
  echo "harness: ${#FAILED[@]} failed (outputs in $OUT)"
  exit 1
fi
echo "harness: every program passed (outputs in $OUT)"
