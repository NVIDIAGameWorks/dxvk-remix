// USD includes create these warnings, and to avoid erroring out during build (warnings as errors are enabled) we must disable them
#pragma warning(disable : 4305)
#pragma warning(disable : 4244)
#pragma warning(disable : 4273)
#pragma warning(disable : 4005)

#include <windows.h>
#include <GL/gl.h>
#include <iostream>

#include <GLFW/glfw3.h>

#include <pxr/imaging/garch/glApi.h>

#include <pxr/pxr.h>
#include <pxr/base/vt/value.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/imaging/glf/contextCaps.h>

#include <pxr/imaging/glf/drawTarget.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hdx/taskController.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hdx/selectionTracker.h>
#include <pxr/imaging/hgiGL/hgi.h>
#include <pxr/imaging/hgiGL/texture.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include <iostream>
#include <filesystem>

// OGL declarations
typedef void (GLAPIENTRY* PFNGLBLITFRAMEBUFFERPROC) (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
typedef void (GLAPIENTRY* PFNGLDELETEFRAMEBUFFERSPROC) (GLsizei n, const GLuint* framebuffers);
typedef void (GLAPIENTRY* PFNGLBINDFRAMEBUFFERPROC) (GLenum target, GLuint framebuffer);
typedef void (GLAPIENTRY* PFNGLFRAMEBUFFERTEXTURE2DPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (GLAPIENTRY* PFNGLGENFRAMEBUFFERSPROC) (GLsizei n, GLuint* framebuffers);

static PFNGLGENFRAMEBUFFERSPROC        glGenFramebuffers = nullptr;
static PFNGLBINDFRAMEBUFFERPROC        glBindFramebuffer = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC   glFramebufferTexture2D = nullptr;
static PFNGLBLITFRAMEBUFFERPROC        glBlitFramebuffer = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC     glDeleteFramebuffers = nullptr;

// GL enums missing from <GL/gl.h>
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

PXR_NAMESPACE_USING_DIRECTIVE

static const int WIDTH = 1920;
static const int HEIGHT = 1080;

struct CameraController {
  pxr::GfVec3f position = pxr::GfVec3f(0.0f);
  float yaw = 0.0f;    // Y-axis rotation (left/right)
  float pitch = 0.0f;  // X-axis rotation (up/down)
  float fov = 75.0f;
  float moveSpeed = 10.1f;
  float mouseSensitivity = 0.002f;

  GLFWwindow* window = nullptr;

  double lastX = 0.0, lastY = 0.0;
  bool firstMouse = true;

  CameraController(const pxr::UsdGeomCamera& usdCamera, GLFWwindow* win)
    : window(win) {
    using namespace pxr;

    // Try to extract the camera transform
    GfMatrix4d camXform(1.0);
    UsdGeomXformable xformable(usdCamera);
    bool resets = false;
    std::vector<UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&resets);
    if (!ops.empty()) {
      // Compose the transform stack (don't just take the first op)
      camXform = xformable.ComputeLocalToWorldTransform(UsdTimeCode::Default());
    }

    // Set initial position
    position = GfVec3f(camXform.ExtractTranslation());

    // Get camera forward direction (-Z in camera space)
    GfVec3f forward = -GfVec3f(camXform.GetRow3(2)).GetNormalized();

    // Convert forward direction to yaw/pitch
    yaw = std::atan2(forward[0], forward[2]); // left/right
    pitch = std::asin(forward[1]);            // up/down

    // Compute FOV from USDA settings if available
    float focalLength = 50.0f; // mm
    float horizAperture = 20.955f; // mm

    usdCamera.GetFocalLengthAttr().Get(&focalLength);
    usdCamera.GetHorizontalApertureAttr().Get(&horizAperture);

    // FOV in degrees
    fov = 2.0f * std::atan(horizAperture / (2.0f * focalLength)) * 180.0f / M_PI;
  }

  void installCallbacks() {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetWindowUserPointer(window, this);

    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
      auto* self = static_cast<CameraController*>(glfwGetWindowUserPointer(win));
      if (!self) {
        return;
      }

      if (self->firstMouse) {
        self->lastX = xpos;
        self->lastY = ypos;
        self->firstMouse = false;
      }

      float dx = float(xpos - self->lastX);
      float dy = float(ypos - self->lastY);
      self->lastX = xpos;
      self->lastY = ypos;

      self->yaw -= dx * self->mouseSensitivity;
      self->pitch -= dy * self->mouseSensitivity;

      self->pitch = std::clamp(self->pitch, -1.5f, 1.5f);
    });
  }

  void update(float deltaTime) {
    using namespace pxr;
    GfVec3f forward(
      std::sin(yaw) * std::cos(pitch),
      std::sin(pitch),
      std::cos(yaw) * std::cos(pitch)
    );
    forward.Normalize();

    GfVec3f right = GfCross(forward, GfVec3f(0, 1, 0)).GetNormalized();
    GfVec3f up = GfCross(right, forward).GetNormalized();

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      position += forward * moveSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      position -= forward * moveSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
      position -= right * moveSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
      position += right * moveSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
      position += up * moveSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
      position -= up * moveSpeed * deltaTime;
    }
  }

  void updateCamera(pxr::HdxTaskController& taskController, int width, int height) {
    using namespace pxr;

    update(1.0f);

    GfVec3f forward(
      std::sin(yaw) * std::cos(pitch),
      std::sin(pitch),
      std::cos(yaw) * std::cos(pitch)
    );
    forward.Normalize();

    GfVec3f up = GfVec3f(0, 1, 0);
    GfMatrix4d viewMatrix = GfMatrix4d().SetLookAt(position, position + forward, up);

    double aspect = static_cast<double>(width) / static_cast<double>(height);
    GfFrustum frustum;
    frustum.SetPerspective(fov, aspect, 0.1, 10000.0);
    GfMatrix4d projMatrix = frustum.ComputeProjectionMatrix();

    taskController.SetFreeCameraMatrices(viewMatrix, projMatrix);
  }
};

