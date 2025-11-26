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

### A

The first value to compare\.


### B

The second value to compare\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| result | Result | Bool | Output | false | No | 

### Result

True if A == B, false otherwise


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | a | b | result |
|---|---|---|---|
| 1 | Float | Float | Bool |
| 2 | Float2 | Float2 | Bool |
| 3 | Float3 | Float3 | Bool |
| 4 | Float4 | Float4 | Bool |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
