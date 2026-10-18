# Blender Deep: hair and curves investigation

Status: **M0 and CPU M1 qualified; GPU diagnostics qualified; GPU hair pending**
(2026-10-18). Part C is `b2ead8e39`. Task 17's corrected GPU diagnostics gate
passed on CUDA and OptiX, including both beauty-policy checks, exact non-run
headers, deep byte identity and triangle ledgers. GPU hair admission, compiled
OptiX hair audit and Part E remain pending.
See the [Step 4 report](../deep/BLENDER_DEEP_MULTILAYER_RESULTS.md). The original
investigation/proposal below is retained as historical context.

Investigated repository `643d2ec0d`, branch `blender-deep-multilayer`, and pinned
Blender `749518deb2f0735a22361a07488b35f7ea5c2fdf`.
Only this document is committed. The user decides implementation scope.
References below use repository-relative file names and one-based line numbers;
`blender-src/` means `D:/temp/cycles_deep_multilayer/blender-src/`.

## 1. Root cause and current state

**Removing the geometry preflight alone is unsafe.** Ordinary closest-hit
traversal already supports curves on CPU, CUDA and OptiX, but deep capture rejects
the returned primitive and its duplicate-boundary helper indexes triangle data.
Volume-enabled capture has a second, different traversal that also needs work.
No curve-supporting build or render was attempted in this investigation.

### 1.1 Rejections and assumptions

| Location | Finding and consequence |
| --- | --- |
| `src/session/deep.cpp:477` | Camera-visible geometry set preserves the earlier indirect-only fix. Do not broaden validation to invisible grooms. |
| `src/session/deep.cpp:650` | Procedural geometry is separately rejected. Supporting synced Hair does not automatically support arbitrary procedural generators. |
| `src/session/deep.cpp:673` | Requires static `Geometry::MESH`, rejecting Hair and PointCloud and deforming geometry. |
| `src/session/deep.cpp:675` | Unconditional `static_cast<Mesh *>` for subdivision after that guard must become mesh-specific. |
| `src/session/deep.cpp:688` | Volume-boundary topology, vertex and enclosed-volume checks assume triangle meshes. Hair must never enter this boundary path. |
| `src/session/deep.cpp:446` | Hair BSDF and neighboring Principled Hair entry are already allowed, qualified on meshes only. This is not curve qualification. |
| `src/session/deep.cpp:865` | OSL attribute validation skips non-mesh/non-volume geometry and accesses mesh attributes. Keep curve OSL explicitly unsupported initially; do not silently inherit this omission. |
| `src/kernel/device/cpu/kernel_arch_impl.h:173` | CPU surface traversal rejects anything except `PRIMITIVE_TRIANGLE`. Its private camera-state copy starts at line 155. |
| `src/kernel/deep/surface_cuda.h:36` | Shared CUDA/OptiX surface path has the same primitive rejection, before shading or depth output. |
| `src/kernel/deep/surface_boundary.h:16` | Duplicate-boundary test assumes two triangles; lines 22–23 read `tri_vindex` using primitive indices. Guard both primitive types before calling it. Curve adjacency is not a triangle shared edge. |
| `src/kernel/deep/volume.h:564` | Volume-enabled surface processing also rejects non-triangles; lines 572–575 call the triangle duplicate helper. |
| `src/kernel/deep/volume.h:306`, `:412` | BVH volume-boundary scan and constructed intersection are intentionally triangle-only. Retain this restriction for media boundaries. |
| `src/kernel/bvh/volume_all.h:128` | CUDA all-hit traversal handles triangle/motion-triangle cases, not curves. A guard-only change could miss hair entirely in volume mode. |
| `src/kernel/device/cpu/bvh.h:384` | Embree volume-all callback converts hits and uses `bvh_volume_anyhit_triangle_filter` at line 408. With `volume_only=false` the filter can return before its triangle shader lookup, so a converted hair hit can reach the explicit rejection in volume.h; this is not a qualified curve event stream. |
| `src/kernel/bvh/intersect_filter.h:279` | Volume-only filtering reads `tri_shader` at line 307. Curves must remain excluded from actual medium discovery, separately from surface discovery. |
| `src/kernel/deep/volume_optix.h:6` | Dedicated deep all-hit any-hit program explicitly ignores non-triangle hits. Its SBT offset is distinct from ordinary closest-hit. |
| `src/kernel/device/optix/kernel_deep.cu:18` | Deep raygen invokes the shared surface/volume dispatch; it does not independently implement a curve intersector. |
| `src/deep/capture.cpp:38`, `:50` | Capture validates capacity and allocates records/events, not mesh geometry. Keep the bounds; no new geometric representation is required for surface hair. |
| `src/deep/capture.cpp:591`, `:594`, `:894` | Manifest index validation, conversion of generic surface events, and explicit primitive-error reporting. No strand field exists. |
| `src/deep/reconstruction.cpp:89`, `:112`, `:135` | Reconstruction consumes depth/alpha/object/facing events, not triangles; mixed provenance becomes unknown rather than a strand identity. |
| `src/deep/volume.cpp:20`, `:34` | Depth merging uses same object/facing and relative depth diameter, not topology or strand index. This needs a hair-specific contract, not an automatic claim of strand preservation. |
| `src/deep/exr_writer.cpp:76`, `:481`, `:574` | Surface depth merging/publication is geometry-agnostic; no EXR schema addition is necessary for object-level hair. |
| `src/session/deep.cpp:491–510` | Manifests enumerate synced objects and hash object names; holdouts are object/shader-derived. They are not per strand. |
| `tools/scan_blender_deep_scene.py:357–358` | CURVES/POINTCLOUD and particle HAIR are blocked. Scanner must distinguish qualified shape/motion/backend/mode, not simply delete both checks. |