UsdGeomCamera findFirstCamera(const UsdStageRefPtr& stage) {
  for (const auto& prim : stage->Traverse()) {
    if (prim.IsA<UsdGeomCamera>()) {
      return UsdGeomCamera(prim);
    }
  }
  return UsdGeomCamera(); // Invalid camera
}

// Dummy delegate to pass some basic MDL data through to Hydra
class MdlImagingDelegate : public UsdImagingDelegate {
public:
  MdlImagingDelegate(HdRenderIndex* renderIndex, SdfPath const& delegateID)
    : UsdImagingDelegate(renderIndex, delegateID) { }

  VtValue GetMaterialResource(SdfPath const& materialId) override {
    UsdPrim materialPrim = _GetUsdPrim(materialId);
    if (!materialPrim.IsValid() || !materialPrim.IsA<UsdShadeMaterial>()) {
      return VtValue();
    }

    UsdShadeMaterial material(materialPrim);
    // Get the custom 'mdl:surface' output
    UsdShadeOutput mdlOutput = material.GetOutput(TfToken("mdl:surface"));
    UsdShadeShader surfaceShader;
    if (mdlOutput) {
      UsdShadeConnectableAPI  source;
      TfToken                 sourceName;
      UsdShadeAttributeType   sourceType;
      // See if its hooked up to a Shader prim
      if (mdlOutput.GetConnectedSource(&source, &sourceName, &sourceType)) {
        surfaceShader = UsdShadeShader(source.GetPrim());
      }
    }

    struct MyMdlMaterial {
      std::string mdlFilePath;
      std::string subIdentifier;
    } mdl;

    // Extract MDL asset path
    VtValue pathVal;
    if (surfaceShader.GetPrim().GetAttribute(TfToken("info:mdl:sourceAsset")).Get(&pathVal) && pathVal.IsHolding<SdfAssetPath>()) {
      mdl.mdlFilePath = pathVal.UncheckedGet<SdfAssetPath>().GetResolvedPath();
    }

    // Extract optional subIdentifier
    surfaceShader.GetPrim().GetAttribute(TfToken("info:mdl:sourceAsset:subIdentifier")).Get(&mdl.subIdentifier);

    // Extract texture inputs
    HdMaterialNode shaderNode;

    for (const auto& attr : surfaceShader.GetPrim().GetAttributes()) {
      const std::string fullName = attr.GetName().GetString();
      const std::string prefix = "inputs:";
      // Only consider attributes that start with "inputs:"
      if (fullName.rfind(prefix, 0) == 0) {
        VtValue val;
        if (attr.HasValue() && attr.Get(&val)) {
          // Strip the 'inputs:' prefix
          std::string strippedName = fullName.substr(prefix.size());
          // Use a TfToken for the parameter key
          TfToken paramKey(strippedName);
          shaderNode.parameters[paramKey] = val;
        }
      }
    }

    shaderNode.identifier = shaderNode.subIdentifier = TfToken(mdl.subIdentifier); // e.g. "AperturePBR_Translucent"
    shaderNode.parameters[TfToken("file")] = VtValue(mdl.mdlFilePath);

    HdMaterialNetworkMap netMap;
    HdMaterialNetwork network;
    network.nodes.push_back(shaderNode);
    netMap.map[HdMaterialTerminalTokens->surface] = network;

    return VtValue(netMap);
  }
};


