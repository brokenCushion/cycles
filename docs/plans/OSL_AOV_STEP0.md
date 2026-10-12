# OSL AOV: Step 0 upstream check

Checked on 2026-10-13 (Australia/Sydney). Stopped at the requested Step 0
checkpoint. No implementation, builds, renders, or acceptance tests performed.

## Local setup

- Worktree: `C:\Users\jun\Documents\ChatGPT\cycles_osl_aov`
- Branch: `feature/osl-aov`, created from `upstream/main`.
- Base: `a456b761034dda42c32eef9f4aae0fa5a5c9f604` (2026-09-08).
- The original plan was read fully, copied unchanged, and committed as
  `30604f856` (`docs: record OSL AOV implementation plan`). Its parked status
  records the original document's state; the user authorized this project.
- Temporary research downloads and process TEMP/TMP were confined to
  `D:\temp\cycles_osl_aov`. This report contains the durable findings and
  source identifiers; it does not depend on those disposable downloads.
- No source files in either checkout were changed. The main checkout's branch
  and working files were left alone. No remote writes occurred.

## Existing work

[OSL AOV support #134021](https://projects.blender.org/blender/blender/pulls/134021)
by Alex Fuller is **open and unmerged**, verified through the live tracker API.
Its head is `86f9dc3e37db2709b316a7cfa5b410be58434138`; the API reports
32 changed files, 511 additions, and 45 deletions. The downloaded diff includes
vector AOVs, debug-closure registration, AOV name lookup, render-buffer
plumbing, and CPU layer execution. It is broader than this project's scope.

The review discusses avoiding separate layer execution, writing AOVs directly,
and preserving outputs from OSL optimization. The author reports CPU progress
and OptiX compilation, but raises unresolved GPU layer-execution questions.
In a 2026-03-22 comment, he says the closure approach worked, but mailing-list
feedback favored an intermediate buffer; that redesign had not been done.
These statements are upstream reports, not locally verified test results.

[Improve custom shader AOVs #73266](https://projects.blender.org/blender/blender/issues/73266)
is open and still lists OSL support as unfinished. Reports
[#79888](https://projects.blender.org/blender/blender/issues/79888) and
[#124609](https://projects.blender.org/blender/blender/issues/124609) are closed;
the latter was closed as an existing TODO, not as an implemented fix.

The live all-state review search for `OSL AOV` also returned
[#154773](https://projects.blender.org/blender/blender/pulls/154773), a closed,
unmerged Light Path Expression proposal. Its description concerns LPE passes,
not implementing the AOV Output node in OSL. Other results were shader string
support (#151184) and Portal BSDF (#114386). `OutputAOVNode` returned no reviews.
Web searches also covered `AOV OSL` and `OutputAOVNode OSL`.

## Recent Cycles history

`git ls-remote upstream refs/heads/main` returned the same SHA as the base;
the GitHub commits API also returned that tip as its newest commit. No fetch
or shared remote-reference update was needed.

At this base, `src/scene/shader_nodes.cpp:6856` still contains an empty
`OutputAOVNode::compile(OSLCompiler&)`. Recent AOV-related commits found by
searching history since 2025-01-01 were:

- `4457b37da` (2026-05-04): fix AOV changes between view layers.
- `6ff1a328d` (2026-03-30): remove a redundant AOV offset lookup.
- `d4396d501` (2025-02-27): update OptiX denoiser type.

OSL compiler/kernel history was also inspected. No completed OSL AOV
implementation was found in the current standalone Cycles main branch.
The current SVM implementation still checks primary-path flags, a non-null
render buffer, and texture cache misses as described in the original plan.

## Recommendation and limits

Review #134021 before starting a duplicate implementation. Assess selective
adaptation of its useful plumbing against the current Cycles code and the
unchanged acceptance criteria. A wholesale backport is not yet justified:
the patch includes extra vector support and does not establish CPU/OptiX
parity or beauty invariance required here.

Resolve the intermediate-buffer feedback before committing to the plan's
closure design. The linked
[OSL developer discussion](https://lists.aswf.io/g/osl-dev/topic/implementing_aovs/111028312)
returned a JavaScript-required page, so the underlying reasoning was not read;
the buffer recommendation is known only from the author's tracker comment.
No alternative design has been selected and no criteria have been relaxed.

This checkpoint does not validate the upstream patch's behavior. CPU and
OptiX implementation remain pending the next user instruction.
