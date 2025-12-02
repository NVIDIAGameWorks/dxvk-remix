# Fog Hash Checker

Detects if a specific fog state is currently active in the scene\.<br/><br/>Checks if a given fog hash matches the current frame's fog hash\.

## Component Information

- **Name:** `FogHashChecker`
- **UI Name:** Fog Hash Checker
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| fogHash | Fog Hash | Hash | Input | 0x0 | No | 

### Fog Hash

The fog hash to check against the current frame's fog hash\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| isMatch | Is Match | Bool | Output | false | No | 

### Is Match

True if the given fog hash matches the current frame's fog hash\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
