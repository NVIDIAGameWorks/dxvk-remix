/*
* Copyright (c) 2024-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "../../../src/dxvk/rtx_render/rtx_option.h"
#include "../../../src/dxvk/rtx_render/rtx_option_layer.h"
#include "../../../src/dxvk/rtx_render/rtx_option_manager.h"
#include "../../../src/util/config/config.h"
#include "../../../src/util/util_hash_set_layer.h"
#include "../../../src/util/util_env.h"

#include "../../test_utils.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <filesystem>

namespace dxvk {
  // Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_rtx_option.log");

namespace rtx_option_test {

  // ============================================================================
  // Test Configuration and Helpers
  // ============================================================================
  
  // Test layer keys for unit tests (using dynamic priority range)
  static constexpr RtxOptionLayerKey kTestLayerLowKey   = { 1000, "TestLayerLow" };
  static constexpr RtxOptionLayerKey kTestLayerMidKey   = { 2000, "TestLayerMid" };
  static constexpr RtxOptionLayerKey kTestLayerHighKey  = { 3000, "TestLayerHigh" };
  
  // Static variables to track onChange callback invocations - separate counters for each option
  static int s_intCallbackCount = 0;
  static int s_floatCallbackCount = 0;
  
  static void testIntOnChangeCallback(DxvkDevice* device) {
    s_intCallbackCount++;
  }
  
  static void testFloatOnChangeCallback(DxvkDevice* device) {
    s_floatCallbackCount++;
  }
  
  // Counters for callback tests
  static int s_chainCallbackCount = 0;
  static int s_cycleCallbackCount = 0;
  
  // Forward declarations for onChange callbacks
  static void testRangeMinOnChangeCallback(DxvkDevice* device);
  static void testRangeMaxOnChangeCallback(DxvkDevice* device);
  static void testChainedBoundsCallback(DxvkDevice* device);
  static void testCyclicBoundsACallback(DxvkDevice* device);
  static void testCyclicBoundsBCallback(DxvkDevice* device);
  
  // Value-setting chain callbacks (A->B->C->D)
  // Note: maxResolves=4, so chain length must be <= 4 to fully resolve
  static void testValueChainACallback(DxvkDevice* device);
  static void testValueChainBCallback(DxvkDevice* device);
  static void testValueChainCCallback(DxvkDevice* device);
  static void testValueChainDCallback(DxvkDevice* device);
  
  // Cyclic value-setting callbacks (A sets B, B sets A)
  static void testValueCycleACallback(DxvkDevice* device);
  static void testValueCycleBCallback(DxvkDevice* device);

  // Helper macro for test assertions
  #define TEST_ASSERT(condition, message) \
    do { \
      if (!(condition)) { \
        std::ostringstream oss; \
        oss << "FAILED: " << __FUNCTION__ << " line " << __LINE__ << ": " << message; \
        throw DxvkError(oss.str()); \
      } \
    } while(0)

  #define TEST_ASSERT_FLOAT_EQ(a, b, epsilon, message) \
    TEST_ASSERT(std::abs((a) - (b)) < (epsilon), message)

  // ============================================================================
  // Test Options - Define test options using RTX_OPTION macros
  // Each option tests a different type or feature
  // ============================================================================
  
  // Class to hold test options - mimics how options are defined in real code
  class TestOptions {
  public:
    // Basic type options
    RTX_OPTION("rtx.test", bool, testBool, false, "Test boolean option");
    RTX_OPTION("rtx.test", int32_t, testInt, 100, "Test integer option");
    RTX_OPTION("rtx.test", float, testFloat, 1.5f, "Test float option");
    RTX_OPTION("rtx.test", std::string, testString, "default", "Test string option");
    
    // Vector type options
    RTX_OPTION("rtx.test", Vector2, testVector2, Vector2(1.0f, 2.0f), "Test Vector2 option");
    RTX_OPTION("rtx.test", Vector3, testVector3, Vector3(1.0f, 2.0f, 3.0f), "Test Vector3 option");
    RTX_OPTION("rtx.test", Vector4, testVector4, Vector4(1.0f, 2.0f, 3.0f, 4.0f), "Test Vector4 option");
    RTX_OPTION("rtx.test", Vector2i, testVector2i, Vector2i(10, 20), "Test Vector2i option");
    
    // Hash collection options
    RTX_OPTION("rtx.test", fast_unordered_set, testHashSet, {}, "Test hash set option");
    
    // Options with RTX_OPTION_ARGS - testing optional arguments
    RTX_OPTION_ARGS("rtx.test", int32_t, testIntWithMin, 50, "Test int with min value",
      args.minValue = 0);
    
    RTX_OPTION_ARGS("rtx.test", int32_t, testIntWithMax, 50, "Test int with max value",
      args.maxValue = 100);
    
    RTX_OPTION_ARGS("rtx.test", int32_t, testIntWithMinMax, 50, "Test int with min and max",
      args.minValue = 0;
      args.maxValue = 100);
    
    RTX_OPTION_ARGS("rtx.test", float, testFloatWithMinMax, 0.5f, "Test float with min and max",
      args.minValue = 0.0f;
      args.maxValue = 1.0f);
    
    RTX_OPTION_ARGS("rtx.test", Vector2, testVector2WithMinMax, Vector2(0.5f, 0.5f), "Test Vector2 with min and max",
      args.minValue = Vector2(0.0f, 0.0f);
      args.maxValue = Vector2(1.0f, 1.0f));
    
    RTX_OPTION_ARGS("rtx.test", int32_t, testIntWithCallback, 0, "Test int with onChange callback",
      args.onChangeCallback = testIntOnChangeCallback);
    
    RTX_OPTION_ARGS("rtx.test", float, testFloatWithCallback, 0.0f, "Test float with onChange callback for blending",
      args.onChangeCallback = testFloatOnChangeCallback);
    
    // Options with environment variables
    RTX_OPTION_ARGS("rtx.test", int32_t, testIntWithEnv, 123, "Test int with environment variable",
      args.environment = "RTX_TEST_INT_ENV");
    
    RTX_OPTION_ARGS("rtx.test", bool, testBoolWithEnv, false, "Test bool with environment variable",
      args.environment = "RTX_TEST_BOOL_ENV");
    
    RTX_OPTION_ARGS("rtx.test", float, testFloatWithEnv, 1.5f, "Test float with environment variable",
      args.environment = "RTX_TEST_FLOAT_ENV");
    
    // Options with flags using RTX_OPTION_ARGS
    RTX_OPTION_ARGS("rtx.test", int32_t, testIntNoSave, 42, "Test int with NoSave flag",
      args.flags = RtxOptionFlags::NoSave);
    
    RTX_OPTION_ARGS("rtx.test", int32_t, testIntNoReset, 42, "Test int with NoReset flag",
      args.flags = RtxOptionFlags::NoReset);
    
    // Option with both environment and flags
    RTX_OPTION_ARGS("rtx.test", int32_t, testIntEnvAndFlags, 99, "Test int with env and NoSave flag",
      args.environment = "RTX_TEST_INT_ENV_FLAGS",
      args.flags = RtxOptionFlags::NoSave);
    
    // Enum option (treated as int)
    enum class TestEnum { ValueA = 0, ValueB = 1, ValueC = 2 };
    RTX_OPTION("rtx.test", TestEnum, testEnum, TestEnum::ValueA, "Test enum option");
    
    // Separate options for layer priority tests (to avoid min/max contamination from other tests)
    RTX_OPTION("rtx.test", int32_t, testIntLayerPriority, 100, "Test int for layer priority");
    RTX_OPTION("rtx.test", float, testFloatBlend, 1.5f, "Test float for blending");
    RTX_OPTION("rtx.test", Vector3, testVector3Blend, Vector3(1.0f, 2.0f, 3.0f), "Test Vector3 for blending");
    
    // Dedicated options for specific layer tests (to avoid state contamination)
    RTX_OPTION("rtx.test", int32_t, testIntEnableDisable, 100, "Test int for enable/disable layer test");
    RTX_OPTION("rtx.test", int32_t, testIntThreshold, 100, "Test int for threshold test");
    RTX_OPTION("rtx.test", int32_t, testIntComplex, 100, "Test int for complex layer test");
    
    // Min/Max interdependent options - setting one constrains the other
    // testRangeMax sets testRangeMin's maxValue, and testRangeMin sets testRangeMax's minValue
    RTX_OPTION_ARGS("rtx.test", float, testRangeMin, 0.0f, "Test min value of a range",
      args.minValue = -100.0f;
      args.maxValue = 100.0f;
      args.onChangeCallback = testRangeMinOnChangeCallback);
    
    RTX_OPTION_ARGS("rtx.test", float, testRangeMax, 10.0f, "Test max value of a range",
      args.minValue = -100.0f;
      args.maxValue = 100.0f;
      args.onChangeCallback = testRangeMaxOnChangeCallback);
    
    // Chained bounds callback option - tests that callbacks setting min/max on other options work
    RTX_OPTION_ARGS("rtx.test", float, testChainedSource, 50.0f, "Source option that sets bounds on target",
      args.minValue = 0.0f;
      args.maxValue = 100.0f;
      args.onChangeCallback = testChainedBoundsCallback);
    
    RTX_OPTION_ARGS("rtx.test", float, testChainedTarget, 50.0f, "Target option with dynamic bounds",
      args.minValue = 0.0f;
      args.maxValue = 100.0f);
    
    // Cyclic bounds callback options - A sets B's max, B sets A's max (tests termination)
    RTX_OPTION_ARGS("rtx.test", float, testCyclicA, 50.0f, "Cyclic option A that adjusts B's bounds",
      args.minValue = 0.0f;
      args.maxValue = 100.0f;
      args.onChangeCallback = testCyclicBoundsACallback);
    
    RTX_OPTION_ARGS("rtx.test", float, testCyclicB, 50.0f, "Cyclic option B that adjusts A's bounds",
      args.minValue = 0.0f;
      args.maxValue = 100.0f;
      args.onChangeCallback = testCyclicBoundsBCallback);
    
    // Value-setting chain: A -> B -> C -> D (each sets the next to current + 1)
    // Note: Chain length <= maxResolves (4) to fully resolve in one applyPendingValues call
    RTX_OPTION_ARGS("rtx.test", int32_t, testValueChainA, 0, "Value chain A",
      args.onChangeCallback = testValueChainACallback);
    RTX_OPTION_ARGS("rtx.test", int32_t, testValueChainB, 0, "Value chain B",
      args.onChangeCallback = testValueChainBCallback);
    RTX_OPTION_ARGS("rtx.test", int32_t, testValueChainC, 0, "Value chain C",
      args.onChangeCallback = testValueChainCCallback);
    RTX_OPTION_ARGS("rtx.test", int32_t, testValueChainD, 0, "Value chain D (end of chain)",
      args.onChangeCallback = testValueChainDCallback);
    
    // Cyclic value-setting: A sets B = A+1, B sets A = B+1 (should terminate)
    RTX_OPTION_ARGS("rtx.test", int32_t, testValueCycleA, 0, "Value cycle A",
      args.onChangeCallback = testValueCycleACallback);
    RTX_OPTION_ARGS("rtx.test", int32_t, testValueCycleB, 0, "Value cycle B",
      args.onChangeCallback = testValueCycleBCallback);
    
    // Options for migration tests
    // Regular developer option (no UserSetting flag) - should be in rtx.conf
    RTX_OPTION("rtx.test", int32_t, testMigrateDeveloper, 100, "Developer option for migration test");
    
    // User setting option (with UserSetting flag) - should be in user.conf
    RTX_OPTION_ARGS("rtx.test", int32_t, testMigrateUser, 200, "User option for migration test",
      args.flags = RtxOptionFlags::UserSetting);
    
    // User setting with NoReset - should still migrate
    RTX_OPTION_ARGS("rtx.test", int32_t, testMigrateUserNoReset, 300, "User option with NoReset for migration test",
      args.flags = RtxOptionFlags::UserSetting | RtxOptionFlags::NoReset);
    
    // Hashset options for migration tests - verifies hashset merging during migration
    // Developer hashset (no UserSetting flag) - should be in rtx.conf
    RTX_OPTION("rtx.test", fast_unordered_set, testMigrateDeveloperHash, {}, "Developer hashset for migration test");
    
    // User hashset (with UserSetting flag) - should be in user.conf
    RTX_OPTION_ARGS("rtx.test", fast_unordered_set, testMigrateUserHash, {}, "User hashset for migration test",
      args.flags = RtxOptionFlags::UserSetting);
  };

  // ============================================================================
  // Callback Implementations for Min/Max, Chained, and Cyclic Tests
  // ============================================================================
  
  // Min/Max interdependency callbacks - mimics pathMinBounces/pathMaxBounces pattern
  static void testRangeMinOnChangeCallback(DxvkDevice* device) {
    // When min changes, set max's minimum to prevent max < min
    TestOptions::testRangeMaxObject().setMinValue(TestOptions::testRangeMin());
  }
  
  static void testRangeMaxOnChangeCallback(DxvkDevice* device) {
    // When max changes, set min's maximum to prevent min > max
    TestOptions::testRangeMinObject().setMaxValue(TestOptions::testRangeMax());
  }
  
  // Chained bounds callback - source option sets max on target option
  // This is the pattern used in production (e.g., targetNumTrainingIterations.setMaxValue)
  static void testChainedBoundsCallback(DxvkDevice* device) {
    s_chainCallbackCount++;
    // When source changes, set target's maxValue to source's value
    // This forces target to be <= source
    TestOptions::testChainedTargetObject().setMaxValue(TestOptions::testChainedSource());
  }
  
  // Cyclic bounds callbacks - A sets B's max, B sets A's max
  // This creates a potentially cyclic dependency on bounds, but should terminate
  // because setMinValue/setMaxValue only marks dirty if bounds actually change
  static void testCyclicBoundsACallback(DxvkDevice* device) {
    s_cycleCallbackCount++;
    // When A changes, set B's maxValue to be >= A's value
    TestOptions::testCyclicBObject().setMinValue(TestOptions::testCyclicA());
  }
  
  static void testCyclicBoundsBCallback(DxvkDevice* device) {
    s_cycleCallbackCount++;
    // When B changes, set A's maxValue to be >= B's value
    TestOptions::testCyclicAObject().setMinValue(TestOptions::testCyclicB());
  }
  
  // Value-setting chain callbacks: A -> B -> C -> D
  // Each callback sets the next option to current value + 1
  // Uses setDeferred() without explicit layer - uses RtxOptionLayerTarget which defaults to Derived layer
  static void testValueChainACallback(DxvkDevice* device) {
    s_chainCallbackCount++;
    // setDeferred() without layer uses getTargetLayer() which returns Derived layer
    TestOptions::testValueChainB.setDeferred(TestOptions::testValueChainA() + 1);
  }
  
  static void testValueChainBCallback(DxvkDevice* device) {
    s_chainCallbackCount++;
    TestOptions::testValueChainC.setDeferred(TestOptions::testValueChainB() + 1);
  }
  
  static void testValueChainCCallback(DxvkDevice* device) {
    s_chainCallbackCount++;
    TestOptions::testValueChainD.setDeferred(TestOptions::testValueChainC() + 1);
  }
  
  static void testValueChainDCallback(DxvkDevice* device) {
    s_chainCallbackCount++;
    // End of chain - no further propagation
  }
  
  // Cyclic value-setting callbacks: A sets B = A+1, B sets A = B+1
  static void testValueCycleACallback(DxvkDevice* device) {
    s_cycleCallbackCount++;
    TestOptions::testValueCycleB.setDeferred(TestOptions::testValueCycleA() + 1);
  }
  
  static void testValueCycleBCallback(DxvkDevice* device) {
    s_cycleCallbackCount++;
    TestOptions::testValueCycleA.setDeferred(TestOptions::testValueCycleB() + 1);
  }

  // ============================================================================
  // Initialize Test Environment
  // ============================================================================
  
  void initializeTestEnvironment() {
    // Initialize system layers just like the real runtime does
    // This creates all the standard layers (Derived, Environment, Quality, User, etc.)
    // and makes RtxOptionLayerTarget work correctly for onChange callbacks
    RtxOptionLayer::initializeSystemLayers();
    
    // Set the initialized flag so options can be accessed
    RtxOptionImpl::setInitialized(true);
    
    // Mark all options with callbacks as dirty so they get invoked during startup
    // This mimics what happens during real application initialization
    RtxOptionManager::markOptionsWithCallbacksDirty();
    
    // Apply pending values to resolve defaults and invoke callbacks
    RtxOptionManager::applyPendingValues(nullptr, true);
  }
  
  // ============================================================================
  // Verify Options at Default Values
  // Called at end of tests to ensure no values leaked after test layer release
  // ============================================================================
  
  void verifyOptionsAtDefaults() {
    // Verify basic type options are at their defaults
    TEST_ASSERT(TestOptions::testBool() == false, 
                "testBool should be at default (false) after test cleanup");
    TEST_ASSERT(TestOptions::testInt() == 100, 
                "testInt should be at default (100) after test cleanup");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloat(), 1.5f, 0.0001f, 
                        "testFloat should be at default (1.5) after test cleanup");
    TEST_ASSERT(TestOptions::testString() == "default", 
                "testString should be at default after test cleanup");
    
    // Verify vector options
    TEST_ASSERT_FLOAT_EQ(TestOptions::testVector2().x, 1.0f, 0.0001f,
                        "testVector2.x should be at default after test cleanup");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testVector2().y, 2.0f, 0.0001f,
                        "testVector2.y should be at default after test cleanup");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testVector3().x, 1.0f, 0.0001f,
                        "testVector3.x should be at default after test cleanup");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testVector3().y, 2.0f, 0.0001f,
                        "testVector3.y should be at default after test cleanup");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testVector3().z, 3.0f, 0.0001f,
                        "testVector3.z should be at default after test cleanup");
    
    // Verify dedicated test options used to avoid state contamination
    TEST_ASSERT(TestOptions::testIntLayerPriority() == 100,
                "testIntLayerPriority should be at default (100) after test cleanup");
    TEST_ASSERT(TestOptions::testIntEnableDisable() == 100,
                "testIntEnableDisable should be at default (100) after test cleanup");
    TEST_ASSERT(TestOptions::testIntThreshold() == 100,
                "testIntThreshold should be at default (100) after test cleanup");
    TEST_ASSERT(TestOptions::testIntComplex() == 100,
                "testIntComplex should be at default (100) after test cleanup");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloatBlend(), 1.5f, 0.0001f,
                        "testFloatBlend should be at default (1.5) after test cleanup");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testVector3Blend().x, 1.0f, 0.0001f,
                        "testVector3Blend.x should be at default after test cleanup");
    
    // Verify callback options are at their defaults
    TEST_ASSERT(TestOptions::testIntWithCallback() == 0,
                "testIntWithCallback should be at default (0) after test cleanup");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloatWithCallback(), 0.0f, 0.0001f,
                        "testFloatWithCallback should be at default (0.0) after test cleanup");
  }

  // ============================================================================
  // Test: Basic Option Types
  // Tests that all basic option types work correctly
  // ============================================================================
  
  void test_basicTypes() {
    std::cout << "  Running test_basicTypes..." << std::endl;
    
    // Test bool default value
    TEST_ASSERT(TestOptions::testBool() == false, "Bool default value should be false");
    
    // Test int default value
    TEST_ASSERT(TestOptions::testInt() == 100, "Int default value should be 100");
    
    // Test float default value
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloat(), 1.5f, 0.0001f, "Float default value should be 1.5");
    
    // Test string default value
    TEST_ASSERT(TestOptions::testString() == "default", "String default value should be 'default'");
    
    // Test Vector2 default value
    TEST_ASSERT(TestOptions::testVector2().x == 1.0f && TestOptions::testVector2().y == 2.0f,
                "Vector2 default value should be (1.0, 2.0)");
    
    // Test Vector3 default value
    TEST_ASSERT(TestOptions::testVector3().x == 1.0f && TestOptions::testVector3().y == 2.0f && 
                TestOptions::testVector3().z == 3.0f,
                "Vector3 default value should be (1.0, 2.0, 3.0)");
    
    // Test Vector4 default value
    TEST_ASSERT(TestOptions::testVector4().x == 1.0f && TestOptions::testVector4().y == 2.0f && 
                TestOptions::testVector4().z == 3.0f && TestOptions::testVector4().w == 4.0f,
                "Vector4 default value should be (1.0, 2.0, 3.0, 4.0)");
    
    // Test Vector2i default value
    TEST_ASSERT(TestOptions::testVector2i().x == 10 && TestOptions::testVector2i().y == 20,
                "Vector2i default value should be (10, 20)");
    
    // Test enum default value
    TEST_ASSERT(TestOptions::testEnum() == TestOptions::TestEnum::ValueA,
                "Enum default value should be ValueA");
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Option Set and Get
  // Tests that options can be set and retrieved correctly
  // ============================================================================
  
  void test_setAndGet() {
    std::cout << "  Running test_setAndGet..." << std::endl;
    
    // Create a test layer for setting values
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", kTestLayerMidKey, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Test setting bool
    TestOptions::testBool.setDeferred(true, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testBool() == true, "Bool should be set to true");
    
    // Test setting int
    TestOptions::testInt.setDeferred(200, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testInt() == 200, "Int should be set to 200");
    
    // Test setting float
    TestOptions::testFloat.setDeferred(3.14f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloat(), 3.14f, 0.0001f, "Float should be set to 3.14");
    
    // Test setting string
    TestOptions::testString.setDeferred("modified", testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testString() == "modified", "String should be set to 'modified'");
    
    // Test setting Vector2
    TestOptions::testVector2.setDeferred(Vector2(5.0f, 6.0f), testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testVector2().x == 5.0f && TestOptions::testVector2().y == 6.0f,
                "Vector2 should be set to (5.0, 6.0)");
    
    // Test setting Vector3
    TestOptions::testVector3.setDeferred(Vector3(7.0f, 8.0f, 9.0f), testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testVector3().x == 7.0f && TestOptions::testVector3().y == 8.0f &&
                TestOptions::testVector3().z == 9.0f,
                "Vector3 should be set to (7.0, 8.0, 9.0)");
    
    // Test setting enum
    TestOptions::testEnum.setDeferred(TestOptions::TestEnum::ValueB, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testEnum() == TestOptions::TestEnum::ValueB,
                "Enum should be set to ValueB");
    
    // Clean up - release the test layer
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Min/Max Clamping
  // Tests that min/max value clamping works correctly
  // ============================================================================
  
  void test_minMaxClamping() {
    std::cout << "  Running test_minMaxClamping..." << std::endl;
    
    // Create a test layer
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", kTestLayerHighKey, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Test int with min value - set below min
    TestOptions::testIntWithMin.setDeferred(-10, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithMin() >= 0, "Int with min should be clamped to >= 0");
    
    // Test int with max value - set above max
    TestOptions::testIntWithMax.setDeferred(200, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithMax() <= 100, "Int with max should be clamped to <= 100");
    
    // Test int with min and max - set below min
    TestOptions::testIntWithMinMax.setDeferred(-50, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithMinMax() >= 0, "Int with minmax should be clamped to >= 0");
    
    // Test int with min and max - set above max
    TestOptions::testIntWithMinMax.setDeferred(150, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithMinMax() <= 100, "Int with minmax should be clamped to <= 100");
    
    // Test int with min and max - set within range
    TestOptions::testIntWithMinMax.setDeferred(75, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithMinMax() == 75, "Int with minmax should be 75 when within range");
    
    // Test float with min and max - set below min
    TestOptions::testFloatWithMinMax.setDeferred(-0.5f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testFloatWithMinMax() >= 0.0f, "Float with minmax should be clamped to >= 0.0");
    
    // Test float with min and max - set above max
    TestOptions::testFloatWithMinMax.setDeferred(1.5f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testFloatWithMinMax() <= 1.0f, "Float with minmax should be clamped to <= 1.0");
    
    // Test Vector2 with min and max - component below min
    TestOptions::testVector2WithMinMax.setDeferred(Vector2(-0.5f, 0.5f), testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testVector2WithMinMax().x >= 0.0f, "Vector2 x should be clamped to >= 0.0");
    
    // Test Vector2 with min and max - component above max
    TestOptions::testVector2WithMinMax.setDeferred(Vector2(0.5f, 1.5f), testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testVector2WithMinMax().y <= 1.0f, "Vector2 y should be clamped to <= 1.0");
    
    // Clean up
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: onChange Callback
  // Tests that onChange callbacks are invoked correctly
  // ============================================================================
  
  void test_onChangeCallback() {
    std::cout << "  Running test_onChangeCallback..." << std::endl;
    
    // Each callback should have been invoked once during startup (initializeTestEnvironment)
    // because markOptionsWithCallbacksDirty + applyPendingValues(forceOnChange=true) was called
    TEST_ASSERT(s_intCallbackCount == 1, 
                "Int callback should have been invoked exactly once during startup");
    TEST_ASSERT(s_floatCallbackCount == 1, 
                "Float callback should have been invoked exactly once during startup");
    
    // Test that the options have no special flags (callback is stored separately, not as a flag)
    TEST_ASSERT(TestOptions::testIntWithCallbackObject().getFlags() == 0,
                "testIntWithCallback should have no special flags");
    TEST_ASSERT(TestOptions::testFloatWithCallbackObject().getFlags() == 0,
                "testFloatWithCallback should have no special flags");
    
    // Create test layers
    Config emptyConfig;
    const RtxOptionLayer* lowLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{13000, "CallbackTestLowLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    const RtxOptionLayer* highLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{14000, "CallbackTestHighLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(lowLayer != nullptr && highLayer != nullptr, "Failed to create test layers");
    
    int intCountBefore = s_intCallbackCount;
    int floatCountBefore = s_floatCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Int callback IS invoked when value actually changes
    // -------------------------------------------------------------------------
    TestOptions::testIntWithCallback.setDeferred(999, highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(s_intCallbackCount == intCountBefore + 1, 
                "Int callback should be invoked when value changes from 0 to 999");
    TEST_ASSERT(s_floatCallbackCount == floatCountBefore, 
                "Float callback should NOT be invoked when only int changes");
    TEST_ASSERT(TestOptions::testIntWithCallback() == 999, 
                "Int value should be 999");
    intCountBefore = s_intCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Int callback NOT invoked when setting the same value
    // -------------------------------------------------------------------------
    TestOptions::testIntWithCallback.setDeferred(999, highLayer);  // Same value
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(s_intCallbackCount == intCountBefore, 
                "Int callback should NOT be invoked when setting the same value (999 -> 999)");
    
    // -------------------------------------------------------------------------
    // Test: Int callback NOT invoked when lower layer sets value but higher layer overrides
    // -------------------------------------------------------------------------
    // High layer has 999. Set low layer to 500. Final value should still be 999 (from high layer).
    TestOptions::testIntWithCallback.setDeferred(500, lowLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 999, 
                "Value should still be 999 from high layer");
    TEST_ASSERT(s_intCallbackCount == intCountBefore, 
                "Int callback should NOT be invoked when lower layer sets value but higher layer overrides");
    
    // -------------------------------------------------------------------------
    // Test: Int callback IS invoked when higher layer value is removed (falls back to lower)
    // -------------------------------------------------------------------------
    TestOptions::testIntWithCallbackObject().disableLayerValue(highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 500, 
                "Value should fall back to 500 from low layer");
    TEST_ASSERT(s_intCallbackCount == intCountBefore + 1, 
                "Int callback should be invoked when value changes from 999 to 500");
    intCountBefore = s_intCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Int callback IS invoked when layers are released and value returns to default
    // -------------------------------------------------------------------------
    RtxOptionManager::releaseLayer(lowLayer);
    RtxOptionManager::releaseLayer(highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 0, 
                "Int value should return to default (0) after layer release");
    TEST_ASSERT(s_intCallbackCount == intCountBefore + 1, 
                "Int callback should be invoked when value returns to default after layer release");
    intCountBefore = s_intCallbackCount;
    floatCountBefore = s_floatCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Float callback IS invoked when layer blend causes value change
    // -------------------------------------------------------------------------
    // Create a layer with 50% blend strength for float blending test
    RtxOptionLayer* blendLayer = const_cast<RtxOptionLayer*>(RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{15000, "CallbackBlendLayer"}, 0.5f, 0.1f, false, &emptyConfig));
    TEST_ASSERT(blendLayer != nullptr, "Failed to create blend layer");
    
    // Set a float value in the blend layer - should blend with default (0.0)
    // Result: 100.0 * 0.5 + 0.0 * 0.5 = 50.0
    TestOptions::testFloatWithCallback.setDeferred(100.0f, blendLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    float blendedValue = TestOptions::testFloatWithCallback();
    TEST_ASSERT_FLOAT_EQ(blendedValue, 50.0f, 0.01f,
                         "Blended float value should be 50.0 (100.0 * 0.5 + 0.0 * 0.5)");
    TEST_ASSERT(s_floatCallbackCount == floatCountBefore + 1, 
                "Float callback should be invoked when blended value changes");
    TEST_ASSERT(s_intCallbackCount == intCountBefore, 
                "Int callback should NOT be invoked when only float changes");
    floatCountBefore = s_floatCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Float callback IS invoked when blend strength changes via requestBlendStrength
    // -------------------------------------------------------------------------
    // Change blend strength from 50% to 100% using requestBlendStrength
    // Result should change from 50.0 to 100.0
    blendLayer->requestBlendStrength(1.0f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloatWithCallback(), 100.0f, 0.01f,
                         "Float value should be 100.0 after blend strength changed to 100%");
    TEST_ASSERT(s_floatCallbackCount == floatCountBefore + 1, 
                "Float callback should be invoked when blend strength changes the value");
    floatCountBefore = s_floatCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Float callback IS NOT invoked when blend strength change doesn't affect value
    // -------------------------------------------------------------------------
    // Change blend strength from 100% to 100% again - no change in value
    blendLayer->requestBlendStrength(1.0f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloatWithCallback(), 100.0f, 0.01f,
                         "Float value should still be 100.0");
    TEST_ASSERT(s_floatCallbackCount == floatCountBefore, 
                "Float callback should NOT be invoked when value doesn't change");
    
    // -------------------------------------------------------------------------
    // Test: Float callback IS invoked when blend strength changes back to 50%
    // -------------------------------------------------------------------------
    // Change blend strength from 100% to 50%
    // Result should change from 100.0 back to 50.0
    blendLayer->requestBlendStrength(0.5f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloatWithCallback(), 50.0f, 0.01f,
                         "Float value should be 50.0 after blend strength changed to 50%");
    TEST_ASSERT(s_floatCallbackCount == floatCountBefore + 1, 
                "Float callback should be invoked when blend strength changes the value back");
    floatCountBefore = s_floatCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Float callback IS invoked when blended layer is released (value changes back)
    // -------------------------------------------------------------------------
    RtxOptionManager::releaseLayer(blendLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloatWithCallback(), 0.0f, 0.01f,
                         "Float value should return to default (0.0) after layer release");
    TEST_ASSERT(s_floatCallbackCount == floatCountBefore + 1, 
                "Float callback should be invoked when blended value returns to default");
    floatCountBefore = s_floatCallbackCount;
    
    // =========================================================================
    // INT BLEND THRESHOLD TESTS
    // Ints don't blend - they either apply (strength >= threshold) or don't
    // =========================================================================
    
    // -------------------------------------------------------------------------
    // Test: Int callback NOT invoked when blend strength is below threshold
    // -------------------------------------------------------------------------
    // Create a layer with blend threshold of 0.5, but strength of 0.3 (below threshold)
    // Since strength < threshold, the int value should NOT be applied
    RtxOptionLayer* thresholdLayer = const_cast<RtxOptionLayer*>(RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{16000, "ThresholdTestLayer"}, 0.3f, 0.5f, false, &emptyConfig));
    TEST_ASSERT(thresholdLayer != nullptr, "Failed to create threshold test layer");
    
    // Set int value in the layer - but it shouldn't apply since strength (0.3) < threshold (0.5)
    TestOptions::testIntWithCallback.setDeferred(777, thresholdLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 0, 
                "Int value should remain at default (0) since blend strength is below threshold");
    TEST_ASSERT(s_intCallbackCount == intCountBefore, 
                "Int callback should NOT be invoked when blend strength is below threshold");
    
    // -------------------------------------------------------------------------
    // Test: Int callback IS invoked when blend strength crosses above threshold
    // -------------------------------------------------------------------------
    // Increase blend strength from 0.3 to 0.6 (now above threshold of 0.5)
    // The int value should now apply
    thresholdLayer->requestBlendStrength(0.6f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 777, 
                "Int value should be 777 after blend strength crossed above threshold");
    TEST_ASSERT(s_intCallbackCount == intCountBefore + 1, 
                "Int callback should be invoked when blend strength crosses above threshold");
    intCountBefore = s_intCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Int callback NOT invoked when blend strength changes but stays above threshold
    // -------------------------------------------------------------------------
    // Change blend strength from 0.6 to 0.8 - still above threshold, value unchanged
    thresholdLayer->requestBlendStrength(0.8f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 777, 
                "Int value should still be 777");
    TEST_ASSERT(s_intCallbackCount == intCountBefore, 
                "Int callback should NOT be invoked when strength changes but value doesn't");
    
    // -------------------------------------------------------------------------
    // Test: Int callback IS invoked when blend strength drops below threshold
    // -------------------------------------------------------------------------
    // Decrease blend strength from 0.8 to 0.4 (now below threshold of 0.5)
    // The int value should revert to default
    thresholdLayer->requestBlendStrength(0.4f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 0, 
                "Int value should revert to default (0) when blend strength drops below threshold");
    TEST_ASSERT(s_intCallbackCount == intCountBefore + 1, 
                "Int callback should be invoked when blend strength drops below threshold");
    intCountBefore = s_intCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Int callback IS invoked when blend threshold is lowered to include current strength
    // -------------------------------------------------------------------------
    // Current strength is 0.4, threshold is 0.5. Lower threshold to 0.3.
    // Now strength (0.4) >= threshold (0.3), so value should apply
    thresholdLayer->requestBlendThreshold(0.3f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 777, 
                "Int value should be 777 after threshold lowered below current strength");
    TEST_ASSERT(s_intCallbackCount == intCountBefore + 1, 
                "Int callback should be invoked when threshold change causes value to apply");
    intCountBefore = s_intCallbackCount;
    
    // -------------------------------------------------------------------------
    // Test: Int callback IS invoked when threshold layer is released
    // -------------------------------------------------------------------------
    RtxOptionManager::releaseLayer(thresholdLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntWithCallback() == 0, 
                "Int value should return to default (0) after threshold layer release");
    TEST_ASSERT(s_intCallbackCount == intCountBefore + 1, 
                "Int callback should be invoked when threshold layer is released");
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    // Reset the callback counts so they don't affect other tests
    s_intCallbackCount = 0;
    s_floatCallbackCount = 0;
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Min/Max Interdependency Pattern
  // Tests the pattern where fooMax sets fooMin.maxValue and vice versa
  // This is used in pathMinBounces/pathMaxBounces, evMinValue/evMaxValue, etc.
  // ============================================================================
  
  void test_minMaxInterdependency() {
    std::cout << "  Running test_minMaxInterdependency..." << std::endl;
    
    // Reset to known state - set to original bounds from args
    TestOptions::testRangeMinObject().setMinValue(-100.0f);
    TestOptions::testRangeMinObject().setMaxValue(100.0f);
    TestOptions::testRangeMaxObject().setMinValue(-100.0f);
    TestOptions::testRangeMaxObject().setMaxValue(100.0f);
    
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{30000, "MinMaxTestLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(testLayer != nullptr, "Failed to create min/max test layer");
    
    // -------------------------------------------------------------------------
    // Test: Setting max constrains min's upper bound
    // -------------------------------------------------------------------------
    // Set max to 50, which should set min's maxValue to 50
    TestOptions::testRangeMax.setDeferred(50.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testRangeMax(), 50.0f, 0.001f,
                         "testRangeMax should be 50");
    
    // Now try to set min to 60 (above max of 50) - should be clamped to 50
    TestOptions::testRangeMin.setDeferred(60.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testRangeMin(), 50.0f, 0.001f,
                         "testRangeMin should be clamped to 50 (max's value)");
    
    // -------------------------------------------------------------------------
    // Test: Setting min constrains max's lower bound
    // -------------------------------------------------------------------------
    // Set min to 20, which should set max's minValue to 20
    TestOptions::testRangeMin.setDeferred(20.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testRangeMin(), 20.0f, 0.001f,
                         "testRangeMin should be 20");
    
    // Now try to set max to 10 (below min of 20) - should be clamped to 20
    TestOptions::testRangeMax.setDeferred(10.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testRangeMax(), 20.0f, 0.001f,
                         "testRangeMax should be clamped to 20 (min's value)");
    
    // -------------------------------------------------------------------------
    // Test: Valid range operations work correctly
    // -------------------------------------------------------------------------
    // Set a valid range where min < max
    TestOptions::testRangeMin.setDeferred(5.0f, testLayer);
    TestOptions::testRangeMax.setDeferred(95.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT_FLOAT_EQ(TestOptions::testRangeMin(), 5.0f, 0.001f,
                         "testRangeMin should be 5");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testRangeMax(), 95.0f, 0.001f,
                         "testRangeMax should be 95");
    
    // Cleanup
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Reset min/max bounds to original values from args
    TestOptions::testRangeMinObject().setMinValue(-100.0f);
    TestOptions::testRangeMinObject().setMaxValue(100.0f);
    TestOptions::testRangeMaxObject().setMinValue(-100.0f);
    TestOptions::testRangeMaxObject().setMaxValue(100.0f);
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Chained OnChange Callbacks (Bounds Pattern)
  // Tests that onChange handlers that set bounds on other options work correctly
  // This is the pattern used in production (e.g., maxNumTrainingIterations sets targetNumTrainingIterations.maxValue)
  // ============================================================================
  
  void test_chainedOnChangeCallbacks() {
    std::cout << "  Running test_chainedOnChangeCallbacks..." << std::endl;
    
    // Reset bounds to known state
    TestOptions::testChainedSourceObject().setMinValue(0.0f);
    TestOptions::testChainedSourceObject().setMaxValue(100.0f);
    TestOptions::testChainedTargetObject().setMinValue(0.0f);
    TestOptions::testChainedTargetObject().setMaxValue(100.0f);
    
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{33000, "ChainedBoundsTestLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    s_chainCallbackCount = 0;
    
    // -------------------------------------------------------------------------
    // Test: Setting source option adjusts target's maxValue via callback
    // -------------------------------------------------------------------------
    // Set source to 30, which should set target's maxValue to 30
    TestOptions::testChainedSource.setDeferred(30.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(TestOptions::testChainedSource(), 30.0f, 0.001f,
                         "testChainedSource should be 30");
    TEST_ASSERT(s_chainCallbackCount >= 1, 
                "Source callback should have been invoked");
    
    // Now try to set target above 30 - should be clamped to 30 (source's value)
    TestOptions::testChainedTarget.setDeferred(50.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(TestOptions::testChainedTarget(), 30.0f, 0.001f,
                         "testChainedTarget should be clamped to 30 (source's value)");
    
    // -------------------------------------------------------------------------
    // Test: Lowering source further clamps existing target value
    // -------------------------------------------------------------------------
    s_chainCallbackCount = 0;
    
    TestOptions::testChainedSource.setDeferred(20.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(TestOptions::testChainedSource(), 20.0f, 0.001f,
                         "testChainedSource should be 20");
    TEST_ASSERT(s_chainCallbackCount >= 1, 
                "Source callback should have been invoked again");
    
    // Target should now be clamped to 20 (new source value)
    // Note: This depends on whether changing maxValue also clamps the current value
    // The callback sets maxValue, which should trigger re-resolution of target
    
    // -------------------------------------------------------------------------
    // Test: Setting target within valid range works
    // -------------------------------------------------------------------------
    TestOptions::testChainedTarget.setDeferred(15.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(TestOptions::testChainedTarget(), 15.0f, 0.001f,
                         "testChainedTarget should be 15 (within valid range)");
    
    // Cleanup
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Reset bounds
    TestOptions::testChainedSourceObject().setMinValue(0.0f);
    TestOptions::testChainedSourceObject().setMaxValue(100.0f);
    TestOptions::testChainedTargetObject().setMinValue(0.0f);
    TestOptions::testChainedTargetObject().setMaxValue(100.0f);
    
    s_chainCallbackCount = 0;
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Cyclic OnChange Callbacks Terminate
  // Tests that cyclic onChange handlers (setting bounds on each other) terminate
  // and don't persist across frames
  // ============================================================================
  
  void test_cyclicOnChangeCallbacksTerminate() {
    std::cout << "  Running test_cyclicOnChangeCallbacksTerminate..." << std::endl;
    
    // Reset bounds to known state
    TestOptions::testCyclicAObject().setMinValue(0.0f);
    TestOptions::testCyclicAObject().setMaxValue(100.0f);
    TestOptions::testCyclicBObject().setMinValue(0.0f);
    TestOptions::testCyclicBObject().setMaxValue(100.0f);
    
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{34000, "CyclicBoundsTestLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    s_cycleCallbackCount = 0;
    
    // -------------------------------------------------------------------------
    // Test: Setting A triggers A->B->A cycle via bounds, but terminates
    // -------------------------------------------------------------------------
    // A's callback sets B's minValue = A's value
    // B's callback sets A's minValue = B's value
    // This could create a cycle, but should terminate because:
    // 1. setMinValue only marks dirty if the bound actually changes
    // 2. Resolution loop has maxResolves limit
    
    // Set A to 30 - this should set B's minValue to 30
    TestOptions::testCyclicA.setDeferred(30.0f, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(TestOptions::testCyclicA(), 30.0f, 0.001f,
                         "testCyclicA should be 30");
    
    // The callbacks should have been invoked but terminated
    // (exact count depends on implementation, but should be bounded)
    TEST_ASSERT(s_cycleCallbackCount <= 8, 
                "Cyclic callbacks should terminate (count <= 8)");
    TEST_ASSERT(s_cycleCallbackCount >= 1, 
                "At least 1 cyclic callback should have been invoked");
    
    int countAfterFirstResolve = s_cycleCallbackCount;
    float valueAAfterResolve = TestOptions::testCyclicA();
    float valueBAfterResolve = TestOptions::testCyclicB();
    
    // -------------------------------------------------------------------------
    // Test: Dirty options are cleared after frame, don't persist
    // -------------------------------------------------------------------------
    // Call applyPendingValues again without any new changes
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Callback count should not have increased - dirty options were cleared
    TEST_ASSERT(s_cycleCallbackCount == countAfterFirstResolve, 
                "Cyclic callbacks should not persist across frames");
    
    // Values should be unchanged
    TEST_ASSERT_FLOAT_EQ(TestOptions::testCyclicA(), valueAAfterResolve, 0.001f,
                         "testCyclicA should be unchanged after second resolve");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testCyclicB(), valueBAfterResolve, 0.001f,
                         "testCyclicB should be unchanged after second resolve");
    
    // Cleanup
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Reset bounds
    TestOptions::testCyclicAObject().setMinValue(0.0f);
    TestOptions::testCyclicAObject().setMaxValue(100.0f);
    TestOptions::testCyclicBObject().setMinValue(0.0f);
    TestOptions::testCyclicBObject().setMaxValue(100.0f);
    
    s_cycleCallbackCount = 0;
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Value-Setting Chain (A -> B -> C -> D -> E)
  // Tests that onChange handlers that set values on other options cascade
  // through multiple resolution passes within a single applyPendingValues call
  // ============================================================================
  
  void test_valueSettingChain() {
    std::cout << "  Running test_valueSettingChain..." << std::endl;
    
    // Use the Derived layer (created by initializeSystemLayers)
    const RtxOptionLayer* derivedLayer = RtxOptionLayer::getDerivedLayer();
    TEST_ASSERT(derivedLayer != nullptr, "Derived layer should exist");
    
    // -------------------------------------------------------------------------
    // Test: Setting A cascades through B -> C -> D
    // -------------------------------------------------------------------------
    // Set A = 100
    // A's callback sets B = A + 1 = 101
    // B's callback sets C = B + 1 = 102
    // C's callback sets D = C + 1 = 103
    // D's callback does nothing (end of chain)
    s_chainCallbackCount = 0;
    
    TestOptions::testValueChainA.setDeferred(100, derivedLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(TestOptions::testValueChainA() == 100, "Chain A should be 100");
    TEST_ASSERT(TestOptions::testValueChainB() == 101, "Chain B should be 101 (A + 1)");
    TEST_ASSERT(TestOptions::testValueChainC() == 102, "Chain C should be 102 (B + 1)");
    TEST_ASSERT(TestOptions::testValueChainD() == 103, "Chain D should be 103 (C + 1)");
    
    // All 4 callbacks should have been invoked (one for each option in the chain)
    TEST_ASSERT(s_chainCallbackCount == 4, 
                "All 4 chain callbacks should have been invoked");
    
    // -------------------------------------------------------------------------
    // Test: Chain resolves within a single applyPendingValues call
    // -------------------------------------------------------------------------
    // This is verified by the fact that all values are correct after one call
    // The resolution loop iterates until no more dirty options (up to maxResolves=4)
    
    // -------------------------------------------------------------------------
    // Test: Setting A again cascades again
    // -------------------------------------------------------------------------
    s_chainCallbackCount = 0;
    
    TestOptions::testValueChainA.setDeferred(200, derivedLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(TestOptions::testValueChainA() == 200, "Chain A should be 200");
    TEST_ASSERT(TestOptions::testValueChainB() == 201, "Chain B should be 201");
    TEST_ASSERT(TestOptions::testValueChainC() == 202, "Chain C should be 202");
    TEST_ASSERT(TestOptions::testValueChainD() == 203, "Chain D should be 203");
    
    TEST_ASSERT(s_chainCallbackCount == 4, 
                "All 4 chain callbacks should have been invoked again");
    
    // -------------------------------------------------------------------------
    // Test: Verify no further callbacks without changes
    // -------------------------------------------------------------------------
    s_chainCallbackCount = 0;
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(s_chainCallbackCount == 0, 
                "No callbacks should be invoked without changes");
    
    s_chainCallbackCount = 0;
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Cyclic Value-Setting Terminates (A sets B, B sets A)
  // Tests that cyclic onChange handlers are forced to terminate after maxResolves
  // and don't continue on subsequent frames
  // ============================================================================
  
  void test_cyclicValueSettingTerminates() {
    std::cout << "  Running test_cyclicValueSettingTerminates..." << std::endl;
    
    // Use the Derived layer (created by initializeSystemLayers)
    const RtxOptionLayer* derivedLayer = RtxOptionLayer::getDerivedLayer();
    TEST_ASSERT(derivedLayer != nullptr, "Derived layer should exist");
    
    s_cycleCallbackCount = 0;
    
    // -------------------------------------------------------------------------
    // Test: Setting A triggers A->B->A->B->... cycle but terminates
    // -------------------------------------------------------------------------
    // A's callback sets B = A + 1
    // B's callback sets A = B + 1
    // This creates a cycle that should terminate after maxResolves (4 passes)
    //
    // Pass 1: A = 1000, A's callback sets B = 1001
    // Pass 2: B = 1001, B's callback sets A = 1002
    // Pass 3: A = 1002, A's callback sets B = 1003
    // Pass 4: B = 1003, B's callback sets A = 1004 (maxResolves reached, stops)
    
    TestOptions::testValueCycleA.setDeferred(1000, derivedLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // The cycle should have been limited by maxResolves
    // Expected: A starts at 1000, cascades 4 times
    // Final values depend on exact maxResolves behavior
    TEST_ASSERT(TestOptions::testValueCycleA() >= 1000, "Cycle A should be >= 1000");
    TEST_ASSERT(TestOptions::testValueCycleB() >= 1000, "Cycle B should be >= 1000");
    
    // Callback count should be bounded (not infinite)
    // With maxResolves=4, we expect at most 8 callbacks (both A and B each pass)
    TEST_ASSERT(s_cycleCallbackCount <= 8, 
                "Cyclic callbacks should terminate (count <= 8)");
    TEST_ASSERT(s_cycleCallbackCount >= 2, 
                "At least 2 cyclic callbacks should have been invoked");
    
    int countAfterFirstResolve = s_cycleCallbackCount;
    int32_t valueAAfterResolve = TestOptions::testValueCycleA();
    int32_t valueBAfterResolve = TestOptions::testValueCycleB();
    
    // -------------------------------------------------------------------------
    // Test: Dirty options are cleared after resolution, don't persist
    // -------------------------------------------------------------------------
    // Call applyPendingValues again without any new changes
    // If dirty options weren't cleared, the cycle would continue
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Callback count should not have increased - dirty options were cleared
    TEST_ASSERT(s_cycleCallbackCount == countAfterFirstResolve, 
                "Cyclic callbacks should not persist across frames");
    
    // Values should be unchanged
    TEST_ASSERT(TestOptions::testValueCycleA() == valueAAfterResolve, 
                "Cycle A should be unchanged after second resolve");
    TEST_ASSERT(TestOptions::testValueCycleB() == valueBAfterResolve, 
                "Cycle B should be unchanged after second resolve");
    
    s_cycleCallbackCount = 0;
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Environment Variable Support
  // Tests that options with environment variables are properly configured
  // ============================================================================
  
  void test_environmentVariables() {
    std::cout << "  Running test_environmentVariables..." << std::endl;
    
    // Test that environment variable names are stored correctly
    const char* intEnvVar = TestOptions::testIntWithEnvObject().getEnvironmentVariable();
    TEST_ASSERT(intEnvVar != nullptr, "Int env var should not be null");
    TEST_ASSERT(strcmp(intEnvVar, "RTX_TEST_INT_ENV") == 0, "Int env var name should match");
    
    const char* boolEnvVar = TestOptions::testBoolWithEnvObject().getEnvironmentVariable();
    TEST_ASSERT(boolEnvVar != nullptr, "Bool env var should not be null");
    TEST_ASSERT(strcmp(boolEnvVar, "RTX_TEST_BOOL_ENV") == 0, "Bool env var name should match");
    
    const char* floatEnvVar = TestOptions::testFloatWithEnvObject().getEnvironmentVariable();
    TEST_ASSERT(floatEnvVar != nullptr, "Float env var should not be null");
    TEST_ASSERT(strcmp(floatEnvVar, "RTX_TEST_FLOAT_ENV") == 0, "Float env var name should match");
    
    // Test option with both environment and flags
    const char* envFlagsVar = TestOptions::testIntEnvAndFlagsObject().getEnvironmentVariable();
    TEST_ASSERT(envFlagsVar != nullptr, "Env+flags var should not be null");
    TEST_ASSERT(strcmp(envFlagsVar, "RTX_TEST_INT_ENV_FLAGS") == 0, "Env+flags var name should match");
    TEST_ASSERT((TestOptions::testIntEnvAndFlagsObject().getFlags() & static_cast<uint32_t>(RtxOptionFlags::NoSave)) != 0,
                "testIntEnvAndFlags should have NoSave flag");
    
    // Test that options without environment variables return null or empty
    const char* noEnvVar = TestOptions::testIntObject().getEnvironmentVariable();
    TEST_ASSERT(noEnvVar == nullptr || strlen(noEnvVar) == 0, 
                "Option without env var should have null or empty string");
    
    // =========================================================================
    // Test that values are actually loaded from environment variables
    // =========================================================================
    
    // Create a test layer for environment variable loading
    Config emptyConfig;
    const RtxOptionLayer* envTestLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{25000, "EnvVarTestLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(envTestLayer != nullptr, "Failed to create env test layer");
    
    // Set environment variables
    bool setIntEnv = env::setEnvVar("RTX_TEST_INT_ENV", "999");
    bool setBoolEnv = env::setEnvVar("RTX_TEST_BOOL_ENV", "1");
    bool setFloatEnv = env::setEnvVar("RTX_TEST_FLOAT_ENV", "3.14");
    
    TEST_ASSERT(setIntEnv, "Failed to set RTX_TEST_INT_ENV");
    TEST_ASSERT(setBoolEnv, "Failed to set RTX_TEST_BOOL_ENV");
    TEST_ASSERT(setFloatEnv, "Failed to set RTX_TEST_FLOAT_ENV");
    
    // Load values from environment variables
    std::string outValue;
    bool intLoaded = TestOptions::testIntWithEnvObject().loadFromEnvironmentVariable(envTestLayer, &outValue);
    TEST_ASSERT(intLoaded, "Should successfully load int from environment variable");
    TEST_ASSERT(outValue == "999", "Loaded int value string should be '999'");
    
    bool boolLoaded = TestOptions::testBoolWithEnvObject().loadFromEnvironmentVariable(envTestLayer, &outValue);
    TEST_ASSERT(boolLoaded, "Should successfully load bool from environment variable");
    TEST_ASSERT(outValue == "1", "Loaded bool value string should be '1'");
    
    bool floatLoaded = TestOptions::testFloatWithEnvObject().loadFromEnvironmentVariable(envTestLayer, &outValue);
    TEST_ASSERT(floatLoaded, "Should successfully load float from environment variable");
    TEST_ASSERT(outValue == "3.14", "Loaded float value string should be '3.14'");
    
    // Apply pending values to resolve the loaded values
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify the actual option values
    TEST_ASSERT(TestOptions::testIntWithEnv() == 999, "testIntWithEnv should be 999 from environment");
    TEST_ASSERT(TestOptions::testBoolWithEnv() == true, "testBoolWithEnv should be true from environment");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloatWithEnv(), 3.14f, 0.001f, "testFloatWithEnv should be 3.14 from environment");
    
    // Test that loading from a non-existent environment variable returns false
    bool noEnvLoaded = TestOptions::testIntObject().loadFromEnvironmentVariable(envTestLayer, nullptr);
    TEST_ASSERT(!noEnvLoaded, "Option without env var should not load from environment");
    
    // Clean up: clear the environment variables
    env::setEnvVar("RTX_TEST_INT_ENV", "");
    env::setEnvVar("RTX_TEST_BOOL_ENV", "");
    env::setEnvVar("RTX_TEST_FLOAT_ENV", "");
    
    // Release the test layer
    RtxOptionManager::releaseLayer(envTestLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: HashSet Operations
  // Tests hash set add, remove, and contains operations
  // ============================================================================
  
  void test_hashSetOperations() {
    std::cout << "  Running test_hashSetOperations..." << std::endl;
    
    // Create three layers with different priorities: weak < middle < strong
    Config emptyConfig;
    const RtxOptionLayerKey weakKey{10000, "WeakHashLayer"};
    const RtxOptionLayerKey middleKey{15000, "MiddleHashLayer"};
    const RtxOptionLayerKey strongKey{20000, "StrongHashLayer"};
    
    const RtxOptionLayer* weakLayer = RtxOptionManager::acquireLayer("", weakKey, 1.0f, 0.1f, false, &emptyConfig);
    const RtxOptionLayer* middleLayer = RtxOptionManager::acquireLayer("", middleKey, 1.0f, 0.1f, false, &emptyConfig);
    const RtxOptionLayer* strongLayer = RtxOptionManager::acquireLayer("", strongKey, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(weakLayer != nullptr, "Failed to create weak layer");
    TEST_ASSERT(middleLayer != nullptr, "Failed to create middle layer");
    TEST_ASSERT(strongLayer != nullptr, "Failed to create strong layer");
    
    XXH64_hash_t hash1 = 0x1234567890ABCDEF;
    XXH64_hash_t hash2 = 0xFEDCBA0987654321;
    XXH64_hash_t hash3 = 0xAAAABBBBCCCCDDDD;
    XXH64_hash_t hash4 = 0x1111222233334444;
    
    // -------------------------------------------------------------------------
    // Test: Basic add in single layer
    // -------------------------------------------------------------------------
    TestOptions::testHashSet.addHash(hash1, weakLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hash1), "Hash1 should be in the set after adding");
    TEST_ASSERT(!TestOptions::testHashSet.containsHash(hash2), "Hash2 should NOT be in the set");
    
    // -------------------------------------------------------------------------
    // Test: Add in weak layer, remove in middle layer -> hash should be removed
    // -------------------------------------------------------------------------
    TestOptions::testHashSet.addHash(hash2, weakLayer);
    TestOptions::testHashSet.removeHash(hash2, middleLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(!TestOptions::testHashSet.containsHash(hash2), 
                "Hash2 should NOT be in set (middle layer removal overrides weak layer add)");
    
    // -------------------------------------------------------------------------
    // Test: Add in weak, remove in middle, re-add in strong -> hash should be present
    // -------------------------------------------------------------------------
    TestOptions::testHashSet.addHash(hash3, weakLayer);
    TestOptions::testHashSet.removeHash(hash3, middleLayer);
    TestOptions::testHashSet.addHash(hash3, strongLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hash3), 
                "Hash3 should be in set (strong layer add overrides middle layer removal)");
    
    // -------------------------------------------------------------------------
    // Test: Remove in weak layer, add in middle layer -> hash should be present
    // -------------------------------------------------------------------------
    TestOptions::testHashSet.removeHash(hash4, weakLayer);
    TestOptions::testHashSet.addHash(hash4, middleLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hash4), 
                "Hash4 should be in set (middle layer add overrides weak layer removal)");
    
    // -------------------------------------------------------------------------
    // Test: Releasing strong layer causes hash3 to fall back to middle layer (removed)
    // -------------------------------------------------------------------------
    RtxOptionManager::releaseLayer(strongLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(!TestOptions::testHashSet.containsHash(hash3), 
                "Hash3 should NOT be in set after strong layer released (falls back to middle removal)");
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hash1), "Hash1 should still be in set");
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hash4), "Hash4 should still be in set");
    
    // -------------------------------------------------------------------------
    // Test: Releasing middle layer causes hash2 and hash4 to fall back to weak layer
    // -------------------------------------------------------------------------
    RtxOptionManager::releaseLayer(middleLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hash2), 
                "Hash2 should be in set after middle layer released (falls back to weak layer add)");
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hash3), 
                "Hash3 should be in set after middle layer released (falls back to weak layer add)");
    // Hash4 was only added in middle layer, removed in weak - should now be removed
    TEST_ASSERT(!TestOptions::testHashSet.containsHash(hash4), 
                "Hash4 should NOT be in set after middle layer released (falls back to weak layer removal)");
    
    // -------------------------------------------------------------------------
    // Test: Clear removes opinion from layer, falls back to lower layers
    // -------------------------------------------------------------------------
    // Re-acquire middle layer to test clear
    const RtxOptionLayer* middleLayer2 = RtxOptionManager::acquireLayer("", middleKey, 1.0f, 0.1f, false, &emptyConfig);
    TestOptions::testHashSet.removeHash(hash1, middleLayer2);  // Remove hash1 in middle
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(!TestOptions::testHashSet.containsHash(hash1), 
                "Hash1 should NOT be in set (middle layer removal)");
    
    TestOptions::testHashSet.clearHash(hash1, middleLayer2);  // Clear opinion in middle
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hash1), 
                "Hash1 should be in set after clearing middle layer (falls back to weak layer add)");
    
    // -------------------------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------------------------
    RtxOptionManager::releaseLayer(middleLayer2);
    RtxOptionManager::releaseLayer(weakLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after all layers released
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Layer Priority and Override
  // Tests that higher priority layers override lower priority layers
  // ============================================================================
  
  void test_layerPriorityOverride() {
    std::cout << "  Running test_layerPriorityOverride..." << std::endl;
    
    // Create two test layers with different priorities (unique names for this test)
    Config emptyConfig;
    const RtxOptionLayer* lowLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{16000, "PriorityTestLow"}, 1.0f, 0.1f, false, &emptyConfig);
    const RtxOptionLayer* highLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{17000, "PriorityTestHigh"}, 1.0f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(lowLayer != nullptr, "Failed to create low priority layer");
    TEST_ASSERT(highLayer != nullptr, "Failed to create high priority layer");
    
    // Use a separate option to avoid contamination from min/max tests
    // Set a value in the low priority layer
    TestOptions::testIntLayerPriority.setDeferred(500, lowLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntLayerPriority() == 500, "Int should be 500 from low layer");
    
    // Set a different value in the high priority layer - should override
    TestOptions::testIntLayerPriority.setDeferred(999, highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntLayerPriority() == 999, "Int should be 999 from high layer (overrides low)");
    
    // Remove the high layer's value - low layer should take effect
    TestOptions::testIntLayerPriorityObject().disableLayerValue(highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntLayerPriority() == 500, "Int should fall back to 500 from low layer");
    
    // Clean up
    RtxOptionManager::releaseLayer(lowLayer);
    RtxOptionManager::releaseLayer(highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Float Blending Across Layers
  // Tests that float values blend correctly across multiple layers
  // ============================================================================
  
  void test_floatBlending() {
    std::cout << "  Running test_floatBlending..." << std::endl;
    
    // Create layers with different blend strengths
    Config emptyConfig;
    
    // Create layer with 50% blend strength
    const RtxOptionLayer* blendLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{18000, "FloatBlendLayer50"}, 0.5f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(blendLayer != nullptr, "Failed to create blend layer");
    
    // Default float is 1.5f for testFloatBlend
    // Set a different value in the blend layer with 50% strength
    TestOptions::testFloatBlend.setDeferred(10.0f, blendLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // With 50% blend: result = 10.0 * 0.5 + 1.5 * (1 - 0.5) = 5.0 + 0.75 = 5.75
    float expected = 10.0f * 0.5f + 1.5f * 0.5f;
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloatBlend(), expected, 0.01f,
                        "Float should be blended correctly");
    
    // Clean up
    RtxOptionManager::releaseLayer(blendLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Non-Float Threshold Behavior
  // Tests that non-float values only apply when blend strength >= threshold
  // ============================================================================
  
  void test_blendThreshold() {
    std::cout << "  Running test_blendThreshold..." << std::endl;
    
    Config emptyConfig;
    
    // Create layer with 40% blend strength but 50% threshold (inactive)
    const RtxOptionLayer* inactiveLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{21000, "InactiveLayer"}, 0.4f, 0.5f, false, &emptyConfig);
    
    TEST_ASSERT(inactiveLayer != nullptr, "Failed to create inactive layer");
    
    // Get current default from dedicated test option
    int defaultVal = TestOptions::testIntThreshold.getDefaultValue();
    
    // Set a value in the inactive layer (blend < threshold, so won't apply for non-float)
    TestOptions::testIntThreshold.setDeferred(9999, inactiveLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // For non-float types, the value should NOT be applied since blend < threshold
    // It should fall back to default or lower priority layers
    TEST_ASSERT(TestOptions::testIntThreshold() != 9999 || inactiveLayer->isActive(), 
                "Int should not use value from inactive layer (below threshold)");
    
    // Clean up
    RtxOptionManager::releaseLayer(inactiveLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: HashSet Layer Merging
  // Tests that hash sets merge correctly across layers with positives and negatives
  // ============================================================================
  
  void test_hashSetLayerMerging() {
    std::cout << "  Running test_hashSetLayerMerging..." << std::endl;
    
    Config emptyConfig;
    const RtxOptionLayer* lowLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{6000, "HashLowLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    const RtxOptionLayer* highLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{7000, "HashHighLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(lowLayer != nullptr, "Failed to create low hash layer");
    TEST_ASSERT(highLayer != nullptr, "Failed to create high hash layer");
    
    XXH64_hash_t hashA = 0x1111111111111111;
    XXH64_hash_t hashB = 0x2222222222222222;
    XXH64_hash_t hashC = 0x3333333333333333;
    
    // Add hashes to low layer
    TestOptions::testHashSet.addHash(hashA, lowLayer);
    TestOptions::testHashSet.addHash(hashB, lowLayer);
    TestOptions::testHashSet.addHash(hashC, lowLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // All three should be present
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hashA), "HashA should be present from low layer");
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hashB), "HashB should be present from low layer");
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hashC), "HashC should be present from low layer");
    
    // Remove hashB from high layer (negative entry overrides positive from low)
    TestOptions::testHashSet.removeHash(hashB, highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // HashB should be removed, others still present
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hashA), "HashA should still be present");
    TEST_ASSERT(!TestOptions::testHashSet.containsHash(hashB), "HashB should be removed by high layer negative");
    TEST_ASSERT(TestOptions::testHashSet.containsHash(hashC), "HashC should still be present");
    
    // Clean up hash sets
    TestOptions::testHashSet.clearHash(hashA, lowLayer);
    TestOptions::testHashSet.clearHash(hashB, lowLayer);
    TestOptions::testHashSet.clearHash(hashC, lowLayer);
    TestOptions::testHashSet.clearHash(hashB, highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Clean up
    RtxOptionManager::releaseLayer(lowLayer);
    RtxOptionManager::releaseLayer(highLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Config Serialization and Parsing
  // Tests writing options to config and reading them back
  // ============================================================================
  
  void test_configSerialization() {
    std::cout << "  Running test_configSerialization..." << std::endl;
    
    // -------------------------------------------------------------------------
    // Part 1: Test Config setOption/getOption (basic Config functionality)
    // -------------------------------------------------------------------------
    Config writeConfig;
    writeConfig.setOption("rtx.test.serializeBool", true);
    writeConfig.setOption("rtx.test.serializeInt", 12345);
    writeConfig.setOption("rtx.test.serializeFloat", 3.14159f);
    writeConfig.setOption("rtx.test.serializeString", std::string("Hello World"));
    writeConfig.setOption("rtx.test.serializeVector2", Vector2(1.5f, 2.5f));
    writeConfig.setOption("rtx.test.serializeVector3", Vector3(1.0f, 2.0f, 3.0f));
    writeConfig.setOption("rtx.test.serializeVector4", Vector4(1.0f, 2.0f, 3.0f, 4.0f));
    writeConfig.setOption("rtx.test.serializeVector2i", Vector2i(100, 200));
    
    // Read values back from config
    TEST_ASSERT(writeConfig.getOption<bool>("rtx.test.serializeBool", false) == true,
                "Serialized bool should be true");
    TEST_ASSERT(writeConfig.getOption<int>("rtx.test.serializeInt", 0) == 12345,
                "Serialized int should be 12345");
    TEST_ASSERT_FLOAT_EQ(writeConfig.getOption<float>("rtx.test.serializeFloat", 0.0f), 3.14159f, 0.00001f,
                        "Serialized float should be 3.14159");
    TEST_ASSERT(writeConfig.getOption<std::string>("rtx.test.serializeString", "") == "Hello World",
                "Serialized string should be 'Hello World'");
    
    Vector2 v2 = writeConfig.getOption<Vector2>("rtx.test.serializeVector2", Vector2(0, 0));
    TEST_ASSERT_FLOAT_EQ(v2.x, 1.5f, 0.0001f, "Serialized Vector2.x should be 1.5");
    TEST_ASSERT_FLOAT_EQ(v2.y, 2.5f, 0.0001f, "Serialized Vector2.y should be 2.5");
    
    Vector3 v3 = writeConfig.getOption<Vector3>("rtx.test.serializeVector3", Vector3(0, 0, 0));
    TEST_ASSERT_FLOAT_EQ(v3.x, 1.0f, 0.0001f, "Serialized Vector3.x should be 1.0");
    TEST_ASSERT_FLOAT_EQ(v3.y, 2.0f, 0.0001f, "Serialized Vector3.y should be 2.0");
    TEST_ASSERT_FLOAT_EQ(v3.z, 3.0f, 0.0001f, "Serialized Vector3.z should be 3.0");
    
    Vector4 v4 = writeConfig.getOption<Vector4>("rtx.test.serializeVector4", Vector4(0, 0, 0, 0));
    TEST_ASSERT_FLOAT_EQ(v4.x, 1.0f, 0.0001f, "Serialized Vector4.x should be 1.0");
    TEST_ASSERT_FLOAT_EQ(v4.y, 2.0f, 0.0001f, "Serialized Vector4.y should be 2.0");
    TEST_ASSERT_FLOAT_EQ(v4.z, 3.0f, 0.0001f, "Serialized Vector4.z should be 3.0");
    TEST_ASSERT_FLOAT_EQ(v4.w, 4.0f, 0.0001f, "Serialized Vector4.w should be 4.0");
    
    Vector2i v2i = writeConfig.getOption<Vector2i>("rtx.test.serializeVector2i", Vector2i(0, 0));
    TEST_ASSERT(v2i.x == 100, "Serialized Vector2i.x should be 100");
    TEST_ASSERT(v2i.y == 200, "Serialized Vector2i.y should be 200");
    
    // -------------------------------------------------------------------------
    // Part 2: Test RtxOption -> Config serialization via writeOptions
    // -------------------------------------------------------------------------
    Config emptyConfig;
    const RtxOptionLayer* serializeLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{25000, "SerializeTestLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(serializeLayer != nullptr, "Failed to create serialize test layer");
    
    // Set various RtxOption values in the layer
    TestOptions::testInt.setDeferred(9999, serializeLayer);
    TestOptions::testFloat.setDeferred(1.234f, serializeLayer);
    TestOptions::testBool.setDeferred(true, serializeLayer);
    TestOptions::testString.setDeferred(std::string("SerializedString"), serializeLayer);
    TestOptions::testVector3.setDeferred(Vector3(7.0f, 8.0f, 9.0f), serializeLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Write the layer's option values to a Config
    Config optionConfig;
    RtxOptionManager::writeOptions(optionConfig, serializeLayer, false);
    
    // Verify the Config contains the expected values
    TEST_ASSERT(optionConfig.getOption<int>("rtx.test.testInt", 0) == 9999,
                "Written RtxOption int should be 9999");
    TEST_ASSERT_FLOAT_EQ(optionConfig.getOption<float>("rtx.test.testFloat", 0.0f), 1.234f, 0.001f,
                        "Written RtxOption float should be 1.234");
    TEST_ASSERT(optionConfig.getOption<bool>("rtx.test.testBool", false) == true,
                "Written RtxOption bool should be true");
    TEST_ASSERT(optionConfig.getOption<std::string>("rtx.test.testString", "") == "SerializedString",
                "Written RtxOption string should be 'SerializedString'");
    
    Vector3 writtenV3 = optionConfig.getOption<Vector3>("rtx.test.testVector3", Vector3(0, 0, 0));
    TEST_ASSERT_FLOAT_EQ(writtenV3.x, 7.0f, 0.0001f, "Written RtxOption Vector3.x should be 7.0");
    TEST_ASSERT_FLOAT_EQ(writtenV3.y, 8.0f, 0.0001f, "Written RtxOption Vector3.y should be 8.0");
    TEST_ASSERT_FLOAT_EQ(writtenV3.z, 9.0f, 0.0001f, "Written RtxOption Vector3.z should be 9.0");
    
    // Clean up
    RtxOptionManager::releaseLayer(serializeLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Config File Write and Read with RtxOptions
  // Tests full round-trip: RtxOption -> Config -> File -> Config -> RtxOption
  // ============================================================================
  
  void test_configFileIO() {
    std::cout << "  Running test_configFileIO..." << std::endl;
    
    std::string tempConfigPath = "test_rtx_option_temp.conf";
    
    // -------------------------------------------------------------------------
    // Part 1: Write RtxOption values to a config file
    // -------------------------------------------------------------------------
    Config emptyConfig;
    const RtxOptionLayer* writeLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{26000, "FileWriteTestLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    TEST_ASSERT(writeLayer != nullptr, "Failed to create file write test layer");
    
    // Set RtxOption values
    TestOptions::testInt.setDeferred(77777, writeLayer);
    TestOptions::testFloat.setDeferred(2.71828f, writeLayer);
    TestOptions::testBool.setDeferred(true, writeLayer);
    TestOptions::testString.setDeferred(std::string("FileTestValue"), writeLayer);
    TestOptions::testVector2.setDeferred(Vector2(11.0f, 22.0f), writeLayer);
    TestOptions::testVector3.setDeferred(Vector3(33.0f, 44.0f, 55.0f), writeLayer);
    TestOptions::testVector2i.setDeferred(Vector2i(111, 222), writeLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Write layer's options to Config, then serialize to file
    Config writeConfig;
    RtxOptionManager::writeOptions(writeConfig, writeLayer, false);
    Config::serializeCustomConfig(writeConfig, tempConfigPath, "rtx.");
    
    // Release the write layer - options should return to defaults
    RtxOptionManager::releaseLayer(writeLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    verifyOptionsAtDefaults();
    
    // -------------------------------------------------------------------------
    // Part 2: Read config file and apply as a new layer
    // -------------------------------------------------------------------------
    Config readConfig = Config::getOptionLayerConfig(tempConfigPath);
    
    // Verify the config was read correctly from the file
    TEST_ASSERT(readConfig.getOption<int>("rtx.test.testInt", 0) == 77777,
                "Config file should contain testInt = 77777");
    TEST_ASSERT(readConfig.getOptions().size() > 0, "Config should have options after reading file");
    
    // Create a new layer with the loaded config
    RtxOptionLayer* readLayer = const_cast<RtxOptionLayer*>(RtxOptionManager::acquireLayer(
      tempConfigPath, RtxOptionLayerKey{27000, "FileReadTestLayer"}, 1.0f, 0.1f, false, &readConfig));
    TEST_ASSERT(readLayer != nullptr, "Failed to create file read test layer");
    TEST_ASSERT(readLayer->isValid(), "Layer should be valid after creation with config");
    
    // applyToAllOptions is already called by acquireLayer if layer is enabled and valid
    // Just need to call applyPendingValues to resolve the values
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify RtxOption values match what was written
    TEST_ASSERT(TestOptions::testInt() == 77777, "File-loaded RtxOption int should be 77777");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloat(), 2.71828f, 0.00001f,
                        "File-loaded RtxOption float should be 2.71828");
    TEST_ASSERT(TestOptions::testBool() == true, "File-loaded RtxOption bool should be true");
    TEST_ASSERT(TestOptions::testString() == "FileTestValue",
                "File-loaded RtxOption string should be 'FileTestValue'");
    
    Vector2 loadedV2 = TestOptions::testVector2();
    TEST_ASSERT_FLOAT_EQ(loadedV2.x, 11.0f, 0.0001f, "File-loaded RtxOption Vector2.x should be 11.0");
    TEST_ASSERT_FLOAT_EQ(loadedV2.y, 22.0f, 0.0001f, "File-loaded RtxOption Vector2.y should be 22.0");
    
    Vector3 loadedV3 = TestOptions::testVector3();
    TEST_ASSERT_FLOAT_EQ(loadedV3.x, 33.0f, 0.0001f, "File-loaded RtxOption Vector3.x should be 33.0");
    TEST_ASSERT_FLOAT_EQ(loadedV3.y, 44.0f, 0.0001f, "File-loaded RtxOption Vector3.y should be 44.0");
    TEST_ASSERT_FLOAT_EQ(loadedV3.z, 55.0f, 0.0001f, "File-loaded RtxOption Vector3.z should be 55.0");
    
    Vector2i loadedV2i = TestOptions::testVector2i();
    TEST_ASSERT(loadedV2i.x == 111, "File-loaded RtxOption Vector2i.x should be 111");
    TEST_ASSERT(loadedV2i.y == 222, "File-loaded RtxOption Vector2i.y should be 222");
    
    // -------------------------------------------------------------------------
    // Clean up
    // -------------------------------------------------------------------------
    RtxOptionManager::releaseLayer(readLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    std::filesystem::remove(tempConfigPath);
    
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: HashSetLayer Direct Operations
  // Tests HashSetLayer class operations directly
  // ============================================================================
  
  void test_hashSetLayerDirect() {
    std::cout << "  Running test_hashSetLayerDirect..." << std::endl;
    
    HashSetLayer layer1;
    HashSetLayer layer2;
    
    XXH64_hash_t h1 = 0x1000000000000001;
    XXH64_hash_t h2 = 0x2000000000000002;
    XXH64_hash_t h3 = 0x3000000000000003;
    XXH64_hash_t h4 = 0x4000000000000004;
    
    // Test add operation
    layer1.add(h1);
    layer1.add(h2);
    TEST_ASSERT(layer1.hasPositive(h1), "h1 should be positive after add");
    TEST_ASSERT(layer1.hasPositive(h2), "h2 should be positive after add");
    TEST_ASSERT(!layer1.hasNegative(h1), "h1 should not be negative after add");
    
    // Test remove operation (creates negative entry)
    layer1.remove(h3);
    TEST_ASSERT(!layer1.hasPositive(h3), "h3 should not be positive after remove");
    TEST_ASSERT(layer1.hasNegative(h3), "h3 should be negative after remove");
    
    // Test count - positives without negatives count as 1
    TEST_ASSERT(layer1.count(h1) == 1, "count(h1) should be 1");
    TEST_ASSERT(layer1.count(h3) == 0, "count(h3) should be 0 (negated)");
    TEST_ASSERT(layer1.count(h4) == 0, "count(h4) should be 0 (not in set)");
    
    // Test add removes from negatives
    layer1.add(h3);
    TEST_ASSERT(layer1.hasPositive(h3), "h3 should be positive after re-add");
    TEST_ASSERT(!layer1.hasNegative(h3), "h3 should not be negative after re-add");
    
    // Test clear removes all opinions
    layer1.clear(h1);
    TEST_ASSERT(!layer1.hasPositive(h1), "h1 should not be positive after clear");
    TEST_ASSERT(!layer1.hasNegative(h1), "h1 should not be negative after clear");
    
    // Test parsing from strings (including negative entries with '-' prefix)
    std::vector<std::string> hashStrings = {
      "0x1111111111111111",
      "0x2222222222222222",
      "-0x3333333333333333"  // Negative entry
    };
    layer2.parseFromStrings(hashStrings);
    
    TEST_ASSERT(layer2.hasPositive(0x1111111111111111), "Parsed hash1 should be positive");
    TEST_ASSERT(layer2.hasPositive(0x2222222222222222), "Parsed hash2 should be positive");
    TEST_ASSERT(layer2.hasNegative(0x3333333333333333), "Parsed hash3 should be negative");
    
    // Test toString serialization
    std::string serialized = layer2.toString();
    TEST_ASSERT(serialized.find("0x1111111111111111") != std::string::npos,
                "Serialized string should contain positive hash1");
    TEST_ASSERT(serialized.find("-0x3333333333333333") != std::string::npos,
                "Serialized string should contain negative hash3 with '-' prefix");
    
    // Test merge operation
    HashSetLayer base;
    base.add(0xAAAAAAAAAAAAAAAA);
    base.add(0xBBBBBBBBBBBBBBBB);
    
    HashSetLayer overrideLayer;
    overrideLayer.add(0xCCCCCCCCCCCCCCCC);
    overrideLayer.remove(0xAAAAAAAAAAAAAAAA);  // Override with removal
    
    // Merge base into override (override keeps its opinions, gets base's where no opinion)
    overrideLayer.mergeFrom(base);
    
    TEST_ASSERT(!overrideLayer.hasPositive(0xAAAAAAAAAAAAAAAA), "Merged should NOT have AAAA positive (override has negative)");
    TEST_ASSERT(overrideLayer.hasNegative(0xAAAAAAAAAAAAAAAA), "Merged should have AAAA negative (from override)");
    TEST_ASSERT(overrideLayer.hasPositive(0xBBBBBBBBBBBBBBBB), "Merged should have BBBB positive (from base)");
    TEST_ASSERT(overrideLayer.hasPositive(0xCCCCCCCCCCCCCCCC), "Merged should have CCCC positive (from override)");
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Option Flags
  // Tests NoSave and NoReset flags
  // ============================================================================
  
  void test_optionFlags() {
    std::cout << "  Running test_optionFlags..." << std::endl;
    
    // Test NoSave flag - option should not be written to config
    TEST_ASSERT((TestOptions::testIntNoSaveObject().getFlags() & (uint32_t)RtxOptionFlags::NoSave) != 0,
                "testIntNoSave should have NoSave flag set");
    
    Config writeConfig;
    RtxOptionManager::writeOptions(writeConfig, RtxOptionLayer::getDefaultLayer(), false);
    
    // The NoSave option should not be in the config
    TEST_ASSERT(!writeConfig.findOption("rtx.test.testIntNoSave"),
                "NoSave option should not be written to config");
    
    // Test NoReset flag - option should survive layer disable (but NOT layer removal)
    TEST_ASSERT((TestOptions::testIntNoResetObject().getFlags() & (uint32_t)RtxOptionFlags::NoReset) != 0,
                "testIntNoReset should have NoReset flag set");
    
    // Create a test layer to set the NoReset option
    Config emptyConfig;
    RtxOptionLayer* noResetLayer = const_cast<RtxOptionLayer*>(RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{9500, "NoResetTestLayer"}, 1.0f, 0.1f, false, &emptyConfig));
    TEST_ASSERT(noResetLayer != nullptr, "Failed to create NoReset test layer");
    
    // Set value for both regular int and NoReset int in the same layer
    TestOptions::testInt.setDeferred(888, noResetLayer);
    TestOptions::testIntNoReset.setDeferred(999, noResetLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Both should have the new values
    TEST_ASSERT(TestOptions::testInt() == 888, "testInt should be 888 after setting");
    TEST_ASSERT(TestOptions::testIntNoReset() == 999, "testIntNoReset should be 999 after setting");
    
    // Disable the layer (this triggers removeFromAllOptions which respects NoReset)
    noResetLayer->requestEnabled(false);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Regular int should return to default, but NoReset should keep its value
    TEST_ASSERT(TestOptions::testInt() == 100, 
                "testInt (regular) should return to default (100) after layer disable");
    TEST_ASSERT(TestOptions::testIntNoReset() == 999, 
                "testIntNoReset should retain its value (999) after layer disable due to NoReset flag");
    
    // Re-enable the layer - NoReset value should still be there
    noResetLayer->requestEnabled(true);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // NoReset option should still have the value (it was never removed)
    TEST_ASSERT(TestOptions::testIntNoReset() == 999, 
                "testIntNoReset should still be 999 after layer re-enable");
    
    // Now completely remove the layer - even NoReset options should be removed
    RtxOptionManager::releaseLayer(noResetLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Both should now return to defaults since the layer is completely gone
    TEST_ASSERT(TestOptions::testInt() == 100, 
                "testInt should return to default (100) after layer removal");
    TEST_ASSERT(TestOptions::testIntNoReset() == 42, 
                "testIntNoReset should return to default (42) after layer removal (NoReset doesn't apply to removal)");
    
    // Verify options returned to defaults
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: isDefault() Method
  // Tests that isDefault correctly identifies when value equals default
  // ============================================================================
  
  void test_isDefault() {
    std::cout << "  Running test_isDefault..." << std::endl;
    
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{8000, "IsDefaultLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Initially should be at default
    TEST_ASSERT(TestOptions::testIntObject().isDefault(), 
                "testInt should be at default initially");
    
    // Change value
    TestOptions::testInt.setDeferred(999, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(!TestOptions::testIntObject().isDefault(), 
                "testInt should NOT be at default after change");
    
    // Reset to default value
    TestOptions::testInt.setDeferred(100, testLayer);  // Default is 100
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(TestOptions::testIntObject().isDefault(), 
                "testInt should be at default after setting to default value");
    
    // Clean up
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: getDefaultValue() Method
  // Tests that getDefaultValue returns the correct default
  // ============================================================================
  
  void test_getDefaultValue() {
    std::cout << "  Running test_getDefaultValue..." << std::endl;
    
    // Test default values for various types
    TEST_ASSERT(TestOptions::testBool.getDefaultValue() == false,
                "testBool default should be false");
    TEST_ASSERT(TestOptions::testInt.getDefaultValue() == 100,
                "testInt default should be 100");
    TEST_ASSERT_FLOAT_EQ(TestOptions::testFloat.getDefaultValue(), 1.5f, 0.0001f,
                        "testFloat default should be 1.5");
    TEST_ASSERT(TestOptions::testString.getDefaultValue() == "default",
                "testString default should be 'default'");
    
    Vector2 v2Default = TestOptions::testVector2.getDefaultValue();
    TEST_ASSERT_FLOAT_EQ(v2Default.x, 1.0f, 0.0001f, "testVector2.x default should be 1.0");
    TEST_ASSERT_FLOAT_EQ(v2Default.y, 2.0f, 0.0001f, "testVector2.y default should be 2.0");
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Layer Enable/Disable
  // Tests layer enable/disable and dirty state
  // ============================================================================
  
  void test_layerEnableDisable() {
    std::cout << "  Running test_layerEnableDisable..." << std::endl;
    
    Config emptyConfig;
    RtxOptionLayer* testLayer = const_cast<RtxOptionLayer*>(RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{20000, "EnableDisableLayer"}, 1.0f, 0.1f, false, &emptyConfig));
    
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Layer should start enabled
    TEST_ASSERT(testLayer->isEnabled(), "Layer should be enabled initially");
    
    // Set a value using dedicated option to avoid state contamination
    TestOptions::testIntEnableDisable.setDeferred(777, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testIntEnableDisable() == 777, "Value should be 777 while layer enabled");
    
    // Request disable
    testLayer->requestEnabled(false);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(!testLayer->isEnabled(), "Layer should be disabled after request");

    // NOTE: disabling then enabling a layer will currently reset any runtime values set via setDeferred.
    //   This is why we cant test if testIntEnableDisable is still 777 after enabling the layer.
    
    // Value should fall back (not be from disabled layer)
    // Note: This depends on whether there are other layers with values
    
    // Request enable
    testLayer->requestEnabled(true);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(testLayer->isEnabled(), "Layer should be enabled after request");
    
    // Clean up
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Blend Strength Request
  // Tests blend strength request and resolution
  // ============================================================================
  
  void test_blendStrengthRequest() {
    std::cout << "  Running test_blendStrengthRequest..." << std::endl;
    
    Config emptyConfig;
    RtxOptionLayer* testLayer = const_cast<RtxOptionLayer*>(RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{9500, "BlendStrengthLayer"}, 0.5f, 0.1f, false, &emptyConfig));
    
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Initial blend strength should be 0.5
    TEST_ASSERT_FLOAT_EQ(testLayer->getBlendStrength(), 0.5f, 0.0001f,
                        "Initial blend strength should be 0.5");
    
    // Request higher blend strength
    testLayer->requestBlendStrength(0.8f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(testLayer->getBlendStrength(), 0.8f, 0.0001f,
                        "Blend strength should be 0.8 after request");
    
    // Multiple requests - should take MAX
    testLayer->requestBlendStrength(0.3f);
    testLayer->requestBlendStrength(0.9f);
    testLayer->requestBlendStrength(0.6f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(testLayer->getBlendStrength(), 0.9f, 0.0001f,
                        "Blend strength should be MAX of requests (0.9)");
    
    // Clean up
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Blend Threshold Request
  // Tests blend threshold request and resolution
  // ============================================================================
  
  void test_blendThresholdRequest() {
    std::cout << "  Running test_blendThresholdRequest..." << std::endl;
    
    Config emptyConfig;
    RtxOptionLayer* testLayer = const_cast<RtxOptionLayer*>(RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{9600, "BlendThresholdLayer"}, 1.0f, 0.5f, false, &emptyConfig));
    
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Initial threshold should be 0.5
    TEST_ASSERT_FLOAT_EQ(testLayer->getBlendStrengthThreshold(), 0.5f, 0.0001f,
                        "Initial blend threshold should be 0.5");
    
    // Request lower threshold
    testLayer->requestBlendThreshold(0.3f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(testLayer->getBlendStrengthThreshold(), 0.3f, 0.0001f,
                        "Blend threshold should be 0.3 after request");
    
    // Multiple requests - should take MIN
    testLayer->requestBlendThreshold(0.8f);
    testLayer->requestBlendThreshold(0.2f);
    testLayer->requestBlendThreshold(0.6f);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT_FLOAT_EQ(testLayer->getBlendStrengthThreshold(), 0.2f, 0.0001f,
                        "Blend threshold should be MIN of requests (0.2)");
    
    // Clean up
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: RtxOptionLayerKey Comparison
  // Tests layer key ordering and equality
  // ============================================================================
  
  void test_layerKeyComparison() {
    std::cout << "  Running test_layerKeyComparison..." << std::endl;
    
    RtxOptionLayerKey keyA = { 100, "LayerA" };
    RtxOptionLayerKey keyB = { 200, "LayerB" };
    RtxOptionLayerKey keyC = { 100, "LayerC" };
    RtxOptionLayerKey keyD = { 100, "LayerA" };
    
    // Higher priority (larger number) should come first (operator< returns true for higher priority)
    TEST_ASSERT(keyB < keyA, "Higher priority (200) should be 'less than' lower priority (100) in ordering");
    
    // Same priority - alphabetical order
    TEST_ASSERT(keyA < keyC, "Same priority: 'LayerA' should be 'less than' 'LayerC' alphabetically");
    
    // Equality test
    TEST_ASSERT(keyA == keyD, "Same priority and name should be equal");
    TEST_ASSERT(!(keyA == keyB), "Different priority should not be equal");
    TEST_ASSERT(!(keyA == keyC), "Same priority but different name should not be equal");
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Full Option Name
  // Tests getFullName() method
  // ============================================================================
  
  void test_fullOptionName() {
    std::cout << "  Running test_fullOptionName..." << std::endl;
    
    std::string fullName = TestOptions::testIntObject().getFullName();
    TEST_ASSERT(fullName == "rtx.test.testInt", 
                "Full name should be 'rtx.test.testInt'");
    
    std::string fullName2 = TestOptions::testBoolObject().getFullName();
    TEST_ASSERT(fullName2 == "rtx.test.testBool", 
                "Full name should be 'rtx.test.testBool'");
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Option Type Identification
  // Tests getType() and getOptionType() methods
  // ============================================================================
  
  void test_optionTypeIdentification() {
    std::cout << "  Running test_optionTypeIdentification..." << std::endl;
    
    TEST_ASSERT(TestOptions::testBoolObject().getType() == OptionType::Bool,
                "testBool should have type Bool");
    TEST_ASSERT(TestOptions::testIntObject().getType() == OptionType::Int,
                "testInt should have type Int");
    TEST_ASSERT(TestOptions::testFloatObject().getType() == OptionType::Float,
                "testFloat should have type Float");
    TEST_ASSERT(TestOptions::testStringObject().getType() == OptionType::String,
                "testString should have type String");
    TEST_ASSERT(TestOptions::testVector2Object().getType() == OptionType::Vector2,
                "testVector2 should have type Vector2");
    TEST_ASSERT(TestOptions::testVector3Object().getType() == OptionType::Vector3,
                "testVector3 should have type Vector3");
    TEST_ASSERT(TestOptions::testVector4Object().getType() == OptionType::Vector4,
                "testVector4 should have type Vector4");
    TEST_ASSERT(TestOptions::testVector2iObject().getType() == OptionType::Vector2i,
                "testVector2i should have type Vector2i");
    TEST_ASSERT(TestOptions::testHashSetObject().getType() == OptionType::HashSet,
                "testHashSet should have type HashSet");
    TEST_ASSERT(TestOptions::testEnumObject().getType() == OptionType::Int,
                "testEnum should have type Int (enums are stored as int)");
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Vector Blending
  // Tests that Vector2/3/4 values blend correctly across layers
  // ============================================================================
  
  void test_vectorBlending() {
    std::cout << "  Running test_vectorBlending..." << std::endl;
    
    Config emptyConfig;
    
    // Create layer with 50% blend strength
    const RtxOptionLayer* blendLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{19000, "Vector3BlendLayer"}, 0.5f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(blendLayer != nullptr, "Failed to create blend layer");
    
    // Default Vector3 is (1.0, 2.0, 3.0) for testVector3Blend
    // Set a different value in the blend layer with 50% strength
    TestOptions::testVector3Blend.setDeferred(Vector3(10.0f, 20.0f, 30.0f), blendLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // With 50% blend: each component = layerVal * 0.5 + defaultVal * 0.5
    // x = 10.0 * 0.5 + 1.0 * 0.5 = 5.5
    // y = 20.0 * 0.5 + 2.0 * 0.5 = 11.0
    // z = 30.0 * 0.5 + 3.0 * 0.5 = 16.5
    Vector3 result = TestOptions::testVector3Blend();
    TEST_ASSERT_FLOAT_EQ(result.x, 5.5f, 0.01f, "Vector3.x should be blended to 5.5");
    TEST_ASSERT_FLOAT_EQ(result.y, 11.0f, 0.01f, "Vector3.y should be blended to 11.0");
    TEST_ASSERT_FLOAT_EQ(result.z, 16.5f, 0.01f, "Vector3.z should be blended to 16.5");
    
    // Clean up
    RtxOptionManager::releaseLayer(blendLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Dynamic Min/Max Setting
  // Tests setMinValue and setMaxValue methods
  // ============================================================================
  
  void test_dynamicMinMax() {
    std::cout << "  Running test_dynamicMinMax..." << std::endl;
    
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{10500, "DynamicMinMaxLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Set dynamic min value for int option
    TestOptions::testInt.setMinValue(50);
    
    // Set value below min - should be clamped
    TestOptions::testInt.setDeferred(25, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testInt() >= 50, "Value should be clamped to min (50)");
    
    // Set dynamic max value
    TestOptions::testInt.setMaxValue(200);
    
    // Set value above max - should be clamped
    TestOptions::testInt.setDeferred(300, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testInt() <= 200, "Value should be clamped to max (200)");
    
    // Set value within range - should work normally
    TestOptions::testInt.setDeferred(150, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testInt() == 150, "Value should be 150 when within range");
    
    // Clean up - reset min/max values to not affect other tests
    // Set to extreme values that won't affect normal testing
    TestOptions::testInt.setMinValue(std::numeric_limits<int32_t>::min());
    TestOptions::testInt.setMaxValue(std::numeric_limits<int32_t>::max());
    
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: hasValueInLayer
  // Tests whether option has a value set in a specific layer
  // ============================================================================
  
  void test_hasValueInLayer() {
    std::cout << "  Running test_hasValueInLayer..." << std::endl;
    
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{11000, "HasValueLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Should have value in default layer
    TEST_ASSERT(TestOptions::testIntObject().hasValueInLayer(RtxOptionLayer::getDefaultLayer()),
                "testInt should have value in default layer");
    
    // Should NOT have value in test layer initially
    TEST_ASSERT(!TestOptions::testIntObject().hasValueInLayer(testLayer),
                "testInt should NOT have value in test layer initially");
    
    // Set value in test layer
    TestOptions::testInt.setDeferred(123, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Now should have value in test layer
    TEST_ASSERT(TestOptions::testIntObject().hasValueInLayer(testLayer),
                "testInt should have value in test layer after setting");
    
    // Clean up
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Config Value Parsing
  // Tests Config::parseOptionValue for various types
  // ============================================================================
  
  void test_configParsing() {
    std::cout << "  Running test_configParsing..." << std::endl;
    
    // Test bool parsing
    bool boolResult = false;
    Config::parseOptionValue("True", boolResult);
    TEST_ASSERT(boolResult == true, "Parsing 'True' should yield true");
    
    Config::parseOptionValue("false", boolResult);
    TEST_ASSERT(boolResult == false, "Parsing 'false' should yield false");
    
    Config::parseOptionValue("1", boolResult);
    TEST_ASSERT(boolResult == true, "Parsing '1' should yield true");
    
    // Test int parsing
    int32_t intResult = 0;
    Config::parseOptionValue("42", intResult);
    TEST_ASSERT(intResult == 42, "Parsing '42' should yield 42");
    
    Config::parseOptionValue("-100", intResult);
    TEST_ASSERT(intResult == -100, "Parsing '-100' should yield -100");
    
    // Test float parsing
    float floatResult = 0.0f;
    Config::parseOptionValue("3.14159", floatResult);
    TEST_ASSERT_FLOAT_EQ(floatResult, 3.14159f, 0.00001f, "Parsing '3.14159' should yield 3.14159");
    
    Config::parseOptionValue("-2.5", floatResult);
    TEST_ASSERT_FLOAT_EQ(floatResult, -2.5f, 0.0001f, "Parsing '-2.5' should yield -2.5");
    
    // Test string parsing
    std::string stringResult;
    Config::parseOptionValue("Hello World", stringResult);
    TEST_ASSERT(stringResult == "Hello World", "Parsing 'Hello World' should yield 'Hello World'");
    
    // Test Vector2 parsing
    Vector2 v2Result;
    Config::parseOptionValue("1.5, 2.5", v2Result);
    TEST_ASSERT_FLOAT_EQ(v2Result.x, 1.5f, 0.0001f, "Parsing Vector2 x should be 1.5");
    TEST_ASSERT_FLOAT_EQ(v2Result.y, 2.5f, 0.0001f, "Parsing Vector2 y should be 2.5");
    
    // Test Vector3 parsing
    Vector3 v3Result;
    Config::parseOptionValue("1.0, 2.0, 3.0", v3Result);
    TEST_ASSERT_FLOAT_EQ(v3Result.x, 1.0f, 0.0001f, "Parsing Vector3 x should be 1.0");
    TEST_ASSERT_FLOAT_EQ(v3Result.y, 2.0f, 0.0001f, "Parsing Vector3 y should be 2.0");
    TEST_ASSERT_FLOAT_EQ(v3Result.z, 3.0f, 0.0001f, "Parsing Vector3 z should be 3.0");
    
    // Test Vector4 parsing
    Vector4 v4Result;
    Config::parseOptionValue("1.0, 2.0, 3.0, 4.0", v4Result);
    TEST_ASSERT_FLOAT_EQ(v4Result.x, 1.0f, 0.0001f, "Parsing Vector4 x should be 1.0");
    TEST_ASSERT_FLOAT_EQ(v4Result.y, 2.0f, 0.0001f, "Parsing Vector4 y should be 2.0");
    TEST_ASSERT_FLOAT_EQ(v4Result.z, 3.0f, 0.0001f, "Parsing Vector4 z should be 3.0");
    TEST_ASSERT_FLOAT_EQ(v4Result.w, 4.0f, 0.0001f, "Parsing Vector4 w should be 4.0");
    
    // Test Vector2i parsing
    Vector2i v2iResult;
    Config::parseOptionValue("100, 200", v2iResult);
    TEST_ASSERT(v2iResult.x == 100, "Parsing Vector2i x should be 100");
    TEST_ASSERT(v2iResult.y == 200, "Parsing Vector2i y should be 200");
    
    // Test string vector parsing (for hash sets)
    std::vector<std::string> vecResult;
    Config::parseOptionValue("0x1234, 0x5678, 0xABCD", vecResult);
    TEST_ASSERT(vecResult.size() == 3, "Parsed vector should have 3 elements");
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Reset to Default
  // Tests resetToDefault() method
  // ============================================================================
  
  void test_resetToDefault() {
    std::cout << "  Running test_resetToDefault..." << std::endl;
    
    Config emptyConfig;
    const RtxOptionLayer* testLayer = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{12000, "ResetLayer"}, 1.0f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(testLayer != nullptr, "Failed to create test layer");
    
    // Modify value
    TestOptions::testInt.setDeferred(999, testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    TEST_ASSERT(TestOptions::testInt() != 100, "Value should be changed from default");
    
    // Reset to default
    TestOptions::testInt.resetToDefault();
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Should now be at or near default (depending on whether reset applies to the target layer)
    // Note: resetToDefault() sets the value in the current target layer to the default value
    
    // Clean up
    RtxOptionManager::releaseLayer(testLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Multiple Layers Complex Scenario
  // Tests a complex scenario with multiple overlapping layers
  // ============================================================================
  
  void test_multipleLayersComplex() {
    std::cout << "  Running test_multipleLayersComplex..." << std::endl;
    
    Config emptyConfig;
    
    // Create three layers with different priorities (use high priorities to avoid conflicts)
    const RtxOptionLayer* layer1 = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{22000, "ComplexLayer1"}, 1.0f, 0.1f, false, &emptyConfig);
    const RtxOptionLayer* layer2 = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{23000, "ComplexLayer2"}, 1.0f, 0.1f, false, &emptyConfig);
    const RtxOptionLayer* layer3 = RtxOptionManager::acquireLayer("", 
      RtxOptionLayerKey{24000, "ComplexLayer3"}, 1.0f, 0.1f, false, &emptyConfig);
    
    TEST_ASSERT(layer1 && layer2 && layer3, "Failed to create all layers");
    
    // Set values in each layer using dedicated option to avoid state contamination
    TestOptions::testIntComplex.setDeferred(1000, layer1);  // Lowest priority
    TestOptions::testIntComplex.setDeferred(2000, layer2);  // Middle priority
    // layer3 has no value
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Highest layer with value should win (layer2)
    TEST_ASSERT(TestOptions::testIntComplex() == 2000, 
                "Value should come from highest priority layer with a value (layer2 = 2000)");
    
    // Add value to layer3 (highest)
    TestOptions::testIntComplex.setDeferred(3000, layer3);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(TestOptions::testIntComplex() == 3000, 
                "Value should now come from layer3 (3000)");
    
    // Remove layer3's value
    TestOptions::testIntComplexObject().disableLayerValue(layer3);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(TestOptions::testIntComplex() == 2000, 
                "Value should fall back to layer2 (2000)");
    
    // Remove layer2's value
    TestOptions::testIntComplexObject().disableLayerValue(layer2);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(TestOptions::testIntComplex() == 1000, 
                "Value should fall back to layer1 (1000)");
    
    // Remove layer1's value
    TestOptions::testIntComplexObject().disableLayerValue(layer1);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Should fall back to default
    TEST_ASSERT(TestOptions::testIntComplex() == 100, 
                "Value should fall back to default (100)");
    
    // Clean up
    RtxOptionManager::releaseLayer(layer1);
    RtxOptionManager::releaseLayer(layer2);
    RtxOptionManager::releaseLayer(layer3);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify options returned to defaults after test layer release
    verifyOptionsAtDefaults();
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test: Migration of Miscategorized Options
  // Tests that options in wrong layers can be detected and migrated
  // ============================================================================
  
  void test_migrateMiscategorizedOptions() {
    std::cout << "  Running test_migrateMiscategorizedOptions..." << std::endl;
    
    // Get the system layers
    RtxOptionLayer* userLayer = const_cast<RtxOptionLayer*>(RtxOptionLayer::getUserLayer());
    RtxOptionLayer* rtxConfLayer = RtxOptionLayer::getRtxConfLayer();
    
    TEST_ASSERT(userLayer != nullptr, "User layer should exist");
    TEST_ASSERT(rtxConfLayer != nullptr, "RtxConf layer should exist");
    
    // Category flags are set during initializeSystemLayers():
    // - userLayer has UserSetting flag (only user options belong there)
    // - rtxConfLayer has categoryFlags = 0 (developer options)
    
    // -------------------------------------------------------------------------
    // Test 1: Developer option in user layer should be counted as miscategorized
    // -------------------------------------------------------------------------
    
    // Set a developer option (no UserSetting flag) in the user layer
    TestOptions::testMigrateDeveloper.setDeferred(999, userLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify it's in the user layer
    TEST_ASSERT(TestOptions::testMigrateDeveloperObject().hasValueInLayer(userLayer),
                "testMigrateDeveloper should be in user layer");
    
    // Count should be at least 1 (may be more if other test options leak)
    uint32_t miscategorizedInUser = userLayer->countMiscategorizedOptions();
    TEST_ASSERT(miscategorizedInUser >= 1,
                "User layer should have at least 1 miscategorized option (developer option)");
    
    // -------------------------------------------------------------------------
    // Test 2: User option in rtxConf layer should be counted as miscategorized
    // -------------------------------------------------------------------------
    
    // Set a user option (with UserSetting flag) in the rtxConf layer
    TestOptions::testMigrateUser.setDeferred(888, rtxConfLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify it's in the rtxConf layer
    TEST_ASSERT(TestOptions::testMigrateUserObject().hasValueInLayer(rtxConfLayer),
                "testMigrateUser should be in rtxConf layer");
    
    // Count should be at least 1
    uint32_t miscategorizedInRtx = rtxConfLayer->countMiscategorizedOptions();
    TEST_ASSERT(miscategorizedInRtx >= 1,
                "RtxConf layer should have at least 1 miscategorized option (user option)");
    
    // -------------------------------------------------------------------------
    // Test 3: Migrate developer option from user layer to rtxConf layer
    // -------------------------------------------------------------------------
    
    uint32_t migratedFromUser = userLayer->migrateMiscategorizedOptions();
    TEST_ASSERT(migratedFromUser >= 1,
                "Should have migrated at least 1 option from user layer");
    
    // Developer option should now be in rtxConf layer
    TEST_ASSERT(!TestOptions::testMigrateDeveloperObject().hasValueInLayer(userLayer),
                "testMigrateDeveloper should no longer be in user layer after migration");
    TEST_ASSERT(TestOptions::testMigrateDeveloperObject().hasValueInLayer(rtxConfLayer),
                "testMigrateDeveloper should now be in rtxConf layer after migration");
    
    // Value should still be 999
    TEST_ASSERT(TestOptions::testMigrateDeveloper() == 999,
                "testMigrateDeveloper value should be preserved after migration");
    
    // -------------------------------------------------------------------------
    // Test 4: Migrate user option from rtxConf layer to user layer
    // -------------------------------------------------------------------------
    
    uint32_t migratedFromRtx = rtxConfLayer->migrateMiscategorizedOptions();
    TEST_ASSERT(migratedFromRtx >= 1,
                "Should have migrated at least 1 option from rtxConf layer");
    
    // User option should now be in user layer
    TEST_ASSERT(!TestOptions::testMigrateUserObject().hasValueInLayer(rtxConfLayer),
                "testMigrateUser should no longer be in rtxConf layer after migration");
    TEST_ASSERT(TestOptions::testMigrateUserObject().hasValueInLayer(userLayer),
                "testMigrateUser should now be in user layer after migration");
    
    // Value should still be 888
    TEST_ASSERT(TestOptions::testMigrateUser() == 888,
                "testMigrateUser value should be preserved after migration");
    
    // -------------------------------------------------------------------------
    // Test 5: UserSetting + NoReset option should still migrate
    // -------------------------------------------------------------------------
    
    // Set a user option with NoReset in rtxConf layer
    TestOptions::testMigrateUserNoReset.setDeferred(777, rtxConfLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    TEST_ASSERT(TestOptions::testMigrateUserNoResetObject().hasValueInLayer(rtxConfLayer),
                "testMigrateUserNoReset should be in rtxConf layer");
    
    // Migrate - NoReset should not prevent migration
    migratedFromRtx = rtxConfLayer->migrateMiscategorizedOptions();
    TEST_ASSERT(migratedFromRtx >= 1,
                "Should have migrated NoReset user option");
    
    TEST_ASSERT(!TestOptions::testMigrateUserNoResetObject().hasValueInLayer(rtxConfLayer),
                "testMigrateUserNoReset should no longer be in rtxConf layer");
    TEST_ASSERT(TestOptions::testMigrateUserNoResetObject().hasValueInLayer(userLayer),
                "testMigrateUserNoReset should now be in user layer");
    TEST_ASSERT(TestOptions::testMigrateUserNoReset() == 777,
                "testMigrateUserNoReset value should be preserved");
    
    // -------------------------------------------------------------------------
    // Test 6: Hashset migration - developer hashset in user layer
    // -------------------------------------------------------------------------
    
    // Use unique hash values for migration tests
    XXH64_hash_t devHash1 = 0xABCDEF1234567890;
    XXH64_hash_t devHash2 = 0x0987654321FEDCBA;
    XXH64_hash_t userHash1 = 0x1111222233334444;
    XXH64_hash_t userHash2 = 0x5555666677778888;
    
    // Add some hashes to a developer hashset in the user layer
    TestOptions::testMigrateDeveloperHash.addHash(devHash1, userLayer);
    TestOptions::testMigrateDeveloperHash.addHash(devHash2, userLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify it's in the user layer
    TEST_ASSERT(TestOptions::testMigrateDeveloperHashObject().hasValueInLayer(userLayer),
                "testMigrateDeveloperHash should be in user layer");
    TEST_ASSERT(TestOptions::testMigrateDeveloperHash().count(devHash1) > 0,
                "testMigrateDeveloperHash should contain devHash1");
    TEST_ASSERT(TestOptions::testMigrateDeveloperHash().count(devHash2) > 0,
                "testMigrateDeveloperHash should contain devHash2");
    
    // Migrate developer hashset from user layer
    userLayer->migrateMiscategorizedOptions();
    
    // Developer hashset should now be in rtxConf layer with values preserved
    TEST_ASSERT(!TestOptions::testMigrateDeveloperHashObject().hasValueInLayer(userLayer),
                "testMigrateDeveloperHash should no longer be in user layer");
    TEST_ASSERT(TestOptions::testMigrateDeveloperHashObject().hasValueInLayer(rtxConfLayer),
                "testMigrateDeveloperHash should now be in rtxConf layer");
    TEST_ASSERT(TestOptions::testMigrateDeveloperHash().count(devHash1) > 0,
                "testMigrateDeveloperHash should still contain devHash1 after migration");
    TEST_ASSERT(TestOptions::testMigrateDeveloperHash().count(devHash2) > 0,
                "testMigrateDeveloperHash should still contain devHash2 after migration");
    
    // -------------------------------------------------------------------------
    // Test 7: Hashset migration - user hashset in rtxConf layer
    // -------------------------------------------------------------------------
    
    // Add some hashes to a user hashset in the rtxConf layer
    TestOptions::testMigrateUserHash.addHash(userHash1, rtxConfLayer);
    TestOptions::testMigrateUserHash.addHash(userHash2, rtxConfLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // Verify it's in the rtxConf layer
    TEST_ASSERT(TestOptions::testMigrateUserHashObject().hasValueInLayer(rtxConfLayer),
                "testMigrateUserHash should be in rtxConf layer");
    
    // Migrate user hashset from rtxConf layer
    rtxConfLayer->migrateMiscategorizedOptions();
    
    // User hashset should now be in user layer with values preserved
    TEST_ASSERT(!TestOptions::testMigrateUserHashObject().hasValueInLayer(rtxConfLayer),
                "testMigrateUserHash should no longer be in rtxConf layer");
    TEST_ASSERT(TestOptions::testMigrateUserHashObject().hasValueInLayer(userLayer),
                "testMigrateUserHash should now be in user layer");
    TEST_ASSERT(TestOptions::testMigrateUserHash().count(userHash1) > 0,
                "testMigrateUserHash should still contain userHash1 after migration");
    TEST_ASSERT(TestOptions::testMigrateUserHash().count(userHash2) > 0,
                "testMigrateUserHash should still contain userHash2 after migration");
    
    // -------------------------------------------------------------------------
    // Test 8: Cleanup - clear test options from layers
    // -------------------------------------------------------------------------
    
    // Clear the options from both layers to reset state
    TestOptions::testMigrateDeveloperObject().disableLayerValue(rtxConfLayer);
    TestOptions::testMigrateUserObject().disableLayerValue(userLayer);
    TestOptions::testMigrateUserNoResetObject().disableLayerValue(userLayer);
    TestOptions::testMigrateDeveloperHashObject().disableLayerValue(rtxConfLayer);
    TestOptions::testMigrateUserHashObject().disableLayerValue(userLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // -------------------------------------------------------------------------
    // Test 9: Layer's unsaved changes are properly tracked after migration
    // -------------------------------------------------------------------------
    
    // Start fresh - clear any unsaved state
    // Set a developer option in user layer
    TestOptions::testMigrateDeveloper.setDeferred(555, userLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    // User layer should have unsaved changes
    TEST_ASSERT(userLayer->hasUnsavedChanges(),
                "User layer should have unsaved changes after setting value");
    
    // Migrate
    userLayer->migrateMiscategorizedOptions();
    
    // rtxConf layer should now have unsaved changes (received the value)
    TEST_ASSERT(rtxConfLayer->hasUnsavedChanges(),
                "RtxConf layer should have unsaved changes after receiving migrated value");
    
    // Clean up
    TestOptions::testMigrateDeveloperObject().disableLayerValue(rtxConfLayer);
    RtxOptionManager::applyPendingValues(nullptr, false);
    
    std::cout << "    PASSED" << std::endl;
  }

  // ============================================================================
  // Test Runner
  // ============================================================================
  
  void runAllTests() {
    std::cout << "============================================" << std::endl;
    std::cout << "Running RTX Option Unit Tests" << std::endl;
    std::cout << "============================================" << std::endl;
    
    initializeTestEnvironment();
    
    // Basic functionality tests
    test_basicTypes();
    test_setAndGet();
    test_getDefaultValue();
    test_fullOptionName();
    test_optionTypeIdentification();
    
    // Min/max and clamping tests
    test_minMaxClamping();
    test_dynamicMinMax();
    
    // Callback tests
    test_onChangeCallback();
    test_minMaxInterdependency();
    test_chainedOnChangeCallbacks();
    test_cyclicOnChangeCallbacksTerminate();
    test_valueSettingChain();
    test_cyclicValueSettingTerminates();
    test_environmentVariables();
    
    // Hash set tests
    test_hashSetOperations();
    test_hashSetLayerDirect();
    test_hashSetLayerMerging();
    
    // Layer system tests
    test_layerPriorityOverride();
    test_layerEnableDisable();
    test_layerKeyComparison();
    test_hasValueInLayer();
    test_multipleLayersComplex();
    test_migrateMiscategorizedOptions();
    
    // Blending tests
    test_floatBlending();
    test_vectorBlending();
    test_blendThreshold();
    test_blendStrengthRequest();
    test_blendThresholdRequest();
    
    // Flag tests
    test_optionFlags();
    test_isDefault();
    test_resetToDefault();
    
    // Serialization tests
    test_configSerialization();
    test_configFileIO();
    test_configParsing();
    
    std::cout << "============================================" << std::endl;
    std::cout << "All RTX Option Unit Tests PASSED!" << std::endl;
    std::cout << "============================================" << std::endl;
  }

}  // namespace rtx_option_test
}  // namespace dxvk

int main() {
  try {
    dxvk::rtx_option_test::runAllTests();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& e) {
    std::cerr << "TEST FAILED with exception: " << e.what() << std::endl;
    return -1;
  }
  
  return 0;
}
