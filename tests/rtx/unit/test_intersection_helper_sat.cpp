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

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_intersection_test_helpers.h"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_intersection_helper_sat.log");
}

class SatTestApp {
  struct TestCamera {
    TestCamera() { }
    TestCamera(
      const float n_nearPlane,
      const float n_farPlane,
      const float n_fov,
      const float n_aspectRatio,
      const dxvk::Matrix4& n_worldToView,
      const bool n_isLHS,
      const bool n_isInfFrustum)
      : nearPlane(n_nearPlane)
      , farPlane(n_farPlane)
      , fov(n_fov)
      , aspectRatio(n_aspectRatio)
      , worldToView(n_worldToView)
      , isLHS(n_isLHS)
      , isInfFrustum(n_isInfFrustum) {
      float4x4 frustumMatrix;
      if (isInfFrustum) {
        frustumMatrix.SetupByHalfFovyInf((float) (fov * 0.5), aspectRatio, nearPlane, (isLHS ? PROJ_LEFT_HANDED : 0));
      } else {
        frustumMatrix.SetupByHalfFovy((float) (fov * 0.5), aspectRatio, nearPlane, farPlane, (isLHS ? PROJ_LEFT_HANDED : 0));
      }
      frustum.Setup(NDC_OGL, frustumMatrix);

      calculateFrustumGeometry();
    }

    ~TestCamera() { }

    void calculateFrustumGeometry() {
      // Calculate frustum near and far plane extents
      const float tanHalfFov = std::tan(fov * 0.5f);
      nearPlaneUpExtent = nearPlane * tanHalfFov;
      nearPlaneRightExtent = nearPlaneUpExtent * aspectRatio;
      farPlaneUpExtent = farPlane * tanHalfFov;
      farPlaneRightExtent = farPlaneUpExtent * aspectRatio;

      const float zNear = isLHS ? nearPlane : -nearPlane;
      const float zFar = isLHS ? farPlane : -farPlane;

      // Near Plane Vertices
      nearPlaneFrustumVertices[0] = dxvk::Vector3(-nearPlaneRightExtent, -nearPlaneUpExtent, zNear);
      nearPlaneFrustumVertices[1] = dxvk::Vector3(-nearPlaneRightExtent, nearPlaneUpExtent, zNear);
      nearPlaneFrustumVertices[2] = dxvk::Vector3(nearPlaneRightExtent, nearPlaneUpExtent, zNear);
      nearPlaneFrustumVertices[3] = dxvk::Vector3(nearPlaneRightExtent, -nearPlaneUpExtent, zNear);

      // Far Plane Vertices
      farPlaneFrustumVertices[0] = dxvk::Vector3(-farPlaneRightExtent, -farPlaneUpExtent, zFar);
      farPlaneFrustumVertices[1] = dxvk::Vector3(-farPlaneRightExtent, farPlaneUpExtent, zFar);
      farPlaneFrustumVertices[2] = dxvk::Vector3(farPlaneRightExtent, farPlaneUpExtent, zFar);
      farPlaneFrustumVertices[3] = dxvk::Vector3(farPlaneRightExtent, -farPlaneUpExtent, zFar);

      // Edge Vectors (Normalized)
      for (int i = 0; i < 4; ++i) {
        frustumEdgeVectors[i] = dxvk::normalize(farPlaneFrustumVertices[i] - nearPlaneFrustumVertices[i]);
      }
    }

    bool isLHS = false;
    bool isInfFrustum = false;
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    float fov = 0.0f;
    float aspectRatio = 0.0f;

    float nearPlaneUpExtent = 0.0f;
    float nearPlaneRightExtent = 0.0f;
    float farPlaneUpExtent = 0.0f;
    float farPlaneRightExtent = 0.0f;

    cFrustum frustum;
    dxvk::Vector3 nearPlaneFrustumVertices[4];
    dxvk::Vector3 farPlaneFrustumVertices[4];
    dxvk::Vector3 frustumEdgeVectors[4];

    dxvk::Matrix4 worldToView;
  };

  static constexpr uint32_t TestCount = 6;

  const bool testResult[TestCount] = { true, false, true, true, true, false };

  struct TestData {
    TestCamera camera;
    dxvk::Vector3 minPos;
    dxvk::Vector3 maxPos;

