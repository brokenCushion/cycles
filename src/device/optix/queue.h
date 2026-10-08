/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#ifdef WITH_OPTIX

#  include "device/cuda/queue.h"

CCL_NAMESPACE_BEGIN

class OptiXDevice;

/* Base class for CUDA queues. */
class OptiXDeviceQueue : public CUDADeviceQueue {
 public:
  OptiXDeviceQueue(OptiXDevice *device);

  void init_execution() override;

  bool enqueue(DeviceKernel kernel,
               const int work_size,
               const DeviceKernelArguments &args) override;
#ifdef WITH_CYCLES_DEEP_OPAQUE
  ~OptiXDeviceQueue() override;

 private:
  unique_ptr<device_only_memory<uint8_t>> deep_launch_params_;
  bool enqueue_deep(const int work_size, const DeviceKernelArguments &args);
#endif
};

CCL_NAMESPACE_END

#endif /* WITH_OPTIX */
