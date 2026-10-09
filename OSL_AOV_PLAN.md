# Side project: AOV Output support in OSL mode

Status: parked (2026-10-07). Start only after the deep optimization plan
(`DEEP_OPTIMIZATION_PLAN.md`) has completed Phase 9 and the user confirms.
This project is independent of deep output and must not touch deep code.

## 1. Problem

Shader AOVs are user-defined flat render layers (View Layer > Shader AOV). In a
material, the **AOV Output** node writes a colour or value into one of them.

Cycles evaluates shader graphs either with SVM (built-in, all devices) or OSL
(Open Shading Language: CPU, and GPU only through OptiX). The AOV Output node is
implemented only for SVM. Its OSL compile function is an empty placeholder:

```cpp
// src/scene/shader_nodes.cpp:6856
void OutputAOVNode::compile(OSLCompiler & /*compiler*/)
{
  /* TODO */
}
```

With OSL enabled, every shader AOV is silently empty. Studios that use OSL
materials lose their custom mask/utility layers.

## 2. How SVM does it today (the behaviour to match)

- `SVMCompiler::find_aov_nodes_and_dependencies` (`src/scene/svm.cpp:826`)
  collects AOV Output nodes with a valid `offset` and their input dependencies,
  even though they are not connected to the Material Output.
- `NODE_AOV_START` (`svm.cpp:1142`) lets the kernel skip AOV-only nodes when no
  AOV will be written.
- `svm_node_aov_check` (`src/kernel/svm/aov.h`): write only for the primary
  camera path (`PATH_RAY_TRANSPARENT_BACKGROUND` set and
  `PATH_RAY_SINGLE_PASS_DONE` not set) and when `render_buffer` is available.
- `svm_node_aov_color` / `svm_node_aov_value`: skip on texture cache miss
  (`SR_CACHE_MISS`), else call `film_write_aov_pass_color` /
  `film_write_aov_pass_value` (`src/kernel/film/aov_passes.h`) with the node's
  pass offset.

OSL output must match these semantics exactly: same pixels written, same
primary-path and transparency rules, same pass layout.

## 3. Step 0: check upstream first

Before writing code, search Blender's tracker and code review
(projects.blender.org, "AOV OSL", "OutputAOVNode OSL") and recent Cycles
history. If upstream has implemented or is reviewing this, report it and
propose backporting or reviewing that work instead of duplicating it.

## 4. Design

Recommended approach: carry AOV writes as a special internal closure,
handled in the shared OSL closure flattening code that already runs on both
CPU and OptiX.

1. **Compiler** (`src/scene/osl.cpp`, `OSLCompiler`): like SVM, find AOV Output
   nodes with `offset >= 0` and their dependencies, and include them in the
   compiled surface group even though they are not connected to the output.
   Have the group's final output append one AOV closure per AOV node at the top
   level of `Ci` (after the material's own closure tree, so mix/add closure
   weights never scale it).
2. **OSL shader** (`src/kernel/osl/shaders/node_output_aov.osl`, new): emits the
   internal closure with the AOV's pass offset and colour/value carried in its
   weight (value AOVs use a grey weight and read one channel). Use
   `raytype("camera")` so non-camera rays skip the work.
3. **Closure registration** (`src/kernel/osl/closures_template.h`,
   `closures.cpp`): register the internal closure for CPU and OptiX. It must
   never become a BSDF/emission/volume closure.
4. **Kernel** (`flatten_closure_tree` in `src/kernel/osl/osl.h`): when this
   closure is encountered, apply the same checks as `svm_node_aov_check` and the
   cache-miss rule, then call `film_write_aov_pass_color/value`. Do not add it to
   `sd` closures. `osl_eval_nodes` needs the `render_buffer` pointer passed
   through from `surface_shader_eval`, as `svm_eval_nodes` already receives it.
5. Keep beauty identical: the AOV closure must not change closure counts,
   weights, sampling or any other pass.

Alternative to evaluate only if the closure route hits an OSL limitation:
OSL renderer outputs (output parameters registered with the ShadingSystem,
read back after execution; on GPU via OSL's symbol-location API, if the bundled
OSL version supports it). Report the reason before switching.

## 5. Tests and acceptance

Build fixtures (standalone Cycles XML or Blender files) covering:
- Colour AOV and value AOV; several AOVs in one material; AOVs in several
  materials; an AOV name with no matching pass.
- AOV driven by textures, by an OSL Script node, and by a node shared with the
  surface closure.
- Primary-path rules: AOV behind a transparent surface, AOV on a surface seen
  only in reflection (must not write), AOV on the world/background if SVM writes
  it (match SVM).
- Mixed SVM-only features unaffected; texture cache miss behaviour.

Acceptance:
- CPU OSL AOV pixels match CPU SVM AOV pixels for the same scene (exact where
  node maths is identical; otherwise within a stated float tolerance, with any
  difference explained by known OSL/SVM node differences).
- OptiX OSL matches CPU OSL within the same tolerance.
- Beauty and all non-AOV passes are bit-identical with and without AOV nodes
  in OSL mode on CPU (GPU: within the existing run-to-run envelope).
- Existing Cycles tests pass.

## 6. Delivery

- Branch from `main` (not from the deep branch), e.g. `codex/osl-aov`, so the
  change stays independent and could be offered upstream to Blender.
- Follow Blender/Cycles code style; no deep-output dependencies.
- Keep the change small: compiler, one OSL shader, closure registration,
  flattening hook, tests. Write a short summary suitable for an upstream patch
  description.
