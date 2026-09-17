# SPDX-License-Identifier: Apache-2.0
"""Run in Gaffer's Python Editor with the current ScriptNode named root."""
import CyclesDeep
import Gaffer
import GafferUI
import GafferSceneUI
import IECore
import imath

with Gaffer.UndoScope(root):
    if 'DeepToPointCloud' not in root:
        root['DeepToPointCloud']=CyclesDeep.DeepToPointCloud()
    live=root['DeepToPointCloud']
    live['in'].setInput(root['MERGE_separate_deep_objects']['out'])
    live['pixelStride'].setValue(1)
    if '__uiPosition' not in live:
        live.addChild(Gaffer.V2fPlug('__uiPosition',defaultValue=imath.V2f(0,-45),flags=Gaffer.Plug.Flags.Default|Gaffer.Plug.Flags.Dynamic))
    # Preserve the old snapshot nodes, but make the existing display follow the live image.
    root['VIEW_DEEP_POINTCLOUD']['in'].setInput(live['out'])
    Gaffer.Metadata.registerValue(root['DEEP_POINTCLOUD_samples'],'description','Legacy static snapshot retained for comparison. The live point cloud now comes from DeepToPointCloud and the connected deep image.')
    root.selection().clear(); root.selection().add(live)

GafferUI.NodeMenu.acquire(root.ancestor(Gaffer.ApplicationRoot)).append(
    '/Cycles Deep/DeepToPointCloud', CyclesDeep.DeepToPointCloud,searchText='DeepToPointCloud DeepToPoints')
viewer=GafferUI.Viewer.acquire(live,floating=False)
GafferSceneUI.ScriptNodeAlgo.expandInVisibleSet(root,IECore.PathMatcher(['/']))
viewer._doPendingUpdate()
viewer.view().frame(IECore.PathMatcher(['/*']),imath.V3f(1,-.3,1))
GafferUI.NodeEditor.acquire(live,floating=False)
graph=GafferUI.GraphEditor.acquire(root)
graph.graphGadget().setNodePosition(live,graph.graphGadget().getNodePosition(root['MERGE_separate_deep_objects'])+imath.V2f(40,-20))
graph.frame([root['MERGE_separate_deep_objects'],live])
root.save()
print('LIVE: DeepMerge -> DeepToPointCloud -> 3D scene. Saved current review including existing edits.')
