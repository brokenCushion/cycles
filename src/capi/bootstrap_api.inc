/* SPDX-License-Identifier: Apache-2.0 */
ccl_status CCL_CALL ccl_api_version(ccl_version *out)
{
  CCL_BOOTSTRAP_FORWARD(ccl_api_version, (out));
  return boundary([&]() -> ccl_status {
    if (!out || out->size < sizeof(*out)) {
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid version output size");
    }
    out->major = 1;
    out->minor = 7;
    out->patch = 0;
    return CCL_SUCCESS;
  });
}

ccl_status CCL_CALL ccl_has_feature(const char *name, int32_t *supported)
{
  CCL_BOOTSTRAP_FORWARD(ccl_has_feature, (name, supported));
  return boundary([&]() -> ccl_status {
    if (!name || !supported) {
      return fail(CCL_ERROR_INVALID_ARGUMENT, "NULL feature name or output");
    }
    bool available = std::strcmp(name, "cpu") == 0;
#ifdef WITH_CUDA
    available |= std::strcmp(name, "cuda") == 0;
#endif
#ifdef WITH_OPTIX
    available |= std::strcmp(name, "optix") == 0;
#endif
#ifdef WITH_HIP
    available |= std::strcmp(name, "hip") == 0;
#endif
#ifdef WITH_HIPRT
    available |= std::strcmp(name, "hiprt") == 0;
#endif
#ifdef WITH_METAL
    available |= std::strcmp(name, "metal") == 0;
#endif
#ifdef WITH_ONEAPI
    available |= std::strcmp(name, "oneapi") == 0;
#endif
#ifdef WITH_OSL
    available |= std::strcmp(name, "osl") == 0;
#endif
#ifdef WITH_CYCLES_DEEP_OPAQUE
    available |= std::strcmp(name, "deep") == 0;
#endif
    *supported = available ? 1 : 0;
    return CCL_SUCCESS;
  });
}

ccl_status CCL_CALL ccl_session_params_default(ccl_session_params *p)
{
  CCL_BOOTSTRAP_FORWARD(ccl_session_params_default, (p));
  return boundary([&] {
    if (!p || p->size < sizeof(*p))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid session params size");
    auto size = p->size;
    *p = {};
    p->size = size;
    p->device_index = UINT32_MAX;
    p->headless = p->background = 1;
    p->bvh_layout = "AUTO";
    p->bvh_compact_structure = p->bvh_unaligned_nodes = 1;
    p->hair_subdivisions = 3;
    p->texture_resolution = 1.0f;
    p->use_texture_cache = 1;
    return CCL_SUCCESS;
  });
}

ccl_status CCL_CALL ccl_deep_params_default(ccl_deep_params *p)
{
  CCL_BOOTSTRAP_FORWARD(ccl_deep_params_default, (p));
  return boundary([&] {
    if (!p || p->size < sizeof(*p))
      return fail(CCL_ERROR_INVALID_ARGUMENT, "Invalid deep params size");
    auto size = p->size;
    *p = {};
    p->size = size;
    p->max_events = 16;
    p->memory_bytes = UINT64_C(64) * 1024 * 1024;
    p->error = 1e-3f;
    p->z_tolerance = 1e-4f;
    return CCL_SUCCESS;
  });
}
