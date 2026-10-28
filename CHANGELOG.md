# Changelog

## 2026-10-28 — dlm-module branch: /dev/paula builds as a loadable module for k30 and k46 (module/, amix-dlm kit; needs kabi minor 3 and major c 54; modcheck ok on candidate kits, k30 43a4d1c9, k46 61d6938f). His static relink path is unchanged (paula_040.o 5efb3edd). Two upstream candidates: the chip sample area is cleared (stock AllocMem ignores MEMF_CLEAR for chip), and an opt-in safe-close patch for the stock audio driver (5ab95a0, 48533e4, cb37c9d)
