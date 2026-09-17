/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* One deterministic lane owns one complete camera-chain record. No atomics,
 * global append counter or partially published chain. */
struct KernelDeepRecord {
  unsigned int x, y, sample, population;
  int count;
  float events[128];
};
