# TFCC bug catalog

Every bug listed on [officialtfc.win/fixes](https://officialtfc.win/fixes) (and its
investigation pages) and in [tfc_fixes_mm v1.2.0](https://github.com/lazydaydreamer12-collab/tfc_fixes_mm/releases/tag/v1.2.0),
where it is fixed, and whether the client needed to change.

**Why most fixes stay server-side.** tf15-client reverse-engineers only the TFC
*client* (`cl_dll`, `pm_shared`). Its `dlls/` folder is the stock Half-Life SDK
server, not `tfc.so`, so there is no rebuildable TFC server. Server bugs are
fixed by metamod plugins on top of Valve's `tfc.so`, which is also the safest
route for AMXX compatibility: every entity, message, cvar and offset that
plugins rely on stays exactly Valve's.

**Rule for client changes.** A TFCC client must behave exactly like the stock
client unless the server opts in. Server opt-in uses a physinfo key published by
`tfcc_mm` (see `server/tfcc_mm`). Stock clients ignore unknown keys, and a TFCC
client on a stock server never sees them. The movement differential test
(`tests/pm`) proves the "no key = bit-identical to stock" part on every commit.

Legend: **S** = fixed server-side by the named plugin (TFCC bundles/recommends it),
**C** = TFCC client change, **—** = nothing to do / not code.

## tfc_fixes_mm 1.2.0

| # | Fix (ini key) | Where | Client change? |
|---|---|---|---|
| 1 | `ghost_nails` nails hanging in the air | S tfc_fixes_mm | No. Server-only entity behaviour. |
| 2 | `flag_in_wall` dropped flag stuck in brush | S tfc_fixes_mm | No. |
| 7 | `door_caltrops` doors cure caltrops | S tfc_fixes_mm | No. |
| 8 | `teleport_death` dying mid-teleport | S tfc_fixes_mm | No. |
| 9 | `teleport_flag` flag rides a teleport | S tfc_fixes_mm | No. |
| — | `teleport_leaver` leaving mid-teleport | S tfc_fixes_mm | No. |
| 11 | `grenade_in_entity` stuck grenade full damage | S tfc_fixes_mm | No. |
| 12 | `nailgren_ceiling` | S tfc_fixes_mm | No. |
| 13 | `mirv_walls` (+1.2.0 "Trigger in clipping list" crash fix) | S tfc_fixes_mm | No. |
| 14 | `grenade_spawn` primed grenade follows you to spawn | S tfc_fixes_mm | No. |
| 15 | `dropitems_grenade` capping deletes primed grenade | S tfc_fixes_mm | No. |
| 16 | `zero_health` "death bug" | S tfc_fixes_mm | No. The plugin clamps the health sent to the client to 1, which fixes the HUD, the death view and dead-player prediction together. A client-only version would need `deadflag` from `tfc.so`'s clientdata, which is unverified, so it is not attempted. |
| 17 | `grenade_jump_fps` hand-grenade jumps at 1000 tick | S tfc_fixes_mm | No. Knockback is server-authoritative; clients never predict it (stock or TFCC). |
| 18 | `flag_toss` "Not enough room to drop items" | S tfc_fixes_mm | No. |
| 19 | `stuck_menus` menu left over from last map | S tfc_fixes_mm **and** C | **Yes (belt and braces).** TFCC closes any old-map menu in `CHudMenu::VidInit` (runs once per map load, before any new-level message), so it also works on servers without the plugin and can never close a menu sent on the new map. Cvar `cl_tfcc_menufix 0` restores stock. |
| — | `push_fps` jump pads/fans at any tick | S tfc_fixes_mm | No. `trigger_push` velocity is server-only in stock TFC too. |
| — | `conc_edge` (off by default) | S tfc_fixes_mm | No, same reason. |
| 3,4,5,6,10 | AMX "Bug Fixes TFC" items | — | Fixed by Valve in the current TFC build (per tfc_fixes_mm). |

`tfc_fixes_mm` replaces the AMXX "Bug Fixes TFC 1.4" plugin. Do not run both
(Seth's pickup config currently loads `BugFixesTFC.amxx`; the TFCC server
profile disables it).

## OTFC (officialtfc.win)

| Fix | Where | Client change? |
|---|---|---|
| Ramp bug: a slope hit twice in one move counts as a corner and zeroes speed (`otfc_mf_rampfix`) | S otfc_movefix 0.3 **and** C | **Yes.** With movefix on, the server no longer zeroes speed but stock client prediction still does, so players get a prediction error (snap back) every time it triggers. TFCC's `PM_FlyMove` ignores planes already in the clip list, gated by physinfo `tcrf=1`, which `tfcc_mm` publishes only while `otfc_mf_enable` and `otfc_mf_rampfix` are on. Verified by `tests/pm` gate 2. **Parity with movefix's exact code still has to be confirmed** against its source (see TESTING.md). |
| Ramp-save chat message (`otfc_mf_announce`) | S otfc_movefix | No. |
| Instant unstick | Planned by OTFC (server) | No (server-side position fix). |
| Corners slide instead of stopping (vote) | Planned by OTFC | Would need the same physinfo-gated mirror as the ramp fix once it exists. |
| Map-edge snags | Per map | — |
| Ramp slides depend on fps | Needs client *and* server change plus community agreement | Not done: it changes movement for everyone, which breaks the "identical unless opted in" rule until OTFC defines it. The physinfo mechanism is ready for it. |
| Stuck on players (overlap) | Engine (ReHLDS) | — |
| Sentry targeting options (`otfc_sg_mode`) | S otfc_sentry 0.1 | No. |
| Netcode settings (`cl_updaterate`, `-pingboost`, `sv_minupdaterate`, `ex_interp`, `fps_max`...) | Config | — |

## Inherited from upstream tf15-client

`b312e02 client: fix blood splatters when healing red team teammates` is already in the base.
