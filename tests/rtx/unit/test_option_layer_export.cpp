/*
* Copyright (c) 2025-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "../../test_utils.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <filesystem>

namespace dxvk {
  // Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_option_layer_export.log");

namespace option_layer_export_test {

  // ============================================================================
  // Test Configuration and Helpers
  // ============================================================================
  
  // Test layer keys for unit tests (using dynamic priority range)
  static constexpr RtxOptionLayerKey kTestLayerKey = { 1000, "TestExportLayer" };
  
  // Helper macro for test assertions
  #define TEST_ASSERT(condition, message) \
    do { \
      if (!(condition)) { \
        std::ostringstream oss; \
        oss << "FAILED: " << __FUNCTION__ << " line " << __LINE__ << ": " << message; \
        throw DxvkError(oss.str()); \
      } \
    } while(0)

  // Helper to clean up test files
  void cleanupTestFile(const std::string& path) {
    if (std::filesystem::exists(path)) {
      std::filesystem::remove(path);
    }
  }

  // Helper to read a config file and verify option values
  std::string readOptionFromFile(const std::string& filePath, const std::string& optionName) {
    Config config = Config::getOptionLayerConfig(filePath);
    if (!config.findOption(optionName.c_str())) {
      return "";
    }
    return config.getOption<std::string>(optionName.c_str(), "");
  }

  // ============================================================================
  // Test Options (created as test globals)
  // ============================================================================
  
  // Define test options to use in the tests - must be in a class for RTX_OPTION macro
  class TestExportOptions {
  public:
    RTX_OPTION("rtx.test.export", int32_t, testIntOption, 42, "Test integer option for export");
    RTX_OPTION("rtx.test.export", float, testFloatOption, 3.14f, "Test float option for export");
    RTX_OPTION("rtx.test.export", std::string, testStringOption, "default", "Test string option for export");
    RTX_OPTION("rtx.test.export", fast_unordered_set, testHashSetOption, {}, "Test hash set option for export");
  };

  // ============================================================================
  // Test: Export Added Options (New File)
  // ============================================================================
  
  void testExportAddedOptionsNewFile() {
    std::cout << "Testing: Export added options to new file..." << std::endl;
    
    const std::string testFile = "test_export_added_new.conf";
    const std::string layerFile = "test_layer_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Create a layer with an empty config (simulates rtx.conf with no options)
      Config emptyConfig;
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &emptyConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Set values on the layer (these will be "added" since the saved config is empty)
      TestExportOptions::testIntOption.setImmediately(100, layer);
      TestExportOptions::testFloatOption.setImmediately(2.71f, layer);
      TestExportOptions::testStringOption.setImmediately(std::string("test_value"), layer);
      
      // Export unsaved changes
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Verify the exported file contains the correct values
      TEST_ASSERT(std::filesystem::exists(testFile), "Export file should exist");
      
      std::string intValue = readOptionFromFile(testFile, "rtx.test.export.testIntOption");
      TEST_ASSERT(intValue == "100", "Int option should be exported correctly");
      
      std::string floatValue = readOptionFromFile(testFile, "rtx.test.export.testFloatOption");
      TEST_ASSERT(std::stof(floatValue) == 2.71f, "Float option should be exported correctly");
      
      std::string stringValue = readOptionFromFile(testFile, "rtx.test.export.testStringOption");
      TEST_ASSERT(stringValue == "test_value", "String option should be exported correctly");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test: Export Modified Options
  // ============================================================================
  
  void testExportModifiedOptions() {
    std::cout << "Testing: Export modified options..." << std::endl;
    
    const std::string testFile = "test_export_modified.conf";
    const std::string layerFile = "test_layer_modified_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Create a config with initial values
      Config initialConfig;
      initialConfig.setOption("rtx.test.export.testIntOption", 42);
      initialConfig.setOption("rtx.test.export.testFloatOption", 3.14f);
      
      // Create a layer with the initial config
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Modify the values
      TestExportOptions::testIntOption.setImmediately(200, layer);
      TestExportOptions::testFloatOption.setImmediately(6.28f, layer);
      
      // Export unsaved changes
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Verify the exported file contains the modified values
      std::string intValue = readOptionFromFile(testFile, "rtx.test.export.testIntOption");
      TEST_ASSERT(intValue == "200", "Modified int option should be exported");
      
      std::string floatValue = readOptionFromFile(testFile, "rtx.test.export.testFloatOption");
      TEST_ASSERT(std::stof(floatValue) == 6.28f, "Modified float option should be exported");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test: Export HashSet - Add New Hashes
  // ============================================================================
  
  void testExportHashSetAddNew() {
    std::cout << "Testing: Export hash set with new hashes..." << std::endl;
    
    const std::string testFile = "test_export_hashset_add.conf";
    const std::string layerFile = "test_layer_hashset_add_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Create a config with initial hashes
      Config initialConfig;
      initialConfig.setOption("rtx.test.export.testHashSetOption", std::string("0x1111111111111111, 0x2222222222222222"));
      
      // Create a layer with the initial config
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Add new hashes to the set
      fast_unordered_set newHashes;
      newHashes.insert(0x1111111111111111);  // Already in saved config
      newHashes.insert(0x2222222222222222);  // Already in saved config  
      newHashes.insert(0x3333333333333333);  // New hash
      newHashes.insert(0x4444444444444444);  // New hash
      TestExportOptions::testHashSetOption.setImmediately(newHashes, layer);
      
      // Export unsaved changes
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Verify the exported file contains only the new hashes
      Config exportedConfig = Config::getOptionLayerConfig(testFile);
      std::vector<std::string> exportedHashes = exportedConfig.getOption<std::vector<std::string>>("rtx.test.export.testHashSetOption");
      HashSetLayer parsedHashes;
      parsedHashes.parseFromStrings(exportedHashes);
      
      // Should contain only the two new hashes (delta export)
      TEST_ASSERT(parsedHashes.hasPositive(0x3333333333333333), "Should contain new hash 0x3333");
      TEST_ASSERT(parsedHashes.hasPositive(0x4444444444444444), "Should contain new hash 0x4444");
      TEST_ASSERT(!parsedHashes.hasPositive(0x1111111111111111), "Should NOT contain old hash 0x1111 (already in saved config)");
      TEST_ASSERT(!parsedHashes.hasPositive(0x2222222222222222), "Should NOT contain old hash 0x2222 (already in saved config)");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test: Export HashSet - Add then Remove Creates Negative Opinion
  // ============================================================================
  
  void testExportHashSetAddThenRemove() {
    std::cout << "Testing: Add hash then remove hash creates negative opinion..." << std::endl;
    
    const std::string testFile = "test_export_hashset_add_remove.conf";
    const std::string layerFile = "test_layer_hashset_add_remove_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Create initial config with NO hashes
      Config initialConfig;
      
      // Create layer with empty hash set
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Add a hash
      TestExportOptions::testHashSetOption.addHash(0x1111111111111111, layer);
      
      // Then remove the same hash - this should create a negative opinion
      TestExportOptions::testHashSetOption.removeHash(0x1111111111111111, layer);
      
      // Export changes
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Parse the exported file
      Config exportedConfig = Config::getOptionLayerConfig(testFile);
      std::vector<std::string> exportedHashStrings = exportedConfig.getOption<std::vector<std::string>>("rtx.test.export.testHashSetOption");
      HashSetLayer exportedHashes;
      exportedHashes.parseFromStrings(exportedHashStrings);
      
      // The exported file should contain a negative opinion (the user explicitly removed this hash)
      TEST_ASSERT(exportedHashes.hasNegative(0x1111111111111111), "Should have negative opinion after add→remove");
      TEST_ASSERT(!exportedHashes.hasPositive(0x1111111111111111), "Should NOT have positive opinion after add→remove");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test: Export HashSet - Conflicting Opinions (Positive vs Negative)
  // ============================================================================
  
  void testExportHashSetConflictingOpinions() {
    std::cout << "Testing: Export hash set - removing hash to create negative opinion..." << std::endl;
    
    const std::string testFile = "test_export_hashset_negative.conf";
    const std::string layerFile = "test_layer_hashset_negative_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Create initial config with two hashes
      Config initialConfig;
      initialConfig.setOption("rtx.test.export.testHashSetOption", std::string("0x1111111111111111, 0x2222222222222222"));
      
      // Create layer with initial hashes
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Verify initial state has both hashes
      const Config& savedConfig = layer->getConfig();
      std::vector<std::string> savedHashStrings = savedConfig.getOption<std::vector<std::string>>("rtx.test.export.testHashSetOption");
      HashSetLayer savedHashes;
      savedHashes.parseFromStrings(savedHashStrings);
      TEST_ASSERT(savedHashes.hasPositive(0x1111111111111111), "Should have positive opinion for hash 0x1111 initially");
      TEST_ASSERT(savedHashes.hasPositive(0x2222222222222222), "Should have positive opinion for hash 0x2222 initially");
      
      // Use the proper API to remove hash 0x1111 - this creates a negative opinion
      TestExportOptions::testHashSetOption.removeHash(0x1111111111111111, layer);
      
      // Export to a new file
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Parse the exported file
      Config exportedConfig = Config::getOptionLayerConfig(testFile);
      std::vector<std::string> exportedHashStrings = exportedConfig.getOption<std::vector<std::string>>("rtx.test.export.testHashSetOption");
      HashSetLayer exportedHashes;
      exportedHashes.parseFromStrings(exportedHashStrings);
      
      // The exported file should contain a negative opinion for the removed hash
      TEST_ASSERT(exportedHashes.hasNegative(0x1111111111111111), "Should have negative opinion for removed hash 0x1111");
      TEST_ASSERT(!exportedHashes.hasPositive(0x1111111111111111), "Should NOT have positive opinion for removed hash 0x1111");
      TEST_ASSERT(!exportedHashes.hasPositive(0x2222222222222222), "Should NOT have positive opinion for unchanged hash 0x2222 (delta export)");
      TEST_ASSERT(!exportedHashes.hasNegative(0x2222222222222222), "Should NOT have negative opinion for kept hash 0x2222");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test: Export HashSet - Merge with Existing File
  // ============================================================================
  
  void testExportHashSetMergeWithExistingFile() {
    std::cout << "Testing: Export hash set merge with existing file..." << std::endl;
    
    const std::string testFile = "test_export_hashset_merge.conf";
    const std::string layerFile = "test_layer_hashset_merge_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Create an existing export file with some hashes
      Config existingConfig;
      existingConfig.setOption("rtx.test.export.testHashSetOption", std::string("0x5555555555555555, 0x6666666666666666"));
      Config::serializeCustomConfig(existingConfig, testFile, "rtx.");
      
      // Create a layer with initial config (different from existing file)
      Config initialConfig;
      initialConfig.setOption("rtx.test.export.testHashSetOption", std::string("0x1111111111111111"));
      
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Add new hashes (delta from initial config)
      fast_unordered_set newHashes;
      newHashes.insert(0x1111111111111111);  // Already in initial config
      newHashes.insert(0x7777777777777777);  // New hash (delta)
      TestExportOptions::testHashSetOption.setImmediately(newHashes, layer);
      
      // Export unsaved changes (should merge with existing file)
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Verify the exported file contains merged hashes
      Config exportedConfig = Config::getOptionLayerConfig(testFile);
      std::vector<std::string> exportedHashes = exportedConfig.getOption<std::vector<std::string>>("rtx.test.export.testHashSetOption");
      HashSetLayer parsedHashes;
      parsedHashes.parseFromStrings(exportedHashes);
      
      // Should contain: existing file hashes + new delta
      TEST_ASSERT(parsedHashes.hasPositive(0x5555555555555555), "Should keep existing hash 0x5555");
      TEST_ASSERT(parsedHashes.hasPositive(0x6666666666666666), "Should keep existing hash 0x6666");
      TEST_ASSERT(parsedHashes.hasPositive(0x7777777777777777), "Should add new delta hash 0x7777");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test: Export HashSet - Conflicting Opinions in Merge
  // ============================================================================
  
  void testExportHashSetConflictInMerge() {
    std::cout << "Testing: Export hash set with conflicting opinions during merge..." << std::endl;
    
    const std::string testFile = "test_export_hashset_conflict_merge.conf";
    const std::string layerFile = "test_layer_hashset_conflict_merge_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Test Case 1: Existing file has positive, layer adds negative (should override to negative)
      // Create an existing export file with positive opinions for two hashes
      Config existingConfig;
      existingConfig.setOption("rtx.test.export.testHashSetOption", std::string("0x1111111111111111, 0x2222222222222222"));
      Config::serializeCustomConfig(existingConfig, testFile, "rtx.");
      
      // Create a layer with empty initial config
      Config initialConfig;
      
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Add hash 0x2222 (positive opinion)
      TestExportOptions::testHashSetOption.addHash(0x2222222222222222, layer);
      
      // Add negative opinion for 0x1111 (conflicts with existing file's positive)
      TestExportOptions::testHashSetOption.removeHash(0x1111111111111111, layer);
      
      // Export unsaved changes (negative should override file's positive)
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Verify the exported file has negative opinion for 0x1111, but still has 0x2222
      Config exportedConfig = Config::getOptionLayerConfig(testFile);
      std::vector<std::string> exportedHashes = exportedConfig.getOption<std::vector<std::string>>("rtx.test.export.testHashSetOption");
      HashSetLayer parsedHashes;
      parsedHashes.parseFromStrings(exportedHashes);
      
      TEST_ASSERT(parsedHashes.hasNegative(0x1111111111111111), "Should have negative opinion for 0x1111 (overriding file's positive)");
      TEST_ASSERT(!parsedHashes.hasPositive(0x1111111111111111), "Should NOT have positive opinion for 0x1111");
      TEST_ASSERT(parsedHashes.hasPositive(0x2222222222222222), "Should still have positive opinion for 0x2222");
      TEST_ASSERT(!parsedHashes.hasNegative(0x2222222222222222), "Should NOT have negative opinion for 0x2222");
      
      // Test Case 2: Existing file has negative, layer adds positive (should override to positive)
      // Update the file to have a negative opinion for 0x3333 and positive for 0x4444
      existingConfig.setOption("rtx.test.export.testHashSetOption", std::string("-0x3333333333333333, 0x4444444444444444"));
      Config::serializeCustomConfig(existingConfig, testFile, "rtx.");
      
      // Re-acquire layer with empty config
      RtxOptionManager::releaseLayer(layer);
      layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to re-acquire layer");
      
      // Add positive opinion for 0x3333 (conflicts with file's negative)
      TestExportOptions::testHashSetOption.addHash(0x3333333333333333, layer);
      
      // Add positive opinion for 0x4444 (matches file's positive - redundant but should work)
      TestExportOptions::testHashSetOption.addHash(0x4444444444444444, layer);
      
      // Export unsaved changes (positive should override file's negative)
      result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Verify the exported file has positive opinion for 0x3333 (overriding negative) and 0x4444
      exportedConfig = Config::getOptionLayerConfig(testFile);
      exportedHashes = exportedConfig.getOption<std::vector<std::string>>("rtx.test.export.testHashSetOption");
      parsedHashes.clearAll();
      parsedHashes.parseFromStrings(exportedHashes);
      
      TEST_ASSERT(parsedHashes.hasPositive(0x3333333333333333), "Should have positive opinion for 0x3333 (overriding file's negative)");
      TEST_ASSERT(!parsedHashes.hasNegative(0x3333333333333333), "Should NOT have negative opinion for 0x3333");
      TEST_ASSERT(parsedHashes.hasPositive(0x4444444444444444), "Should have positive opinion for 0x4444");
      TEST_ASSERT(!parsedHashes.hasNegative(0x4444444444444444), "Should NOT have negative opinion for 0x4444");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test: Export Non-HashSet Options - Overwrite in Merge
  // ============================================================================
  
  void testExportNonHashSetMergeOverwrite() {
    std::cout << "Testing: Export non-hash set options overwrite in merge..." << std::endl;
    
    const std::string testFile = "test_export_merge_overwrite.conf";
    const std::string layerFile = "test_layer_merge_overwrite_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Create an existing export file
      Config existingConfig;
      existingConfig.setOption("rtx.test.export.testIntOption", 999);
      existingConfig.setOption("rtx.test.export.testStringOption", std::string("old_value"));
      Config::serializeCustomConfig(existingConfig, testFile, "rtx.");
      
      // Create a layer with initial config
      Config initialConfig;
      initialConfig.setOption("rtx.test.export.testIntOption", 42);
      
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Modify the value
      TestExportOptions::testIntOption.setImmediately(300, layer);
      
      // Export unsaved changes (should overwrite existing value)
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(result, "Export should succeed");
      
      // Verify the exported file has the new value (not merged, overwritten)
      std::string intValue = readOptionFromFile(testFile, "rtx.test.export.testIntOption");
      TEST_ASSERT(intValue == "300", "Should overwrite with new value, not merge");
      
      // Verify other option from existing file is preserved
      std::string stringValue = readOptionFromFile(testFile, "rtx.test.export.testStringOption");
      TEST_ASSERT(stringValue == "old_value", "Should preserve unrelated options from existing file");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test: Export with No Unsaved Changes
  // ============================================================================
  
  void testExportNoUnsavedChanges() {
    std::cout << "Testing: Export with no unsaved changes..." << std::endl;
    
    const std::string testFile = "test_export_no_changes.conf";
    const std::string layerFile = "test_layer_no_changes_source.conf";
    cleanupTestFile(testFile);
    cleanupTestFile(layerFile);
    
    try {
      // Create a layer with initial config
      Config initialConfig;
      initialConfig.setOption("rtx.test.export.testIntOption", 42);
      
      const RtxOptionLayer* layer = RtxOptionManager::acquireLayer(layerFile, kTestLayerKey, 1.0f, 0.1f, false, &initialConfig);
      TEST_ASSERT(layer != nullptr, "Failed to acquire layer");
      
      // Don't modify anything - no unsaved changes
      
      // Try to export (should fail or return false)
      bool result = layer->exportUnsavedChanges(testFile);
      TEST_ASSERT(!result, "Export should fail when there are no unsaved changes");
      
      // Verify no file was created
      TEST_ASSERT(!std::filesystem::exists(testFile), "No file should be created when there are no changes");
      
      // Cleanup
      RtxOptionManager::releaseLayer(layer);
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      
      std::cout << "  PASSED" << std::endl;
    } catch (const DxvkError& e) {
      cleanupTestFile(testFile);
      cleanupTestFile(layerFile);
      throw;
    }
  }

  // ============================================================================
  // Test Runner
  // ============================================================================
  
  void runAllTests() {
    std::cout << "\n=== Running Option Layer Export Tests ===" << std::endl;
    
    try {
      testExportAddedOptionsNewFile();
      testExportModifiedOptions();
      testExportHashSetAddNew();
      testExportHashSetAddThenRemove();
      testExportHashSetConflictingOpinions();
      testExportHashSetMergeWithExistingFile();
      testExportHashSetConflictInMerge();
      testExportNonHashSetMergeOverwrite();
      testExportNoUnsavedChanges();
      
      std::cout << "\n=== All Option Layer Export Tests PASSED ===" << std::endl;
    } catch (const DxvkError& e) {
      std::cerr << "\n=== TEST SUITE FAILED ===" << std::endl;
      std::cerr << e.message() << std::endl;
      throw;
    }
  }

} // namespace option_layer_export_test

} // namespace dxvk

int main(int argc, char** argv) {
  try {
    dxvk::option_layer_export_test::runAllTests();
    return 0;
  } catch (const dxvk::DxvkError& e) {
    std::cerr << "Test failed with error: " << e.message() << std::endl;
    return -1;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return -1;
  }
}
