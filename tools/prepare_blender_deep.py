"""Overlay the current Cycles tree and its native Blender deep adapter.

Run only after the clean baseline build/render completes. The pinned Blender
checkout must be clean; this script refuses to overwrite local modifications.
Generated source stays in builds/, while this reproducible adapter is tracked.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '749518deb2f0735a22361a07488b35f7ea5c2fdf'
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT/'builds/blender/source')
parser.add_argument('--check', action='store_true', help='Validate the overlay without writing files')
args = parser.parse_args()
source = args.source.resolve()

def git(*args):
    return subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()

if git('rev-parse', 'HEAD') != BASE:
    raise RuntimeError('Blender source must be at the qualified baseline revision')
if git('status', '--porcelain', '--untracked-files=normal'):
    raise RuntimeError('Blender source has local changes; preserve/reconcile them before overlay')
target = source/'intern/cycles'
prepared = {}

def read_text(relative):
    return (prepared[relative].decode() if relative in prepared
            else (target/relative).read_text())

def stage_text(relative, text):
    prepared[relative] = text.encode()

def edit(relative, old, new, count=1):
    contents = read_text(relative)
    if contents.count(old) != count:
        raise RuntimeError(f'Unexpected adapter anchor count: {relative}: {old!r}')
    stage_text(relative, contents.replace(old, new, count))

files = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '--cached',
    '--others', '--exclude-standard', '--', 'src'], text=True).splitlines()
manifest = {'blender_revision': BASE, 'files': {}}
for name in files:
    path = ROOT/name
    # git ls-files includes tracked files deleted in the current worktree.
    # Skip them rather than trying to read a file that is no longer present.
    if not path.is_file():
        continue
    relative = path.relative_to(ROOT/'src')
    if relative.parts[0] == 'blender':
        raise RuntimeError('Unexpected Blender host code in standalone overlay')
    # Blender owns dependency discovery; the standalone version searches system libraries.
    if relative.as_posix() == 'cmake/external_libs.cmake':
        continue
    # Stage all edits before touching the checkout, including adapter anchors.
    prepared[relative.as_posix()] = path.read_bytes().replace(b'\r\n', b'\n')

# Match the Blender host option name (standalone prefixes it with CYCLES).
edit('CMakeLists.txt', 'if(WITH_CYCLES_PUGIXML OR OPENIMAGEIO_PUGIXML_FOUND)',
     'if(WITH_PUGIXML OR OPENIMAGEIO_PUGIXML_FOUND)')
edit('CMakeLists.txt', '# Subdirectories\n', '''# Experimental deep capture is shared with the standalone renderer.
option(WITH_CYCLES_DEEP_OPAQUE "Enable experimental deep camera visibility" ON)
if(WITH_CYCLES_DEEP_OPAQUE)
  add_compile_definitions(WITH_CYCLES_DEEP_OPAQUE)
  add_subdirectory(deep)
endif()

# Subdirectories
''')
edit('blender/CMakeLists.txt', 'set(ADDON_FILES\n', '''if(WITH_CYCLES_DEEP_OPAQUE)
  list(APPEND LIB PRIVATE cycles_deep_exr)
endif()

set(ADDON_FILES
''')
edit('blender/addon/properties.py', 'class CyclesRenderSettings(bpy.types.PropertyGroup):\n', '''class CyclesRenderSettings(bpy.types.PropertyGroup):
    use_deep_output: BoolProperty(
        name="Experimental Deep Visibility", default=False,
        description="Write scalar camera visibility Z/ZBack/A alongside beauty",
    )
    deep_output_path: StringProperty(
        name="Deep EXR", subtype='FILE_PATH', default="",
    )
    deep_max_events: IntProperty(name="Deep Events Per Sample", default=16, min=1, max=8192,
        description="Surface/homogeneous limit: 64; native grids use at least 4096, up to 8192")
    use_deep_volume: BoolProperty(
        name="Deep Volume Visibility", default=False,
        description="Capture scalar absorption through supported volume density grids",
    )
    use_deep_volume_shader_eval: BoolProperty(name="Deep Volume Shader Evaluation", default=False,
        description="Opt in to adaptive extinction evaluation; OSL volumes use it automatically")
    deep_volume_step: FloatProperty(name="Deep Volume Step", default=0, min=0,
        description="World-unit step cap; 0 derives from grid voxels, nongrid volumes require an explicit step")
    deep_z_tolerance: FloatProperty(name="Deep Surface Depth Tolerance", default=1e-4, min=0,
        description="Relative same-object surface depth span; 0 disables; strict forces 0")
    deep_error: FloatProperty(name="Deep Transmittance Error", default=1e-3, min=0, max=1e-2,
        description="0 selects strict; nonzero must exceed the 1e-6 FLOAT precision floor")
    deep_memory_mb: IntProperty(name="Deep Working Memory MiB", default=512, min=1, max=2147483647)
    use_deep_ids: BoolProperty(name="Deep Object IDs", default=False,
        description="Write a per-sample UINT object ID and name manifest")
    deep_samples: IntProperty(name="Deep Samples", default=0, min=0, max=4096,
        description="First N accepted camera samples; 0 uses all beauty samples")
''')
edit('blender/addon/ui.py', 'class CYCLES_RENDER_PT_film_pixel_filter', """class CYCLES_RENDER_PT_deep(CyclesButtonsPanel, Panel):
    bl_label = "Deep Visibility"
    bl_parent_id = "CYCLES_RENDER_PT_film"
    bl_options = {'DEFAULT_CLOSED'}

    def draw_header(self, context):
        self.layout.prop(context.scene.cycles, "use_deep_output", text="")

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        col = layout.column()
        col.active = context.scene.cycles.use_deep_output
        for prop in ("deep_output_path", "use_deep_volume", "use_deep_volume_shader_eval",
                     "deep_volume_step", "use_deep_ids",
                     "deep_error", "deep_z_tolerance", "deep_samples", "deep_max_events", "deep_memory_mb"):
            col.prop(context.scene.cycles, prop)


