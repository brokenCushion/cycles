# Checkpoint 3: F12 feasibility

No F12-support implementation was added.

The plan's starting assumption was not true for the prior released build. A normal,
non-background Blender process using the F12 operator (`render.render` with
`INVOKE_DEFAULT`) completed a factory-cube CPU render and wrote both a Deep EXR
and its samples CSV. The probe used 32 x 32 pixels and four samples.
Evidence: `f12-existing/result.json`, `existing-f12.log`, and
`f12-existing/existing.deep.exr` in this verification directory.

Cycles' `BlenderSession::background` means an offline render and is true for F12
as well as command-line final renders. `BlenderSession::headless` reflects
`bpy.app.background`. Both final-render routes use `BlenderOutputDriver`;
viewport rendering and baking have separate session/driver paths.

A separate Deep output driver does not appear necessary for F12. Supporting it
in this UI update would require revising the explicit headless guards and the
background-only UI/warning policy. That change has not been made. The patched
build was verified to show the warning, complete beauty, and publish no Deep EXR
or CSV during an interactive render (`gui-runtime/results.json`, screenshots,
and `gui-runtime.log`). This deliberately restricts behavior observed in the
prior build, in accordance with the agreed plan.

Before qualifying F12 support, test interactive cancellation and atomic
publication, repeated/persistent sessions, animation and multiple view layers,
large capture/spill workloads, GPU backends, and display/context lifetime.
Large exports may delay interactive completion; diagnostic CSV must remain
opt-in. The small prior-build probe establishes feasibility, not production
qualification for those cases.