int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: hdremix_test_renderer.exe <scene.usd> <pluginName> <numberOfFrames (-1 for infinite)> <screenshotFilename>\n";
    return 1;
  }

  const std::string usdFile = argv[1];
  const TfToken pluginName(argv[2]);
  int numberOfFrames = -1;
  if(argc >= 4) {
    numberOfFrames = std::atoi(argv[3]);
  }
  std::string screenshotFilename;
  if (argc >= 5) {
    screenshotFilename = argv[4];
  }

  if (!glfwInit()) {
    std::cerr << "Failed to init GLFW\n";
    return 1;
  }

  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "HdRemix Test Render", NULL, NULL);

  glfwMakeContextCurrent(window);

#define LOAD_GL_FUNC(type, name) \
  name = reinterpret_cast<type>(wglGetProcAddress(#name)); \
  if (!name) { std::cerr << "Failed to load: " << #name << "\n"; return 1; }

  LOAD_GL_FUNC(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers);
  LOAD_GL_FUNC(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
  LOAD_GL_FUNC(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D);
  LOAD_GL_FUNC(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer);
  LOAD_GL_FUNC(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers);
#undef LOAD_GL_FUNC

  GlfContextCaps::InitInstance();

  // Allow plugins at root (in this case where we drop the HdRemix.dll)
  pxr::PlugRegistry::GetInstance().RegisterPlugins(std::filesystem::current_path().string());

  // Allow plugins stored within the usd dir heirarchy (where plugins produced by build system will go)
  std::filesystem::path p = std::filesystem::path("usd") / "plugins";
  std::string pluginsDir = std::filesystem::absolute(p).string();
  const auto& plugins = pxr::PlugRegistry::GetInstance().RegisterPlugins(pluginsDir);

  if (plugins.empty()) {
    std::cout << "No USD plugins loaded" << std::endl;
  }

  for (auto const& notice : plugins) {
    if (!notice->IsLoaded() && !notice->Load()) {
      std::cout << "Error: USD plugin, " << notice->GetName() << " failed to load!" << std::endl;
    } else {
      std::cout << "USD plugin, " << notice->GetName() << " loaded!" << std::endl;
    }
  }

  HgiUniquePtr hgi = std::make_unique<HgiGL>();
  HdDriver hgiDriver = { HgiTokens->renderDriver, VtValue(hgi.get()) };

  // Create Render Index
  HdDriverVector drivers;
  drivers.push_back(&hgiDriver);

  // Create Render Delegate
  HdPluginRenderDelegateUniqueHandle renderDelegate = HdRendererPluginRegistry::GetInstance().CreateRenderDelegate(pluginName);
  if (!renderDelegate) {
    std::cerr << "Could not create render delegate: " << pluginName << "\n";
    return 1;
  }

  renderDelegate->SetDrivers(drivers);

  HdRenderIndex* renderIndex = HdRenderIndex::New(renderDelegate.Get(), drivers);
  if (!renderIndex) {
    std::cerr << "Failed to create render index\n";
    return 1;
  }

  // Load USD Stage
  UsdStageRefPtr stage = UsdStage::Open(usdFile);
  if (!stage) {
    std::cerr << "Failed to load stage: " << usdFile << "\n";
    return 1;
  }

  // Setup Scene Delegate (UsdImaging)
  UsdImagingDelegate* sceneDelegate = new MdlImagingDelegate(renderIndex, SdfPath("/"));
  sceneDelegate->Populate(stage->GetPseudoRoot());

  pxr::UsdGeomCamera usdCamera = findFirstCamera(stage);
  CameraController camCtrl(usdCamera, window);
  camCtrl.installCallbacks();

  HdxTaskController taskController(renderIndex, SdfPath("/hdxtc"));
  taskController.SetRenderOutputs({ HdAovTokens->color });
  taskController.SetCameraPath(usdCamera.GetPath());
  taskController.SetRenderViewport(GfVec4d(0, 0, WIDTH, HEIGHT));
  taskController.SetFraming(CameraUtilFraming(GfRect2i(GfVec2i(0), GfVec2i(WIDTH, HEIGHT))));

  HdAovDescriptor colorDesc;
  colorDesc.format = HdFormatUNorm8Vec4;
  colorDesc.clearValue = VtValue(GfVec4f(0, 0, 0, 1));
  colorDesc.multiSampled = false;
  taskController.SetRenderOutputSettings(HdAovTokens->color, colorDesc);

  HdxSelectionTrackerSharedPtr selectionTracker = std::make_shared<HdxSelectionTracker>();

  HdEngine engine;
  engine.SetTaskContextData(HdxTokens->selectionState, VtValue(selectionTracker));

  // Render loop with presentation to window
  int frameIdx = 0;
  while (!glfwWindowShouldClose(window) && frameIdx++ != numberOfFrames) {
    glViewport(0, 0, WIDTH, HEIGHT);

    camCtrl.updateCamera(taskController, WIDTH, HEIGHT);

    HdTaskSharedPtrVector tasks = taskController.GetRenderingTasks();
    engine.Execute(renderIndex, &tasks);

    HdRenderBuffer* renderBuffer = static_cast<HdRenderBuffer*>(
    renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, SdfPath("/hdxtc/aov_color")));

    if (!renderBuffer) {
      std::cerr << "HdRenderBuffer not found!\n";
      return 1;
    }

    renderBuffer->Resolve(); // Ensure render contents are finalized

    // Extract GL texture from Hgi handle
    HgiTextureHandle texHandle = renderBuffer->GetResource(false).Get<HgiTextureHandle>();
    HgiGLTexture* glTexture = static_cast<HgiGLTexture*>(texHandle.Get());
    GLuint colorTex = glTexture->GetTextureId();

    // Create an FBO and bind colorTex as read target
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    // Set the draw target as the default framebuffer (GLFW backbuffer)
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // Blit from colorTex (read FBO) to GLFW backbuffer
    glBlitFramebuffer(
      0, 0, WIDTH, HEIGHT,   // source rect
      0, 0, WIDTH, HEIGHT,   // destination rect
      GL_COLOR_BUFFER_BIT,   // what to copy
      GL_NEAREST             // filter
    );

    // Cleanup
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  // Readback framebuffer
  std::vector<unsigned char> pixels(WIDTH * HEIGHT * 4);
  glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  // Save image to disk (PPM format)
  if (!screenshotFilename.empty()) {
    FILE* f = fopen((screenshotFilename + std::string(".ppm")).c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int y = HEIGHT - 1; y >= 0; --y) {
      for (int x = 0; x < WIDTH; ++x) {
        unsigned char* p = &pixels[4 * (y * WIDTH + x)];
        fwrite(p, 1, 3, f);
      }
    }
    fclose(f);
  }

  std::cout << "Image saved to output.ppm\n";

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}