class CYCLES_RENDER_PT_film_pixel_filter""")
edit('blender/addon/ui.py', '    CYCLES_RENDER_PT_film_pixel_filter,',
     '    CYCLES_RENDER_PT_deep,\n    CYCLES_RENDER_PT_film_pixel_filter,')
edit('blender/sync.cpp', '  return params;\n}\n\nDenoiseParams BlenderSync::get_denoise_params', '''#ifdef WITH_CYCLES_DEEP_OPAQUE
  params.deep.enabled = background && !(b_engine.flag & blender::RE_ENGINE_PREVIEW) &&
                        get_boolean(cscene, "use_deep_output");
  if (params.deep.enabled) {
    params.deep.transparent = true;
    params.deep.volume = get_boolean(cscene, "use_deep_volume");
    params.deep.volume_shader_eval = get_boolean(cscene, "use_deep_volume_shader_eval");
    params.deep.volume_step = get_float(cscene, "deep_volume_step");
    params.deep.error = get_float(cscene, "deep_error");
    params.deep.z_tolerance = get_float(cscene, "deep_z_tolerance");
    params.deep.samples = get_int(cscene, "deep_samples");
    params.deep.ids = get_boolean(cscene, "use_deep_ids");
    params.deep.max_events = get_int(cscene, "deep_max_events");
    params.deep.memory_bytes = size_t(get_int(cscene, "deep_memory_mb")) * 1024 * 1024;
    /* Deep publication covers a complete frame. Native automatic tiling is not
     * part of that contract yet; capture storage itself can spill to disk. */
    params.use_auto_tile = false;
  }
#endif
  return params;
}

DenoiseParams BlenderSync::get_denoise_params''')
edit('blender/output_driver.h', '  bool read_render_tile(const Tile &tile) override;\n', '''  bool read_render_tile(const Tile &tile) override;
#ifdef WITH_CYCLES_DEEP_OPAQUE
  void set_deep_output(const string &path, const int frame)
  {
    deep_path_ = path;
    deep_frame_ = frame;
  }
  bool supports_deep_output() const override { return !deep_path_.empty(); }
  void write_deep_render_tile(const DeepTile &tile) override;
 private:
  string deep_path_;
  int deep_frame_ = 1;
#endif
''')
edit('blender/output_driver.cpp', '#include "blender/output_driver.h"\n', '''#include "blender/output_driver.h"
#ifdef WITH_CYCLES_DEEP_OPAQUE
#  include "deep/exr_writer.h"
#  include "deep/publication.h"
#  include <algorithm>
#  include <fstream>
#  include <iomanip>
#  include <limits>
#endif
''')
edit('blender/output_driver.cpp', 'CCL_NAMESPACE_END', '''#ifdef WITH_CYCLES_DEEP_OPAQUE
void BlenderOutputDriver::write_deep_render_tile(const DeepTile &tile)
{
  if (deep_path_.empty()) {
    throw std::runtime_error("Blender deep adapter requires an output path");
  }
  deep::SurfaceImage image;
  image.display_window = {0, 0, tile.width - 1, tile.height - 1};
  image.data_window = image.display_window;
  image.frame = deep_frame_;
  image.view = tile.view.empty() ? "default" : tile.view;
  image.compression = deep::DeepCompression::Zips;
  image.volume_row_sample_limit = tile.volume_row_sample_limit();
  image.volume_export_workers = tile.volume_export_workers();
  image.export_statistics = tile.export_statistics();
  image.error = tile.error();
  image.volume_shader_error = tile.volume_shader_error();
  image.volume_shader_adaptive = tile.volume_shader_adaptive();
  image.volume_step_min = tile.volume_step_min();
  image.volume_step_max = tile.volume_step_max();
  image.z_tolerance = tile.z_tolerance();
  image.deep_samples = tile.sample_limit();
  image.object_manifest = tile.object_manifest();
  image.holdout_manifest = tile.holdout_manifest();
  image.ids = tile.ids();
  const auto check_cancel = [&] {
    if (tile.cancelled())
      throw std::runtime_error("Blender deep export cancelled; final file preserved");
  };
  const auto publish = [&](const string &path) {
  if (tile.volume) {
    /* Accepted per-camera intervals on a small grid, before pixel mixture
     * reconstruction/reduction. This is diagnostic evidence, not a completed
     * frame marker; the EXR is published independently below. */
    deep::AtomicOutput diagnostic(path + ".samples.csv");
    std::ofstream records(diagnostic.temporary());
    records.exceptions(std::ios::badbit | std::ios::failbit);
    records << "file_x,file_y,sample,front,back,value,kind,event\\n"
            << std::setprecision(std::numeric_limits<double>::max_digits10);
    const auto begin_row = [&](const int y) {
      check_cancel();
      tile.begin_row(tile.height - 1 - y);
      for (int gy = 0; gy < 9; ++gy) {
        if (gy * (tile.height - 1) / 8 != y) continue;
      for (int gx = 0; gx < 9; ++gx) {
        const int x = gx * (tile.width - 1) / 8;
        for (int sample = 0; sample < tile.population(x, tile.height - 1 - y); ++sample) {
          const auto camera = tile.get_camera_sample(x, tile.height - 1 - y, sample);
          size_t event = 0;
          for (const auto &v : camera.intervals)
            records << x << ',' << y << ',' << sample << ',' << v.front << ',' << v.back
                    << ',' << v.optical_depth << ",volume," << event++ << '\\n';
          for (const auto &s : camera.camera.events)
            records << x << ',' << y << ',' << sample << ',' << s.depth << ',' << s.depth
                    << ',' << s.alpha << ",surface," << event++ << '\\n';
          if (!event)
            records << x << ',' << y << ',' << sample << ",0,0,0,miss,-1\\n";
        }
      }
    }
    };
    const auto end_row = [&](const int y) {
      tile.end_row(tile.height - 1 - y);
      if (y == tile.height - 1) {
        records.close();
        check_cancel();
        diagnostic.publish();
      }
    };
    deep::write_volume_exr_pixels(path, image, [&](const int x, const int y) {
      check_cancel();
      auto pixel = tile.get_pixel(x, tile.height - 1 - y);
      check_cancel();
      return pixel;
    }, check_cancel, begin_row, end_row);
    return;
  }
  /* Small, reproducible diagnostic grid for independent reader validation.
   * This is raw accepted camera data, before pixel reconstruction/FLOAT export. */
  deep::AtomicOutput records_publication(path + ".samples.csv");
  std::ofstream records(records_publication.temporary());
  records.exceptions(std::ios::badbit | std::ios::failbit);
  records << "file_x,file_y,sample,depth,alpha,event\\n"
          << std::setprecision(std::numeric_limits<float>::max_digits10);
  const auto begin_row = [&](const int y) {
    check_cancel();
    tile.begin_row(tile.height - 1 - y);
    if (y % std::max(1, tile.height / 8) && y != tile.height - 1)
      return;
    for (int x = 0; x < tile.width; ++x) {
      if (x % std::max(1, tile.width / 8) && x != tile.width - 1)
        continue;
      for (int s = 0; s < tile.population(x, tile.height - 1 - y); ++s) {
        const auto camera = tile.get_camera_sample(x, tile.height - 1 - y, s);
        if (camera.camera.events.empty())
          records << x << ',' << y << ',' << s << ",0,0,-1\\n";
        for (size_t i = 0; i < camera.camera.events.size(); ++i) {
          const auto &event = camera.camera.events[i];
          records << x << ',' << y << ',' << s << ',' << event.depth << ','
                  << event.alpha << ',' << i << '\\n';
        }
      }
    }
  };
  const auto end_row = [&](const int y) {
    tile.end_row(tile.height - 1 - y);
    if (y == tile.height - 1) {
      records.close();
      check_cancel();
      records_publication.publish();
    }
  };
  deep::write_deep_exr_rows(path, image, [&](const int y) {
    check_cancel();
    std::vector<std::vector<deep::SurfaceSample>> row(tile.width);
    for (int x = 0; x < tile.width; ++x) {
      for (const auto &sample : tile.get_pixel(x, tile.height - 1 - y))
        row[x].push_back({sample.front, sample.alpha, sample.object, sample.facing});
    }
    check_cancel();
    return row;
  }, check_cancel, begin_row, end_row);
  };
  /* Validation-only companion: both publications read the same immutable
   * accepted-camera capture. This avoids adaptive GPU variation in the
   * depth-domain check and avoids rendering or capturing the scene twice. */
  const char *baseline = std::getenv("CYCLES_DEEP_Z_BASELINE");
  if (baseline && baseline[0] && image.error && image.z_tolerance) {
    const float tolerance = image.z_tolerance;
    image.z_tolerance = 0;
    const auto start = std::chrono::steady_clock::now();
    publish(baseline);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::ofstream timing(string(baseline) + ".timing.json");
    timing.exceptions(std::ios::badbit | std::ios::failbit);
    timing << std::setprecision(17) << '{' << char(34) << "export_seconds" << char(34) << ':' << seconds;
    if (image.export_statistics) {
      using Stage = deep::ExportStatistics;
      timing << ',' << char(34) << "density_fit" << char(34) << ':' << image.export_statistics->seconds(Stage::DensityFit)
             << ',' << char(34) << "mixture_fit" << char(34) << ':' << image.export_statistics->seconds(Stage::MixtureFit)
             << ',' << char(34) << "quantize_coalesce" << char(34) << ':' << image.export_statistics->seconds(Stage::Quantize);
      for (auto &value : image.export_statistics->nanoseconds) value.store(0);
    }
    timing << "}";
    timing.close();
    image.z_tolerance = tolerance;
  }
  publish(deep_path_);
}
#endif

CCL_NAMESPACE_END''')
# Only the offline render driver gets a deep destination. Baking is unchanged.
text = read_text('blender/session.cpp')
text = text.replace('#include "DNA_screen_types.h"',
                    '#include "DNA_screen_types.h"\n#include "DNA_layer_types.h"', 1)
anchor = '  session->set_output_driver(make_unique<BlenderOutputDriver>(b_engine));'
if text.count(anchor) != 2:
    raise RuntimeError('Unexpected offline/bake driver setup')
text = text.replace(anchor, '''  auto output_driver = make_unique<BlenderOutputDriver>(b_engine);
#ifdef WITH_CYCLES_DEEP_OPAQUE
  blender::PointerRNA deep_scene_ptr = RNA_id_pointer_create(&b_scene->id);
  blender::PointerRNA deep_cycles = RNA_pointer_get(&deep_scene_ptr, "cycles");
  if (background && get_boolean(deep_cycles, "use_deep_output")) {
    int deep_layers = 0;
    for (const blender::ViewLayer &layer : b_scene->view_layers)
      deep_layers += (layer.flag & blender::VIEW_LAYER_RENDER) != 0;
    if (deep_layers != 1 || (b_scene->r.scemode & blender::R_MULTIVIEW)) {
      session->progress.set_error("Experimental deep output requires one view layer and mono rendering");
      update_status_progress();
      return;
    }
    const string path = get_string(deep_cycles, "deep_output_path");
    if (path.empty()) {
      session->progress.set_error("Deep EXR output path is empty");
      update_status_progress();
      return;
    }
    output_driver->set_deep_output(
        blender_absolute_path(*b_data, &b_scene->id, path), b_scene->r.cfra);
  }
#endif
  session->set_output_driver(std::move(output_driver));''', 1)
stage_text('blender/session.cpp', text)
for relative, data in prepared.items():
    manifest['files'][relative] = hashlib.sha256(data).hexdigest()
if args.check:
    print('Validated overlay anchors and', len(prepared), 'files; no source changes written')
else:
    for relative, data in prepared.items():
        destination = target/relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    (source.parent/'deep-overlay.json').write_text(json.dumps(manifest, indent=2))
    print('Prepared Blender deep overlay:', source)
