# Anti-Culling System

## Anti-Culling objects

In most rasterization-based games, aggressive geometry culling, especially frustum culling, is employed to significantly reduce the number of draw call commands. However, in a path tracing-based renderer, this approach leads to a situation where the bounding volume hierarchy (BVH) only includes geometries that pass the culling rules of the original game, such as those within the frustum.

This missing of geometries that outside the frustum can result in several issues, including incorrect indirect bounces, light leaks, and missing shadows. These problems are even worse when the player's camera changes location or direction, or when the game scene involves many dynamic objects, causing severe flickering due to inconsistent geometries.

To address this issue, Remix introduces an anti-culling system that aggressively preserves geometries that are outside of the camera frustum, without requiring modifications to the original game code. Remix transforms drawcalls into "instances" and uses them to build or update the BVH every frame. When anti-culling is enabled, the system performs a robust bounding box and frustum intersection check for the instances from the previous frame. Instances outside the frustum become candidates to be preserved in the current frame (no need to handle instances inside the frustum, as the original game provides drawcalls for them). These candidates' hashes and locations are checked to avoid duplication issues caused by objects outside the frustum. The final surviving candidates are combined with all new instances in the current frame to create an anti-culling BVH, ensuring the correct retention of all necessary geometries for scene rendering and the elimination of flickering artifacts.

User Instructions:
1. When you notice issues caused by game culling, such as incorrect shadows or light leaks, consider enabling this system using [rtx.antiCulling.object.enable](../RtxOptions.md).  It's a very general and robust solution, so you don't need to do lots of setup in most cases.
2. Unless you experience a significant performance drop, please use the default settings. In such worst cases, consider disabling high-precision intersection checks. [rtx.antiCulling.object.enableHighPrecisionAntiCulling](../RtxOptions.md)
3. In very rare cases where the game employs a different and extremely aggressive culling mechanism, which may cause the anti-culling system to miss preserving some geometries, you can either reduce the camera field of view (FOV) by using [rtx.antiCulling.object.fovScale](../RtxOptions.md), or, if the issue pertains to specific geometries, add them to [rtx.antiCulling.antiCullingTextures](../RtxOptions.md).

Limitations:
1. The system cannot predict draw calls that have never been sent by the game, primarily causing issues at the beginning of the game when it has no knowledge of geometries outside the frustum.
2. It cannot predict the precise culling method used by a specific game, which could be frustum culling, octree culling, or other custom methods. Our system adopts the very aggressive approach to attempt to encompass the actual culling domain, preserving geometries as comprehensively as possible. In the worst case, users may need to manually configure anti-culling (refer to User Instructions 3), though this is exceptionally rare.

Debugging:
Enable debugging view [Is Inside Frustum](../RtxOptions.md) or [rtx.debugView.debugViewIdx = 700](../src/dxvk/shaders/rtx/utility/debug_view_indices.h), green means inside frustum, red means outside. All pixels that the ray is not missing are expected to be green. Please report bug if find any artifacts or wrong results on the debugging view.

Future plans:
- GPU-based anti-culling: Since each instance is independent, optimizing with GPU is a viable option. However, the lack of indirect BVH generation commands necessitates additional synchronizations and copies, potentially impacting performance compared to the current CPU version.
- Initial frame geometries prediction: Addressing the issue of missing geometries at the beginning of the game remains a challenge. Options include fetching shadow passes or moving the camera around to pre-warm the BVH.

## Anti-Culling Lights

Similar to the Anti-Culling objects, some games also do culling on analytical lights. Current anti-culling system only support spherical lights and rectangle lights, but it's easy to extend to other shapes. Enable this feature by simply setup [rtx.antiCulling.light.enable](../RtxOptions.md)