Volume mode is selected from `params.deep.volume`, not only from whether a visible
volume happened to be hit (`src/session/deep.cpp:521`; CPU dispatch at
`src/kernel/device/cpu/kernel_arch_impl.h:149`; GPU dispatch at
`src/kernel/deep/surface_cuda.h:172`). Junkshop's previously successful smoke uses
scene-wide volume settings. Surface-only hair support cannot be advertised as
sufficient for the unchanged all-layer smoke configuration. Raising the event
limit to 4096 is also not a general surface workaround: preflight permits more
than 64 only for its grid/shader-evaluated-volume mode (`deep.cpp:537–547`).

### 1.2 Traversal by backend

| Backend | Existing surface intersection | Required deep work |
| --- | --- | --- |
| CPU | Embree constructs flat/round Catmull-Rom and round linear curves (`src/bvh/embree.cpp:644–709`). `src/kernel/device/cpu/bvh.h:132–173` maps Embree segment indices into curve primitive plus packed segment/type, preserving t/u/v. | Accept explicitly qualified curve types, make duplicate suppression type-safe, qualify native continuation/shading. Volume mode is a separate milestone. |
| CUDA | BVH2 dispatches curve types at `src/kernel/bvh/traversal.h:181–196`; `src/kernel/geom/curve_intersect.h:798–858` selects ribbon, thick spline or thick linear intersection. | Same shared surface changes; no new fundamental intersector appears necessary. Compile/resource/state checks remain mandatory. |
| OptiX | Thick spline/linear use built-in round primitives; Ribbon is **custom intersection**, not a built-in round curve (`src/device/optix/device_impl.cpp:1731–1761`; `src/kernel/device/optix/bvh.h:305–345`). | Shared surface changes and qualification of existing SBT/feature selection. No new closest-hit program is indicated by inspection. Mixed volume support does require separate all-hit/SBT work or a separate curve surface stream. |

`load_deep_pipeline()` calls `add_hit_miss_program_groups()`
(`src/device/optix/device_impl.cpp:977–990`); that helper includes every non-null
hit group (`:246`). Deep's SBT copies those headers (`:1049–1055`). Curve instance
SBT offsets are selected at `:1978–1994`. Closest-hit traces enforce any-hit and
use the ordinary closest-hit offset (`src/kernel/device/optix/bvh.h:404–439`).
Thus the surface pipeline is not missing curve groups merely because
kernel_deep.cu contains no curve entry-point definitions. The volume all-hit
replacement at offset 3 is **not** proof that all curve-specific SBT offsets use
the correct deep callback; that mapping must be designed and tested explicitly.

### 1.3 Shapes, hits, depth and facing

