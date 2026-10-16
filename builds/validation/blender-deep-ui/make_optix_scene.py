import bpy
bpy.context.scene.render.resolution_x=64
bpy.context.scene.render.resolution_y=64
bpy.context.scene.render.resolution_percentage=100
bpy.context.scene.cycles.use_denoising=False
bpy.context.scene.cycles.use_adaptive_sampling=False
bpy.ops.wm.save_as_mainfile(filepath=r'D:\temp\cycles_deep_blender_ui\verification\optix-scene.blend')
