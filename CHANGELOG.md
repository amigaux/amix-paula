# Changelog

## 2026-10-28 — dlm-module: opt-in patcher port/src/patch_audio_initwait.py bounds the stock audioinit() busy-wait (32 scan lines per channel on VHPOSR, about 2 ms; no CIA timer touched); on timeout /dev/audio's open frees its client and fails EIO, and the next open retries. Applies in any order with the other two patchers; k46 audio_040.o 2be2b185 (safeclose, initwait, paula), TOTAL complaints 0, scan060 clean; module images unchanged (62fdcab)

## 2026-10-28 — module/README: the module form's paulatest shows idle 1–2 (the open's self-test counts one idle tick); /dev/audio under amiberry needs sound_output=interrupts, because with "none" the stock audioinit() spins at spl7 and the box freezes. No code change; images unchanged (a84400b)

## 2026-10-28 — module form: the first open of /dev/paula no longer returns EBUSY. Kickstart 3.0–3.2.x timer.device leaves CIA-A timer B running and no kernel code writes CRB, so paula_mopen's timer-B START refusal failed every first open; removed (paulaopen reprograms the timer, as the static form always has). Images k30 147844b4, k46 d2c7d8f4; static form unchanged (004f9e6)

## 2026-10-28 — dlm-module: the /dev/audio ⇄ /dev/paula open race is closed on both sides (amix-dlm C3-2 v2 §3): patch_audio_paula.py re-tests paula_busy at splaudio after the sleeping allocation (a fifth fingerprint place; the four old ones unchanged), and paulaopen claims busy/paula_busy before its allocation. Static path TOTAL complaints 0; modcheck ok on the real 1.3 kits, k30 818b6028, k46 506bb5ed (7a13a9e, ecf2e1b)

## 2026-10-28 — dlm-module branch: /dev/paula builds as a loadable module for k30 and k46 (module/, amix-dlm kit; needs kabi minor 3 and major c 54; modcheck ok on candidate kits, k30 43a4d1c9, k46 61d6938f). His static relink path is unchanged (paula_040.o 5efb3edd). Two upstream candidates: the chip sample area is cleared (stock AllocMem ignores MEMF_CLEAR for chip), and an opt-in safe-close patch for the stock audio driver (5ab95a0, 48533e4, cb37c9d)