    dxvk::Matrix4 objectToWorld;
  };
public:
  void run() {
    const dxvk::Matrix4 worldToView_01(
      -0.994860888f, -0.0304994211f, 0.0965413973f, 0.0f,
      -0.101251513f,  0.299676329f, -0.948580980f,  0.0f,
       0.0f,          0.953481019f,  0.301224351f,  0.0f,
      -17947.5938f,   21581.3145f,  -68876.1016f,   1.0f);

    const TestCamera camera_01(1.0f,                        // Near Plane
                               4833.14746f,                 // Far Plane
                               60.0f * 3.1415926f / 180.0f, // Fov
                               4.0f / 3.0f,                 // Aspect Ratio (4:3)
                               worldToView_01,              // View Matrix
                               true,                        // isLHS
                               true);                       // isInfFar

    const TestCamera camera_02(4.0f,
                               8000.39697f,
                               64.4f * 3.1415926f / 180.0f,
                               16.0f / 9.0f,
                               dxvk::Matrix4(),
                               true,
                               false);

    const dxvk::Matrix4 worldToView_03(
       0.34739398956298828f, -0.0097213806584477425f, -0.93766885995864868f,  0.0f,
      -0.93771928548812866f, -0.003601450240239501f,  -0.3473753035068512f,   0.0f,
       0.0f,                  0.9999462366104126f,    -0.010367047972977161f, 0.0f,
       150.36508178710938f,  -198.93931579589844f,    -665.31854248046875f,   1.0f);

    const TestCamera camera_03(7.0f,
                               29996.916f,
                               59.84f * 3.1415926f / 180.0f,
                               16.0f / 9.0f,
                               worldToView_03,
                               false,
                               false);

    TestData testData[TestCount] = {
      {
        /*
          Test case when vertices of bbox are all outside of the frustum
           __________
          _\________/_
         |  \      /  |
         |___\____/___|
              \__/
        */
        camera_01,
        dxvk::Vector3(-1586.83081f, -1586.83081f, -800.000122f),
        dxvk::Vector3( 1586.83081f,  1586.83081f, -100.000153f),
        dxvk::Matrix4(1.0f,  0.0f,  0.0f, 0.0f,
                      0.0f, -1.0f,  0.0f, 0.0f,
                      0.0f,  0.0f, -1.0f, 0.0f,
                     -10546.7383f, -73629.0859f, 169.809219f, 1.0f)
      },
      {
        /*
          Test case when the bbox is outside frustum
          __________
          \        /  ___
           \      /  |   |
            \    /   |___|
             \__/
        */
        camera_01,
        dxvk::Vector3(-122.215591f, -132.705475f, -512.826904f),
        dxvk::Vector3( 120.290161f,  133.867371f,  129.717026f),
        dxvk::Matrix4(0.495844066f, -0.868411601f, 0.0f,   0.0f,
                      0.868411601f,  0.495844066f, 0.0f,   0.0f,
                      0.0f,          0.0f,         1.0f,   0.0f,
                     -11111.7686f,  -74203.7188f,  120.0f, 1.0f)
      },
      {
        /*
          Test case when all vertices of bbox are outside of the frustum, and the bbox has 1 edge with extent == 0 (the bbox becomes rectangle in such case):
           __________
           \        /
            \      /
          ___\____/___
              \__/
        */
        camera_01,
        dxvk::Vector3(-1228.0f, -1228.0f, 0.0f),
        dxvk::Vector3( 1228.0f,  1228.0f, 0.0f),
        dxvk::Matrix4(1.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 1.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 1.0f, 0.0f,
                     -15980.0f, -80692.0f, -1.0f, 1.0f)
      },
      {
        /*
          Test case when the frustum is completely inside bbox
          ____________
         | __________ |
         | \        / |
         |  \      /  |
         |   \    /   |
         |    \__/    |
         |____________|
        */
        camera_02,
        dxvk::Vector3(40.0f, -288.0f, -146.0f),
        dxvk::Vector3(408.0f, 260.0f, -10.0f),
        dxvk::Matrix4(-0.47072017192840576f,  0.038728754967451096f,  0.88143211603164673f,  0.0f,
                      -0.882282555103302f,   -0.020662775263190269f, -0.4702664315700531f,   0.0f,
                      -0.0f,                  0.99903607368469238f,  -0.043896090239286423f, 0.0f,
                      -26.3145751953125f,     74.528823852539062f,   -260.11444091796875f,   1.0f)
      },
      {
        /*
          Test case when more than 1 vertices of bbox are inside the frustum
           __________
           \    ____/_
            \  |   /  |
             \ |__/___|
              \__/
        */
        camera_02,
        dxvk::Vector3(0.0f, -41.4266052f, -180.061768f),
        dxvk::Vector3(373.333191f, 979.080444f, 247.927338f),
        dxvk::Matrix4(-0.82927942276000977f, -0.28241223096847534f, -0.48222297430038452f, 0.0f,
                       0.55883419513702393f, -0.41908431053161621f, -0.71559256315231323f, 0.0f,
                       0.0f,                  0.86290884017944336f, -0.50535959005355835f, 0.0f,
                       197.89332580566406f,   230.06362915039062f,   222.90849304199219,   1.0f)
      },
      {
        /*
          Test case when a thin bbox is outside frustum
          __________
          \        /_
           \      /| |
            \    / |_|
             \__/
        */
        camera_03,
        dxvk::Vector3(-2.0000062f, -48.0f, -96.0f),
        dxvk::Vector3(2.0000062f, 48.0f, 96.0f),
        dxvk::Matrix4(0.0f, 1.0f, 0.0f, 0.0f,
                     -1.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 1.0f, 0.0f,
                     -415.920013f, -253.75f, 239.873001f, 1.0f)
      }
    };

    for (uint32_t i = 0; i < TestCount; ++i) {
      const TestCamera& camera = testData[i].camera;
      const dxvk::Matrix4& objectToView = camera.worldToView * testData[i].objectToWorld;

      const bool res = boundingBoxIntersectsFrustumSATInternal(
        testData[i].minPos, testData[i].maxPos, objectToView,
        testData[i].camera.frustum, camera.nearPlane, camera.farPlane, camera.nearPlaneRightExtent, camera.nearPlaneUpExtent, camera.frustumEdgeVectors,
        camera.isLHS, camera.isInfFrustum);

      if (res != testResult[i]) {
        throw dxvk::DxvkError("Error: SAT unit test failed on test No." + std::to_string(i));
      }
    }
  }
};

int main() {
  try {
    SatTestApp satTestApp;
    satTestApp.run();
  }
  catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    return -1;
  }

  return 0;
}