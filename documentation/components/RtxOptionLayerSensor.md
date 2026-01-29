# Rtx Option Layer Sensor

Reads the state of a configuration layer\.<br/><br/>Outputs whether a given RtxOptionLayer is enabled, along with its blend strength and threshold values\. This can be used to create logic that responds to the state of configuration layers\.

## Component Information

- **Name:** `RtxOptionLayerSensor`
- **UI Name:** Rtx Option Layer Sensor
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| configPath | Config Path | AssetPath | Input | "" | No | 
| priority | Priority | Float | Input | 10000\.0 | Yes | 

### Config Path

The config file for the RtxOptionLayer to read\.


### Priority

The priority for the option layer\. Numbers are rounded to the nearest positive integer\. Higher values are blended on top of lower values\. If multiple layers share the same priority, they are ordered alphabetically by config path\.


**Value Constraints:**

- **Minimum Value:** 101\.0
- **Maximum Value:** 10000000\.0

## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| isEnabled | Is Enabled | Bool | Output | false | No | 
| blendStrength | Blend Strength | Float | Output | 0\.0 | No | 
| blendThreshold | Blend Threshold | Float | Output | 0\.0 | No | 

### Is Enabled

True if the option layer is currently enabled\.


### Blend Strength

The current blend strength of the option layer \(0\.0 = no effect, 1\.0 = full effect\)\.


### Blend Threshold

The current blend threshold for non\-float options \(0\.0 to 1\.0\)\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