Blender's `scene.cycles_curves.shape` selects `RIBBONS` (Rounded Ribbons), `THICK`
(3D Curves), or `THICK_LINEAR` (Linear 3D Curves); subdivisions is separate.
See `blender-src/intern/cycles/blender/addon/properties.py:52`,
`blender/sync.cpp:996`, and `blender/curves.cpp:716,1138–1141`.

A closest-hit query returns one nearest accepted intersection, not a strand's
entry/exit interval. A simple isolated strand normally contributes one event
before an opaque stop or transparent continuation. There is **no universal
one-hit-per-strand guarantee** for curved/self-overlapping strands and interleaved
other objects. A thick strand is not automatically two deep events or a volume.

All three backends identify self by **curve primitive/strand, not segment**:
Embree maps `curve_segments[].prim` before filtering (`cpu/bvh.h:147–152`), OptiX
does likewise (`optix/bvh.h:241–266`), and CUDA tests `prim_index` before curve
intersection (`bvh/traversal.h:127–129`). `intersection_skip_self()` compares only
object+prim (`bvh/util.h:244`). Deep advances tmin and saves that identity.
This skips other segments of the immediately previous strand, including a
potential exit or fold. After another primitive is hit, a later portion of the
first strand can become eligible again. Clearing self to enumerate every segment
would change native transparency semantics and risk duplicates.

Beauty's transparent continuation also advances tmin without changing ray origin
or direction (`integrator/shade_surface.h:573`), and uses the last object/prim on
the next query (`integrator/intersect_closest.h:361–386`). Preserve this behavior;
prove it with bends, adjacent segments, interleaved strands and grazing rays.
Do not add a blanket same-object deduplication: it would erase distinct strands.

Depth should remain camera-space depth of `ray.P + isect.t * ray.D`, using the
accepted camera time (`deep/camera_depth.h:7`, CPU capture `:200`, GPU `:38`).
It is neither strand-center distance nor Euclidean ray length. It uses the same
backend hit as beauty setup. Ribbon shading rounds its normal but does not move
the hit onto an imagined cylinder (`geom/curve_intersect.h:909–927`); using such
a cylinder depth would disagree with beauty. Round curves use their actual
backend surface t, which can differ across Embree/BVH2/OptiX.

Ribbon `Ng` is set to `wi` (`geom/curve_intersect.h:951`); the backface test
(`geom/shader_data.h:116`) therefore does not express entry/exit topology.
For thick curves, the geometric normal gives a local facing sign, but native
intersection culling and whole-strand self skipping still prevent treating it
as a closed-medium winding test. CUDA's spline solver has explicit backface
culling (`geom/curve_intersect.h:237`). Keep facing as native surface provenance,
not as a hair medium boundary. Exact hit multiplicities remain unmeasured.

### 1.4 Curve shading and the actual junkshop material

`shader_setup_from_ray()` copies u/v/type and calls curve setup
(`geom/shader_data.h:57–80`). Curve setup uses packed segment/type, curve keys,
ray time, tangent, normals and shader ID (`geom/curve_intersect.h:865–954`).
Curve attributes interpolate with sd.u (`geom/curve.h:51–75`), so segment-local
u must not be replaced with a whole-strand intercept.

Hair Info is currently outside the allowlist. Is Strand, Intercept, Length,
Thickness, Tangent Normal and Random are separate proof obligations:
`src/scene/shader_nodes.cpp:4890–4996` requests attributes for Intercept/Length/
Random; `kernel/svm/geometry.h:191–220` handles type/thickness/tangent; thickness
and random helpers are at `kernel/geom/curve.h:92,130`. These are geometry-dependent
inputs and must be qualified in opacity, including after transparent continuation,
if later admitted. They are not needed merely to intersect opaque hair.

Principled Hair is already a permitted closure but requires curve-shading
qualification for both Chiang and Huang. It reads curve random attributes or a
fallback input (`kernel/svm/closure.h:912–940`). Its absorption/refraction affects
beauty radiance, not automatically camera transparency. Deep alpha remains
`1 - surface_shader_transparency(sd)` (`integrator/surface_shader.h:1047`), not
Hair BSDF attenuation. CPU uses full ShaderData; GPU uses tiny storage and
`store_closures=false`. Transparency is tracked separately, but curve-specific
closure execution, defaults, attributes and RNG use still need tests; source
inspection alone does not prove equality for arbitrary materials. Keep OSL,
Hair Info and other new tier-4 nodes out of the first allowlist expansion.

