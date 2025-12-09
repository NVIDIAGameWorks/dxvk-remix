# Equal To

Returns true if A is equal to B, false otherwise\.<br/><br/>For floating point values, this performs exact equality comparison\. Vector == Vector compares all components\.

## Component Information

- **Name:** `EqualTo`
- **UI Name:** Equal To
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| a | A | NumberOrVector | Input | 0 | No | 
| b | B | NumberOrVector | Input | 0 | No | 
| tolerance | Tolerance | Float | Input | 0\.00001 | No | 

### A

The first value to compare\.


### B

The second value to compare\.


### Tolerance

The tolerance for rounding errors\. Math operations with floating point values are not exact, so equality comparisons should allow for slightly different values\. If the difference between A and B is less than Tolerance, the result will be true\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| result | Result | Bool | Output | false | No | 

### Result

True if A == B, false otherwise


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | a | b | result | tolerance |
|---|---|---|---|---|
| 1 | Float | Float | Bool | Float |
| 2 | Float2 | Float2 | Bool | Float |
| 3 | Float3 | Float3 | Bool | Float |
| 4 | Float4 | Float4 | Bool | Float |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
