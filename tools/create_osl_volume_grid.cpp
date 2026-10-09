/* SPDX-License-Identifier: Apache-2.0 */
/* Fixture assets only. Build with the pinned Blender OpenVDB/Imath/TBB libraries. */
#include <openvdb/openvdb.h>
#include <openvdb/io/File.h>
#include <filesystem>
#include <stdexcept>
int main(int argc, char **argv)
{
  if (argc != 2) throw std::invalid_argument("OUTPUT_DIRECTORY required");
  std::filesystem::create_directories(argv[1]);
  openvdb::initialize();
  for (const auto name : {"grid", "texture", "texture-baked", "svm-optin-baked"}) {
    auto grid = openvdb::FloatGrid::create(0);
    grid->setName("density");
    grid->setGridClass(openvdb::GRID_FOG_VOLUME);
    grid->setTransform(openvdb::math::Transform::createLinearTransform(.125));
    auto values = grid->getAccessor();
    for (int z = -32; z <= -16; ++z)
      for (int y = -8; y <= 8; ++y)
        for (int x = -8; x <= 8; ++x) {
          const float density = std::string(name) == "grid" ? .0005f + .00001f * (z + 32) :
                                                               .2f + .002f * (z + 32);
          const float texture = .8f + .04f * (float(x) * .125f + 2) / 4;
          const float value = std::string(name) == "svm-optin-baked" ? density * density :
                              std::string(name) == "texture-baked" ? density * texture : density;
          values.setValue({x, y, z}, value);
        }
    openvdb::io::File file((std::filesystem::path(argv[1]) / (std::string(name) + ".vdb")).string());
    file.write(openvdb::GridPtrVec{grid});
    file.close();
  }
}