The read-only probe of `Face` found:

- `Churros Medianos` / `ParticleSettings.002`: eight stored parents, 1600 child
  particles; SIMPLE children, rendered_child_count 200, hair_step 5, material slot 2.
- `Churros Grandes` / `ParticleSettings.006`: the same counts/settings, slot 2.
- Both use material **Hair**; the stored settings.count is zero despite existing
  particles. Do not derive groom size from that property alone.
- Scene shape **RIBBONS**, subdivisions **2**.
- Exactly six nodes: `Principled BSDF` (ShaderNodeBsdfPrincipled), `Hair BSDF`
  and `Hair BSDF.001` (ShaderNodeBsdfHair), `Mix Shader` and `Mix Shader.001`
  (ShaderNodeMixShader), `Material Output` (ShaderNodeOutputMaterial).
- Principled BSDF and Hair BSDF feed Mix Shader; that result and Hair BSDF.001
  feed Mix Shader.001; its output feeds Material Output Surface. Both factors
  are constant 0.5, Principled Alpha is 1. There are no groups, textures,
  Transparent BSDF, Hair Info or Principled Hair nodes. All six are reachable.

No new tier-4 node is required by this Hair graph. Actual synchronized strand/key
counts and per-ray overlaps were not measured; stored particle counts are not
an evaluated render histogram. Its opaque closures imply early termination at
the first hair hit in the proposed surface path, rather than thousands of
transparent events merely because the groom has thousands of children.

## 2. Proposed architecture

Recommended first implementation: **static SVM Ribbon surfaces on all three
backends**, with the existing event limit and z tolerance zero. CPU establishes
the reference, then CUDA and OptiX qualify before promotion. This is narrower
than supporting all curve shapes/material inputs, yet directly covers the
junkshop Hair graph. It does not by itself promise the volume-enabled Girl smoke.

1. Preflight: allow only qualified static Hair/shape alongside Mesh; scope mesh
   casts/subdivision checks; retain deforming motion, point-cloud, curve-volume,
   procedural and OSL restrictions. Preserve camera visibility and shared-data
   behavior. Scanner mirrors these rules and reports why another shape/mode fails.
2. CPU and shared CUDA/OptiX surface capture: accept only those primitive types;
   guard triangle boundary deduplication by both types; retain native tmin/self
   semantics and generic 20-byte DEEP_SURFACE events. Do not alter beauty traversal,
   intersectors or shaders. No new writer/channel format is proposed.
3. OptiX surface pipeline: audit compiled feature flags, existing curve hit groups,
   SBT offsets and stack/resource records. Add programs only if this qualification
   identifies a missing path; the inspected registration already covers surfaces.
4. Export: start with z tolerance zero for curve scenes. Either reject positive
   tolerance clearly for that scope or later introduce a separately qualified
   provenance rule; do not silently change the user's setting. Numeric compression
   still uses the existing transmittance bound. Keep IDs at object granularity.
5. **Separate mixed-volume milestone:** do not simply feed hair into triangle
   medium-boundary code. Prefer a deep-private native closest-hit surface stream
   with curve self continuation, merged in depth order with the existing qualified
   medium intervals. Avoid double-counting triangle surfaces, handle opaque cutoffs,
   clipping and ordered shader state, and preserve the qualified medium oracle.
   An alternative is extending each backend's all-hit path, but that must reproduce
   nearest/whole-strand continuation rather than count all segment roots, and OptiX
   needs matching curve SBT entries. Prototype and compare before selecting it.
   CPU/CUDA/OptiX must all fail closed until their mixed mode is qualified.

The mixed-volume step is necessary before claiming the established junkshop
BG+Girl+Robots launch recipe works. A separate Girl surface-only launch is an
alternative the user can choose; changing scene settings per layer is not an
unrequested shortcut in this plan. No capacity increases or node additions are
implied by approving Ribbon surface capture.

## 3. Correctness contract

A raw hair event is a native camera-visible curve surface hit on a particular
accepted camera sample: camera-space Z, initially ZBack=Z, scalar local alpha,
native facing provenance, and synced object identity. It does not encode strand
thickness as extinction, scattering color, strand ID, or a cylinder interior.
Anti-aliasing comes from the accepted camera-ray population, not radius inflation.

