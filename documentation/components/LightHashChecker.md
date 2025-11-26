# Light Hash Checker

Detects if a specific light is currently active in the scene\.<br/><br/>Checks if a specific light hash is present in the current frame's light table\.

## Component Information

- **Name:** `LightHashChecker`
- **UI Name:** Light Hash Checker
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| lightHash | Light Hash | Hash | Input | 0x0 | No | 

### Light Hash

The light hash to check for usage in the current frame\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| isUsed | Is Used | Bool | Output | false | No | 

### Is Used

True if the light hash was used in the current frame\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
