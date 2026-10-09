# SPDX-License-Identifier: Apache-2.0
import CyclesDeep
import GafferUI
GafferUI.NodeMenu.acquire(application).append(
    '/Cycles Deep/DeepToPointCloud', CyclesDeep.DeepToPointCloud,
    searchText='DeepToPointCloud DeepToPoints')
