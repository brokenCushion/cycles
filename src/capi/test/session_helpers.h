/* SPDX-License-Identifier: Apache-2.0 */
/* Used by C and C++ ownership/editing tests; a requested backend must exist. */
static uint32_t test_device(const char *backend)
{
  uint32_t count = 0;
  if (ccl_device_count(&count) != CCL_SUCCESS)
    abort();
  for (uint32_t i = 0; i < count; ++i) {
    ccl_device_desc d = {0};
    d.size = sizeof(d);
    if (ccl_device_info(i, &d) != CCL_SUCCESS)
      abort();
    if (!strcmp(d.backend, backend)) {
      printf("Session device: %s %s\n", d.backend, d.description);
      return i;
    }
  }
  fprintf(stderr, "Required backend missing: %s\n", backend);
  abort();
}