For pixel p, the reference is `T_p(z) = mean_s product_(events at/before z)(1-a)`;
flattened alpha is `1-T_p(infinity)`. Include misses and every accepted sample.
Use raw per-camera records and independent analytic fixtures as in the repaired
validator in `c514c52dd`; finite-sample stochastic beauty alpha is not generally
identical to the deterministic expectation (Task 04 demonstrated this).
Depth-cut oracles must be independent of the EXR reconstruction. Raw capture
alone can validate compression but cannot prove that the intersector found all
intended hits; single-strand analytic/native-hit controls provide that proof.

The existing header error bound covers reconstruction/publication transmittance,
not geometric intersector error, Monte Carlo variance, radiance, or differences
between backends. With z tolerance zero, retain the all-depth oracle checks.
Positive `deep_z_tolerance` allows depth-band interiors to be approximated; it
must not be described as preserving every strand or every interior depth cut.
See [validation policy](../deep/VALIDATION.md) and [support](../deep/SUPPORT.md).

IDs remain per synced object/name. The Blender adapter syncs particle hair as a
separate Cycles object (`blender-src/intern/cycles/blender/object.cpp:548`), but
assigns the Blender object name (`:331`); emitter mesh and hair may therefore
share the same name hash. Validate actual manifest ownership; do not promise a
separate ID for each particle system or strand. Holdout semantics stay unchanged.

CPU beauty deep-on/off must remain exact within the same build. The existing CPU
capture copies camera state; GPU restores modified flag/rng_offset/transparent
bounce (`kernel/deep/surface_cuda.h:192–197`). This makes an isolated implementation
possible, not an automatic proof that future curve code cannot perturb beauty.
Changing shared beauty intersections, shader attributes/RNG behavior, or resource
configuration would violate the proposed boundary and require review. Exact CPU
beauty and unchanged beauty source/resource checks are mandatory acceptance gates.

## 4. Risks and limitations

### Event capacity, memory and performance

Default is 16 events (`src/session/deep.h:31`); surface maximum 64
(`kernel/deep/types.h:28`). Count is per camera sample, not strand population or
pixel. Opaque hair typically stops after one hair event (possibly preceded by
transparent surfaces). For partially transparent overlapping hair, use stress
cases with 8, 16, 32, 64 and 65+ visible events. Dense projected clumps can exceed
64; no defensible typical-groom percentile follows from particle count alone.
Profile median/p95/max, overflow pixels and shader evaluations before proposing
any new maximum. A chain exactly at the limit must pass; another required hit
must fail explicitly, without partial final publication or silent truncation.

Each event is 20 bytes (`kernel/deep/types.h:39–47`), plus a 12-byte result record.
The fixed-capacity logical reservation is
`width * height * samples * (12 + 20 * capacity)` in surface mode
(`deep/capture.cpp:50–53`). Examples, excluding other buffers:

| Workload | Capacity 16 | Capacity 64 |
| --- | --- | --- |
| 200×100, four samples | 26.56 MB | 103.36 MB |
| 1920×1080, 64 samples | 44.06 GB | 171.46 GB |

These are capacity/reservation estimates, **not measured resident memory or actual
compact spill sizes**. Spill/compaction and batch scheduling matter. A 480-lane
GPU event batch at capacity 64 is 614,400 bytes before metadata and staging
(`integrator/path_trace_work_gpu.h:36`). The default 64 MiB memory budget
(`session/deep.h:32`) also covers accounted capture/export working sets, not
beauty/geometry memory. Capture can reject an insufficient scanline budget
(`deep/capture.cpp:57–85`). More events increase shader evaluation, traversal,
readback, spill I/O, sorting and reconstruction work; no timing multiplier was
measured. Compression after capture does not cure capture overflow.

Same-object strands with the same facing are eligible for depth merging under
`deep/volume.cpp:34–57`; grouping is bounded by total relative depth diameter,
with other-object/volume barriers. There is no per-strand barrier. With many
ribbons of one object, positive tolerance can collapse distinct strands into a
span. This is why the first scope uses zero tolerance. Numeric reconstruction
can still reduce a dense event population within the existing error budget;
strict identity must remain deterministic within a qualified build/backend.

