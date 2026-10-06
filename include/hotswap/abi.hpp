/*
** EPITECH PROJECT, 2026
** Hotswap-Core
** File description:
** ABI — the binary contract between the Runtime and a hot-reloaded plugin
*/

/*
** FROZEN CONTRACT. Every developer codes against this file: the Runtime calls
** these symbols, every plugin exports them. Changing a signature or a rule
** below needs the whole team's agreement and a bump of HOTSWAP_ABI_VERSION —
** see docs/abi.md for the full contract and docs/chantiers.md for the process.
**
** Rules that apply to every symbol:
**   - only C types cross the boundary: pointers, integers, bool;
**   - no exception may leave the plugin: catch inside, return an error;
**   - the Runtime never reads the state pointer, it only stores and passes it.
*/

#pragma once

#include <cstddef>
#include <cstdint>

#define HOTSWAP_ABI_VERSION 2

#if defined(__GNUC__) || defined(__clang__)
#define HOTSWAP_EXPORT __attribute__((visibility("default")))
#else
#define HOTSWAP_EXPORT
#endif

extern "C" {

/* Version of this contract. The Runtime rejects a plugin built against
** another one. Implement it as `return HOTSWAP_ABI_VERSION;`. */
HOTSWAP_EXPORT int hotswap_abi_version(void);

/* Identity of the state layout. Two versions returning the same value share
** the same layout: the Runtime then swaps the code and keeps the state as is.
** Must change whenever a field is added, removed, reordered or retyped. */
HOTSWAP_EXPORT uint64_t plugin_state_version(void);

/* Builds a fresh state holding its default values. Returns nullptr on failure.
** The Runtime keeps the pointer and never dereferences it. */
HOTSWAP_EXPORT void *plugin_state_create(void);

/* Destroys a state. Always called on the version that created it — or one with
** the same plugin_state_version — and always before that version is unloaded. */
HOTSWAP_EXPORT void plugin_state_destroy(void *state);

/* Writes a self-describing snapshot of the state (field name + value).
** Returns the number of bytes the snapshot needs. Writes into `out` only when
** `cap` is at least that size, so the Runtime calls it once with cap = 0 to
** learn the size, then again with a buffer that large. */
HOTSWAP_EXPORT size_t plugin_state_save(const void *state, char *out, size_t cap);

/* Reads a snapshot produced by another version into a state freshly returned by
** plugin_state_create, matching fields by name. Fields missing from the
** snapshot keep their default value, unknown ones are ignored.
** Returns false if the snapshot cannot be read: the Runtime then keeps the
** previous version and its state, untouched. */
HOTSWAP_EXPORT bool plugin_state_load(void *state, const char *in, size_t len);

/* The reloaded code itself, called once per iteration of the host loop. */
HOTSWAP_EXPORT void plugin_update(void *state);

}

namespace hotswap {

using AbiVersionFn   = int (*)();
using StateVersionFn = uint64_t (*)();
using StateCreateFn  = void *(*)();
using StateDestroyFn = void (*)(void *);
using StateSaveFn    = size_t (*)(const void *, char *, size_t);
using StateLoadFn    = bool (*)(void *, const char *, size_t);
using UpdateFn       = void (*)(void *);

} // namespace hotswap
