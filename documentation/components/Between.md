# Between

Tests if a value is within a range \(inclusive\)\.<br/><br/>Returns true if the value is >= Min Value AND <= Max Value\. Combines greater\-than\-or\-equal, less\-than\-or\-equal, and boolean AND into a single component\.

## Component Information

- **Name:** `Between`
- **UI Name:** Between
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| value | Value | Float | Input | 0\.0 | No | 
| minValue | Min Value | Float | Input | 0\.0 | No | 
| maxValue | Max Value | Float | Input | 1\.0 | No | 

### Value

The value to test\.


### Min Value

The minimum value of the range \(inclusive\)\.


### Max Value

The maximum value of the range \(inclusive\)\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| result | Result | Bool | Output | false | No | 

### Result

True if value is greater than or equal to Min Value AND less than or equal to Max Value\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