### Backend and motion limitations

Embree and CUDA/OptiX need not return identical t, u, segment selection or grazing
coverage. OptiX thick curves use hardware built-ins; CUDA uses its spline solver;
Ribbon OptiX uses the custom curve intersection routine. Even shared routines
can differ after compilation. Record near-tip/near-tangent failures, small radii,
endcaps, transformed instances, folds and near-coincident strands. Do not promise
cross-backend depth/sample-count or byte identity.

Static hair with **rigid object/camera motion** can potentially reuse the accepted
sample time and existing rigid-transform checks (`session/deep.cpp:599–632,789`).
It still needs qualification on each backend. Deforming keys/radii require
motion primitive acceptance, correctly interpolated geometry and attributes,
valid topology over shutter time, and matching beauty hits. Existing mesh
preflight rejects deformation; curve support must not accidentally relax that.
Defer deformation and keep explicit rejection. Frame-by-frame changed geometry
without motion blur is a distinct test, not proof of deformation blur.

Point clouds are **out of scope**. They have existing point/sphere intersection
and shading (`kernel/geom/point_intersect.h:66,94`) and OptiX point hit groups/
instance offsets (`device/optix/device_impl.cpp:1996`). The type-safe duplicate
helper and generic export work could be reused, but point radii, facing,
self-skip, Point Info, motion and mixed-volume behavior need independent guards
and tests. Hair support must not enable them by accident.

## 5. Milestones and exit criteria

Estimates are engineering days for one developer, excluding long build queues
and unexpected numerical-gate investigations; they are not completion promises.

| Milestone | Backends / rough effort | Exit criterion |
| --- | --- | --- |
| M0: approve contract and controls | All, 1–2 days | User selects shapes/modes; native-hit diagnostics and independent strand/depth fixtures defined; no renderer promotion. |
| M1: static Ribbon surface prototype | CPU, 2–4 days | Type-safe preflight/capture, opaque and transparent-chain tests, 16/64 overflow handling, exact CPU beauty, no regression. OSL/deform/volume modes reject clearly. |
| M2: same surface contract on GPU | CUDA + OptiX, 3–5 days | Both own-oracle/depth/ID tests and GPU beauty policy pass; OptiX SBT/stack verified; native controls recorded; all existing gates remain unchanged. |
| M3: mixed hair surfaces + existing volumes | CPU/CUDA/OptiX, 4–8 days | No missing/duplicate curve events, medium oracles unchanged, opacity state ordered correctly, smoke/hair depth cuts pass; standard all-layer junkshop smoke can be attempted. |
| M4: 3D/linear shapes and optional Hair Info | All, 3–6 days | Each selected shape/output qualifies separately, including thin/grazing/folded strands and opacity-driven attributes. No blanket tier-4 admission. |
| M5: motion expansion | Rigid all backends, 2–3 days; deformation separately estimated after design | Shutter-time depths/alpha/IDs and beauty gates pass; deformation stays rejected until separately approved and qualified. |
| Promotion | All approved scopes, 1–2 days | Scanner drift, regressions, resource/source records, repeat identity, OpenEXR/Gaffer, documented limits and authorized junkshop smoke pass. |

A failed existing gate stops promotion and follows VALIDATION.md; never raise a
bound, drop an unfavorable fixture, or replace references to make it pass.

## 6. Tests and oracles

| Fixture | Required observations |
| --- | --- |
| Single strand over transparent world and then a plane | Known radius/depth; hit/miss camera samples, tips/grazing/inside starts, opaque and constant transparent mix; one segment and connected multi-segment versions. |
| Dense groom over a background plane | Sparse-to-dense event histogram, capacities 16/64 and overflow, memory/spill failure, no partial publication; distinct strands of the same object. |
| Ribbon, thick spline, thick linear | Native t/u/type/prim correspondence, normal/facing, no forced cylinder interpretation; folds, adjacency, interleaving A/B/A, different objects and instances. Unsupported shapes must reject before render. |
| Hair BSDF and Principled Hair | Legacy reflection/transmission, Chiang/Huang, default attributes, constant and varying transparent mix. Junkshop's six-node Hair graph as a separate fixture. |
| Hair Info, only if approved | Every requested output alone and nested through groups, driving color and opacity; intercept/random variation and transparent continuation. |
| Indirect-only / excluded / camera-off | No hair deep events or false blocker; shared groom geometry remains checked where another camera-visible object uses it. Include holdout ownership. |
| Hair occluding/occluded | Opaque plane in front suppresses hair; hair in front suppresses background only when opaque; two hair objects, volumes before/between/behind in M3. |
| Animation | Static frames with changed geometry; rigid object/camera blur; unsupported key/radius deformation rejection, then separate qualification if approved. |

