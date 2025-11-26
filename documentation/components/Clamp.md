# Clamp

Constrains a value to a specified range\.<br/><br/>If the value is less than Min Value, returns Min Value\. If the value is greater than Max Value, returns Max Value\. Otherwise, returns the value unchanged\. Applies to each component of a vector individually\.

## Component Information

- **Name:** `Clamp`
- **UI Name:** Clamp
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| value | Value | NumberOrVector | Input | 0 | No | 
| minValue | Min Value | Float | Input | 0\.0 | No | 
| maxValue | Max Value | Float | Input | 1\.0 | No | 

### Value

The value to clamp\.


### Min Value

The minimum allowed value\.


### Max Value

The maximum allowed value\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| result | Result | NumberOrVector | Output | 0 | No | 

### Result

The clamped value, constrained to \[Min Value, Max Value\]\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | maxValue | minValue | result | value |
|---|---|---|---|---|
| 1 | Float | Float | Float | Float |
| 2 | Float | Float | Float2 | Float2 |
| 3 | Float | Float | Float3 | Float3 |
| 4 | Float | Float | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
