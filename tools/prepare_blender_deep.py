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
    deep_max_events: IntProperty(name="Deep Events Per Sample", default=16, min=1, max=64)
    deep_memory_mb: IntProperty(name="Deep Working Memory MiB", default=512, min=1, max=1024)
''')
edit('blender/sync.cpp', '  return params;\n}\n\nDenoiseParams BlenderSync::get_denoise_params', '''#ifdef WITH_CYCLES_DEEP_OPAQUE
  params.deep.enabled = background && !(b_engine.flag & blender::RE_ENGINE_PREVIEW) &&
                        get_boolean(cscene, "use_deep_output");
  if (params.deep.enabled) {
    params.deep.transparent = true;
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
  if (tile.volume || deep_path_.empty()) {
    throw std::runtime_error("Blender deep adapter requires surface capture and an output path");
  }
  deep::SurfaceImage image;
  image.display_window = {0, 0, tile.width - 1, tile.height - 1};
  image.data_window = image.display_window;
  image.frame = deep_frame_;
  image.view = tile.view.empty() ? "default" : tile.view;
  image.compression = deep::DeepCompression::Zips;
  const auto check_cancel = [&] {
    if (tile.cancelled())
      throw std::runtime_error("Blender deep export cancelled; final file preserved");
  };
  /* Small, reproducible diagnostic grid for independent reader validation.
   * This is raw accepted camera data, before pixel reconstruction/FLOAT export. */
  deep::AtomicOutput records_publication(deep_path_ + ".samples.csv");
  std::ofstream records(records_publication.temporary());
  records.exceptions(std::ios::badbit | std::ios::failbit);
  records << "file_x,file_y,sample,depth,alpha,event\\n"
          << std::setprecision(std::numeric_limits<float>::max_digits10);
  for (int y = 0; y < tile.height; ++y) {
    if (y % std::max(1, tile.height / 8) && y != tile.height - 1)
      continue;
    check_cancel();
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
  }
  records.close();
  check_cancel();
  records_publication.publish();
  deep::write_deep_exr_rows(deep_path_, image, [&](const int y) {
    check_cancel();
    std::vector<std::vector<deep::SurfaceSample>> row(tile.width);
    for (int x = 0; x < tile.width; ++x) {
      for (const auto &sample : tile.get_pixel(x, tile.height - 1 - y))
        row[x].push_back({sample.front, sample.alpha});
    }
    check_cancel();
    return row;
  }, check_cancel);
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
