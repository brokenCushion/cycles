# Cycles C interface: local discussion draft

**Local discussion draft, 2026-10-14. Not submitted upstream.** Phase 9's selected
Windows host subset is qualified in commit `be0d1b0f9`. This
document proposes a review scope; it does not promise upstream acceptance or a
permanent ABI maintained independently of the Cycles developers.

## Problem

A host embedding Cycles currently compiles against its C++ classes and needs
source and binary updates when those classes change. GafferCycles is the first
integration target. A small C boundary could let an unchanged host load a
compatible replacement renderer library, while preserving native rendering.

## Proposed boundary

Keep a documented plain-C header, opaque handles, versioned structures and
status returns. Expose device discovery, session-owned scenes, node/socket
reflection and editing, shader graphs, geometry, render control and copied
output callbacks. Provide socket IDs for repeated edits as well as name-based
access. Unknown future socket types must be skipped by hosts.

The wrapper delegates to native Cycles. It does not replace the integrator,
change kernels or define an alternative rendering algorithm. Deep output is an
optional extension and is not a dependency of the core interface.

Sessions create their own native scene on the selected device. There is no
standalone scene transfer between devices. Pausing without editing preserves
samples; editing a paused session blocks while cancelling and waiting for the
current job, then requires reset. A future nonblocking viewport edit protocol
would require a separately reviewed contract.

## Loading and dependencies

The Windows prototype has an OS-only front DLL and a privately named backend.
The front exposes only `ccl_*` C entry points. The backend exposes one private
function-table getter with a version handshake. The front checks already-loaded
same-named dependencies before loading the backend, so a known incompatible host
receives `CCL_ERROR_DEPENDENCY_MISMATCH` instead of a Windows import failure.

Renderer dependencies use the host's version-matched shared set. Microsoft C++
runtime 14.x updates may satisfy the recorded minimum if every imported symbol
is present; that exception does not apply to renderer libraries. Missing version
resources limit the strength of version checks and must remain visible in the
support contract. Linux/macOS loading and equivalent checks remain unqualified.

## Evidence and review scope

The [implementation plan](CYCLES_C_API.md) and
[API README](../../src/capi/README.md) retain phase results and limitations.
Tests cover C compilation, exports, node-registration completeness, ownership,
typed sockets, native scene/render parity, callback lifetime and dependency
rejection. Optional deep tests compare against native outputs at identical
settings, including BVH mode.

The local Gaffer comparison uses the existing C++ adapter and the C adapter
against **the same Cycles 5.3 source and settings**. This avoids treating a
Cycles-version difference as a wrapper error. The native adapter's necessary
5.1-to-5.3 source patch is retained with line ranges and reasons. Loading
two API builds without changing that host is a separate acceptance test, not a
claim that arbitrary future major versions are interchangeable.

### Working local prototype

- [Public C header](../../src/capi/cycles_c.h) and
  [C-only host adapter](../../src/capi/gaffer/Renderer.cpp).
- [Native adaptation inventory](../../src/capi/gaffer/README.md) and
  [reproducible source patch](../../src/capi/gaffer/native-cycles53.patch).
- [Measured qualification results](../../builds/validation/c-api/phase9-final.json).

Three shared 64×48, 16-sample scenes (emissive mesh, transformed meshes and a
point-lit diffuse mesh) match bit-for-bit on CPU in nine channels. CUDA and
OptiX pass the existing calibrated raw/count/bias policy, each with five ordinary
controls and four seed references per scene. The largest lit RGB difference is
2.2351741790771484e-8; alpha, normal, depth and counts are exact. Interactive CPU
output also matches before and after a transform/shader edit.

The same Gaffer/plugin binaries loaded core-only and deep-enabled API libraries
without a rebuild. Capability queries confirm the differing deep configuration;
all four fixtures, including interactive edits, match exactly between builds.
Both pass dependency checks inside Gaffer. Core/deep builds pass 14/14 and 23/23
CTest checks; both export 98 public `ccl_*` functions, and their front DLLs import
only OS libraries. The deep test count includes nine native tests.

For comparison, adapting the existing C++ backend from Cycles 5.1 to 5.3 required
146 added/95 removed lines across 11 source files: concrete light classes,
visibility flags, packed attribute/socket storage, geometry topology and motion
attribute access. Build integration is recorded separately. These changes are
confined to Gaffer; native Cycles was not changed. The C adapter's initial
implementation is real work; **zero host changes** describes the subsequent
qualified library swap, not all future integration work.

This uses Gaffer main `afb285e7f5b7245944f3111321068bcee0ae3888`, locally compiled
Cortex 10.7.3.1 and the approved GafferHQ 11.0.0a8 platform25 package: a
**pre-release dependency set for Gaffer 1.8 development**, not a released Gaffer.
The required Microsoft runtime was deployed only in the disposable host copy.
Timing observations during concurrent work are provisional; these results make
no host performance claim.

### Limits before broader adoption

The host prototype covers static triangle meshes, unindexed vertex normals,
basic built-in surface graphs, constant point lights, one camera and interactive
transform/shader edits. Gaffer-side OSL, volumes, motion, subdivision, object
deletion, full UI integration and a production-quality adapter remain outside
this qualification. Earlier API-level OSL/deep tests do not qualify those host
paths. Linux/macOS loaders, packaging and binary compatibility remain untested.
Two same-API builds are not evidence of arbitrary future-major-version support.

The native port had to compile its other geometry paths, but they were not part
of this render subset. They require independent review and tests before reuse.
No benchmark from a busy machine should be used to set an overhead guarantee.

## Suggested first discussion

Ask the Cycles and GafferCycles maintainers whether this boundary and ownership
model match their needs, which subset they would maintain, and what compatibility
window they would support. Review dependency deployment before promising binary
interchangeability. Agree on a human-maintained test matrix and an experienced
C++ reviewer before proposing upstream integration. Broader host coverage,
multiple simultaneous Cycles versions and cross-platform ABI qualification are
separate work.

Questions for joint review:

1. Which initial operations and host subset should be supported upstream?
2. Who owns ABI versioning, compatibility duration and regression CI?
3. Is the approved version-matched dependency deployment practical for Gaffer
   releases, including rejection of incompatible already-loaded dependencies?
4. Which platform/toolchain matrix is required before a stable ABI promise?
5. Should a nonblocking viewport edit protocol be designed later, separately
   from the current blocking pause/edit/reset contract?

The GafferCycles author's review of the interface is the next external step.
This document is ready for local review only; posting it requires user approval.

No issue, pull request, release or repository has been created for this draft.
