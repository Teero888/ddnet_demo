/*
 * internal/dd_physics.h - the demo library's 1:1 port of the original DDNet
 * physics, used to reconstruct ticks a 0.6 demo does not contain.
 *
 * INTERNAL: this is not part of the library's API. Only the library's own
 * implementation and its tests include it; everything is file-local (static).
 * Users get the reconstructed, abstracted demo state instead.
 *
 * Everything mirrors DDNet's source function for function, including the
 * float operation order, because reconstruction has to reproduce the server's
 * results bit for bit. When changing anything, compare against DDNet, not
 * against what looks cleaner.
 *
 * Float semantics: all math is single precision, exactly like DDNet's release
 * builds. Do not compile with -ffast-math, and keep floating point contraction
 * (FMA) off; this header turns it off where the compiler allows.
 *
 * Include this header, not the parts:
 *   dd_math.h       base/math.h, base/vmath.h, tuning
 *   dd_collision.h  CCollision
 *   dd_core.h       CCharacterCore, CWorldCore, CTeamsCore, the client's Evolve
 *
 * Requires ddnet_demo.h (net object layouts) and ddnet_map_loader.h.
 */
#ifndef DD_PHYSICS_INTERNAL_H
#define DD_PHYSICS_INTERNAL_H

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
/* Every helper is file-local; a translation unit uses only some of them. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "dd_core.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

#endif /* DD_PHYSICS_INTERNAL_H */
