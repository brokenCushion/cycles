/* SPDX-License-Identifier: Apache-2.0 */
#include "../cycles_c.cpp"
#include <atomic>
#include <chrono>
#include <thread>
#undef CHECK
#define CHECK(x) \
  do { \
    if (!(x)) { \
      fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, ccl_last_error()); \
      abort(); \
    } \
  } while (0)
#define OK(x) CHECK((x) == CCL_SUCCESS)
struct Host {
  std::atomic<int> callbacks{0};
  bool fail = false, throws = false;
};
static int32_t CCL_CALL pixels(const ccl_render_tile *tile, void *user)
{
  auto &h = *static_cast<Host *>(user);
  ++h.callbacks;
  CHECK(tile->float_count == uint64_t(tile->width) * tile->height * 4);
  ccl_version v{sizeof(v)};
  CHECK(ccl_api_version(&v) == CCL_ERROR_CALLBACK_REENTRY);
  if (h.throws)
    throw std::runtime_error("host error");
  return h.fail;
}
static ccl_buffer_params settings(int samples = 4)
{
  return {sizeof(ccl_buffer_params), 47, 20, 0, 0, 47, 20, samples, 0, 0, 0, nullptr};
}
static ccl_progress snapshot(ccl_session *s)
{
  ccl_progress p{};
  p.size = sizeof(p);
  OK(ccl_session_progress(s, &p));
  CHECK(!p.error);
  return p;
}
static void sample(ccl_session *s, int minimum)
{
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (snapshot(s).sample < minimum) {
    CHECK(std::chrono::steady_clock::now() < end);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
int main(int argc, char **argv)
{
  CHECK(argc == 3);
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::filesystem::create_directories(argv[2]);
  ccl_init_params init{sizeof(init), argv[1], argv[2]};
  OK(ccl_init(&init));
  ccl_session_params p{};
  p.size = sizeof(p);
  OK(ccl_session_params_default(&p));
  p.background = 0;
  p.threads = 2;
  ccl_session *s;
  OK(ccl_session_create(&p, &s));
  ccl_scene *scene;
  OK(ccl_session_scene(s, &scene));
  ccl_node *pass, *integrator;
  OK(ccl_scene_singleton(scene, "integrator", &integrator));
  OK(ccl_node_create(scene, "pass", "combined", &pass));
  OK(ccl_node_set_string(pass, "name", "Combined"));
  ccl_output_pass request{sizeof(request), "Combined", 4};
  Host host;
  OK(ccl_session_set_output_callback(s, &request, 1, pixels, &host));
  CHECK(ccl_session_start(s) == CCL_ERROR_IN_USE);
  auto bp = settings();
  auto bad = bp;
  bad.width = 0;
  CHECK(ccl_session_reset(s, &bad) == CCL_ERROR_INVALID_ARGUMENT);
  auto missing = request;
  missing.name = "Absent";
  OK(ccl_session_set_output_callback(s, &missing, 1, pixels, &host));
  CHECK(ccl_session_reset(s, &bp) == CCL_ERROR_NOT_FOUND);
  auto wrong = request;
  wrong.channels = 1;
  OK(ccl_session_set_output_callback(s, &wrong, 1, pixels, &host));
  CHECK(ccl_session_reset(s, &bp) == CCL_ERROR_TYPE_MISMATCH);
  ccl_output_pass duplicate[] = {request, request};
  CHECK(ccl_session_set_output_callback(s, duplicate, 2, pixels, &host) ==
        CCL_ERROR_INVALID_ARGUMENT);
  OK(ccl_session_set_output_callback(s, &request, 1, pixels, &host));
  OK(ccl_session_reset(s, &bp));
  OK(ccl_session_start(s));
  sample(s, 4);
  OK(ccl_session_pause(s, 1));
  int before = snapshot(s).sample;
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(snapshot(s).sample == before);
  CHECK(ccl_node_set_int(integrator, "seed", 7) ==
        CCL_SUCCESS);  // Synchronous cancellation is the edit barrier.
  CHECK(!s->active && snapshot(s).reset_required);
  CHECK(ccl_session_pause(s, 0) == CCL_ERROR_IN_USE);
  CHECK(ccl_session_start(s) == CCL_ERROR_IN_USE);
  OK(ccl_session_reset(s, &bp));
  CHECK(snapshot(s).sample == 0);
  OK(ccl_session_pause(s, 0));
  sample(s, 4);
  CHECK(ccl_node_set_int(integrator, "seed", 9) == CCL_ERROR_IN_USE);
  int seed = 0;
  CHECK(ccl_node_get_int(integrator, "seed", &seed) == CCL_ERROR_IN_USE);
  CHECK(ccl_session_set_output_callback(s, nullptr, 0, nullptr, nullptr) == CCL_ERROR_IN_USE);
  OK(ccl_session_pause(s, 1));
  before = snapshot(s).sample;
  OK(ccl_session_set_samples(s, 8));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(snapshot(s).sample == before && !snapshot(s).reset_required);
  OK(ccl_session_pause(s, 0));
  sample(s, 8);
  CHECK(snapshot(s).sample >= 8);
  OK(ccl_session_pause(s, 1));
  // Observe the wrapper's lifetime pin solely to make this race test deterministic.
  std::thread waiter([&] { OK(ccl_session_wait(s)); });
  for (;;) {
    bool pinned;
    {
      std::lock_guard<std::mutex> lock(api_mutex);
      pinned = s->waiters != 0;
    }
    if (pinned)
      break;
    std::this_thread::yield();
  }
  CHECK(ccl_session_destroy(s) == CCL_ERROR_IN_USE);
  CHECK(ccl_node_set_int(integrator, "seed", 9) == CCL_ERROR_IN_USE);
  CHECK(ccl_session_reset(s, &bp) == CCL_ERROR_IN_USE);
  OK(ccl_session_cancel(s));
  waiter.join();
  CHECK(snapshot(s).reset_required);
  OK(ccl_node_get_int(integrator, "seed", &seed));
  CHECK(seed == 7);
  int calls = host.callbacks;
  OK(ccl_session_destroy(s));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(calls == host.callbacks);
  CHECK(ccl_session_wait(s) == CCL_ERROR_INVALID_ARGUMENT);
  printf(
      "PASS pause accumulation, blocking edit/reset/resume, wait pin/cancel/destruction and "
      "control errors\n");
  // Return-code and C++ exception failures must not escape into native worker threads.
  p.background = 1;
  for (bool throws : {false, true}) {
    OK(ccl_session_create(&p, &s));
    OK(ccl_session_scene(s, &scene));
    OK(ccl_node_create(scene, "pass", "combined", &pass));
    OK(ccl_node_set_string(pass, "name", "Combined"));
    host.fail = !throws;
    host.throws = throws;
    OK(ccl_session_set_output_callback(s, &request, 1, pixels, &host));
    OK(ccl_session_reset(s, &bp));
    CHECK(ccl_session_pause(s, 1) == CCL_ERROR_UNSUPPORTED);
    OK(ccl_session_start(s));
    CHECK(ccl_session_wait(s) == CCL_ERROR_INTERNAL);
    ccl_progress report{};
    report.size = sizeof(report);
    OK(ccl_session_progress(s, &report));
    CHECK(report.error && report.cancelled &&
          strstr(report.error_message, throws ? "exception" : "failed"));
    OK(ccl_session_destroy(s));
  }
  printf("PASS callback reentry, host errors and exceptions contained\n");
}
