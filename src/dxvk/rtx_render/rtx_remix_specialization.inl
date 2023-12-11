/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

namespace pnext::detail {

  // NOTE: To add a new type:
  //  1) Add the type to 'AllTypes'.
  //  2) Add 'ToEnum' specifying the struct type and corresponding remixapi_StructType.
  //  3) If the new type is an extension
  //       (i.e. can be put linked into 'pNext' chain of a parent type)
  //       (e.g. 'remixapi_LightInfoSphereEXT' is an extension of 'remixapi_LightInfo'):
  //     * Then add 'Root' specifying the extension type and root (parent) type.

  // clang-format off
  using AllTypes = TypeList<
    remixapi_MaterialInfo,
    remixapi_MaterialInfoPortalEXT,
    remixapi_MaterialInfoTranslucentEXT,
    remixapi_MaterialInfoOpaqueEXT,
    remixapi_MaterialInfoOpaqueSubsurfaceEXT,
    remixapi_LightInfoSphereEXT,
    remixapi_LightInfoRectEXT,
    remixapi_LightInfoDiskEXT,
    remixapi_LightInfoCylinderEXT,
    remixapi_LightInfoDistantEXT,
    remixapi_LightInfoDomeEXT,
    remixapi_LightInfo,
    remixapi_MeshInfo,
    remixapi_InstanceInfo,
    remixapi_InstanceInfoBoneTransformsEXT,
    remixapi_InstanceInfoBlendEXT,
    remixapi_InstanceInfoObjectPickingEXT,
    remixapi_CameraInfo,
    remixapi_CameraInfoParameterizedEXT
  >;

  template< typename T > constexpr remixapi_StructType ToEnum                 = REMIXAPI_STRUCT_TYPE_NONE;
  template<> constexpr auto ToEnum< remixapi_MaterialInfo                   > = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
  template<> constexpr auto ToEnum< remixapi_MaterialInfoPortalEXT          > = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT;
  template<> constexpr auto ToEnum< remixapi_MaterialInfoTranslucentEXT     > = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT;
  template<> constexpr auto ToEnum< remixapi_MaterialInfoOpaqueEXT          > = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
  template<> constexpr auto ToEnum< remixapi_MaterialInfoOpaqueSubsurfaceEXT> = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT;
  template<> constexpr auto ToEnum< remixapi_LightInfoSphereEXT             > = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
  template<> constexpr auto ToEnum< remixapi_LightInfoRectEXT               > = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT;
  template<> constexpr auto ToEnum< remixapi_LightInfoDiskEXT               > = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT;
  template<> constexpr auto ToEnum< remixapi_LightInfoCylinderEXT           > = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT;
  template<> constexpr auto ToEnum< remixapi_LightInfoDistantEXT            > = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
  template<> constexpr auto ToEnum< remixapi_LightInfoDomeEXT               > = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT;
  template<> constexpr auto ToEnum< remixapi_LightInfo                      > = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
  template<> constexpr auto ToEnum< remixapi_MeshInfo                       > = REMIXAPI_STRUCT_TYPE_MESH_INFO;
  template<> constexpr auto ToEnum< remixapi_InstanceInfo                   > = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
  template<> constexpr auto ToEnum< remixapi_InstanceInfoBoneTransformsEXT  > = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT;
  template<> constexpr auto ToEnum< remixapi_InstanceInfoBlendEXT           > = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT;
  template<> constexpr auto ToEnum< remixapi_InstanceInfoObjectPickingEXT   > = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT;
  template<> constexpr auto ToEnum< remixapi_CameraInfo                     > = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
  template<> constexpr auto ToEnum< remixapi_CameraInfoParameterizedEXT     > = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;

  template< typename T > struct Root                                         { using Type = T; /* by default, a root is self */ };
  template<>           struct Root< remixapi_MaterialInfoPortalEXT          >{ using Type = remixapi_MaterialInfo;              };
  template<>           struct Root< remixapi_MaterialInfoTranslucentEXT     >{ using Type = remixapi_MaterialInfo;              };
  template<>           struct Root< remixapi_MaterialInfoOpaqueEXT          >{ using Type = remixapi_MaterialInfo;              };
  template<>           struct Root< remixapi_MaterialInfoOpaqueSubsurfaceEXT>{ using Type = remixapi_MaterialInfo;              };
  template<>           struct Root< remixapi_LightInfoSphereEXT             >{ using Type = remixapi_LightInfo;                 };
  template<>           struct Root< remixapi_LightInfoRectEXT               >{ using Type = remixapi_LightInfo;                 };
  template<>           struct Root< remixapi_LightInfoDiskEXT               >{ using Type = remixapi_LightInfo;                 };
  template<>           struct Root< remixapi_LightInfoCylinderEXT           >{ using Type = remixapi_LightInfo;                 };
  template<>           struct Root< remixapi_LightInfoDistantEXT            >{ using Type = remixapi_LightInfo;                 };
  template<>           struct Root< remixapi_LightInfoDomeEXT               >{ using Type = remixapi_LightInfo;                 };
  template<>           struct Root< remixapi_InstanceInfoBoneTransformsEXT  >{ using Type = remixapi_InstanceInfo;              };
  template<>           struct Root< remixapi_InstanceInfoBlendEXT           >{ using Type = remixapi_InstanceInfo;              };
  template<>           struct Root< remixapi_InstanceInfoObjectPickingEXT   >{ using Type = remixapi_InstanceInfo;              };
  template<>           struct Root< remixapi_CameraInfoParameterizedEXT     >{ using Type = remixapi_CameraInfo;                };
  // clang-format on
}