For every approved backend: raw per-camera oracle, independent known-hit checks,
all-depth cuts with z tolerance zero, object-ID ownership and holdout manifests,
strict/numeric repeat identity, finite/sorted valid EXR events and Gaffer
DeepToFlat/DeepSlice/DeepMerge. Raw diagnostic fields should include curve type,
strand primitive, packed segment, u/v, t, native facing and object to distinguish
traversal errors from reconstruction errors; keep instrumentation side-channel.

CPU deep-on/off RGBA and required passes remain exact. GPU-side work requires
separate CUDA and OptiX controls and the calibrated raw-beauty policy in
[VALIDATION.md](../deep/VALIDATION.md#cudaoptix-raw-beauty-policy), not a newly chosen
tolerance. Cross-backend flattened-alpha limit stays **1e-4** with only the
existing native-backend waiver conditions; report each side's own gates first.
Depth shifts and sample-count differences are informational cross-backend.
The raw-sample oracle repair does not automatically waive the policy's separate
native-beauty evidence requirements; if a proposed transparent hair case cannot
meet them, stop for an explicit decision rather than reinterpret the policy.

## 7. Decisions for the user

Recommended: approve **M0–M2, static SVM Ribbon surfaces, CPU then both CUDA and
OptiX, z tolerance zero**, without Hair Info, point clouds, deformation, OSL or
capacity changes. All three already have ordinary surface curve traversal, and
CUDA is useful as the software GPU comparison for OptiX. This matches the actual
junkshop hair shape/material at the smallest useful scope.

Before promising the full junkshop result, choose whether to approve **M3 mixed
volume support** or accept a separately configured Girl surface-only render.
The existing volume-enabled smoke is not covered by M1/M2 alone.

Other review choices:

- Ribbon-only first versus adding THICK and THICK_LINEAR immediately. Both 3D
  variants add meaningful intersection/facing edge cases; no current junkshop need.
- CPU+OptiX first reduces initial qualification work but must keep CUDA explicitly
  unsupported until M2 finishes. Recommended: qualify all three before promotion.
- Hair Info is not required by the inspected scene. Principled Hair fixtures are
  still required because its closure is already on the allowlist; this is not
  permission to admit arbitrary geometry-dependent shader nodes.
- Keep 16 default/64 maximum initially; collect measured event distributions
  before deciding whether transparent dense grooms require a capacity redesign.
- Keep positive depth tolerance outside initial curve scope, or separately approve
  the documented loss of within-band strand/depth separation and its tests.

### Investigation record and remaining unknowns

One read-only Blender background probe was run, with a 300-second timeout:
`D:/temp/cycles_deep_multilayer/hair-probe/inspect.py`, launched by
`D:/temp/cycles_deep_multilayer/run_hair_probe.py`. Exact command is in
`hair-probe/command.json`; output is `hair-probe/junkshop-hair.json` and
`hair-probe/inspect.log`. It exited 0 in about eight seconds, loaded the source
with factory startup and auto-execution disabled, inspected RNA/material links,
and did not render or save. No scratch source edits, builds or modified-guard
renders were performed. All generated probe outputs/caches stayed under the
permitted output root; the scratch tools and qualified prior install were not
modified.

Source SHA-256 before and after:
`b050ae097807cb544489ba715fde17862c2c2df437b96b51bffb5e9cf914e851`.

Not determined experimentally: actual synchronized groom/event counts, hair
capture accuracy/performance, thick-curve root multiplicity and edge cases,
GPU register/stack costs, mixed-volume curve SBT behavior, and deformation
qualification. These are milestone exit criteria, not claims of working support.
Nuke was not run. No render output or renderer change is delivered by this plan.
