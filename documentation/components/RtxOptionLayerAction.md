# Rtx Option Layer Action

Controls an RtxOptionLayer by name, allowing dynamic enable/disable, strength adjustment, and threshold control\. This can be used to activate configuration layers at runtime based on game state or other conditions\. The layer is created if it doesn't exist, and managed with reference counting\. Each layer requires a unique priority value \- if multiple components specify the same priority, an error will occur\.

## Component Information

- **Name:** `RtxOptionLayerAction`
- **UI Name:** Rtx Option Layer Action
- **Version:** 1
- **Categories:** Act

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| configPath | Config Path | AssetPath | Input | "" | No | 
| enabled | Enabled | Bool | Input | true | Yes | 
| blendStrength | Blend Strength | Float | Input | 1\.0 | Yes | 
| blendThreshold | Blend Threshold | Float | Input | 0\.1 | Yes | 
| priority | Priority | Uint32 | Input | 10000 | Yes | 

### Config Path

The config file for the RtxOptionLayer to control\.


### Enabled

If true, the option layer is enabled and its settings are applied\. If false, the layer is disabled\. If multiple components control the same layer, it will be enabled if ANY of them request it\.


### Blend Strength

The blend strength for the option layer \(0\.0 = no effect, 1\.0 = full effect\.\)<br/><br/>Lowest priority layer uses LERP to blend with default value, then each higher priority layer uses LERP to blend with the previous layer's result\.<br/><br/>If multiple components control the same layer, the MAX blend strength will be used\.


**Value Constraints:**

- **Minimum Value:** 0\.0
- **Maximum Value:** 1\.0

### Blend Threshold

The blend threshold for non\-float options \(0\.0 to 1\.0\)\. Non\-float options are only applied when blend strength exceeds this threshold\. If multiple components control the same layer, the MINIMUM blend threshold will be used\.


**Value Constraints:**

- **Minimum Value:** 0\.0
- **Maximum Value:** 1\.0

### Priority

The priority for the option layer\. Higher values are blended onto lower values\. Must be unique across all layers\.


**Value Constraints:**

- **Minimum Value:** 101
- **Maximum Value:** 4294967294

## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| cachedLayerPtr |  | Uint64 | State | 0 | No | 

### 

Cached pointer to the RtxOptionLayer \(internal use\)\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
