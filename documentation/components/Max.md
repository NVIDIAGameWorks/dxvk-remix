# Max

Returns the larger of two values\.<br/><br/>Outputs the maximum value between A and B\.

## Component Information

- **Name:** `Max`
- **UI Name:** Max
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| a | A | NumberOrVector | Input | 0 | No | 
| b | B | NumberOrVector | Input | 0 | No | 

### A

The first value\.


### B

The second value\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| result | Result | NumberOrVector | Output | 0 | No | 

### Result

The maximum of A and B\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | a | b | result |
|---|---|---|---|
| 1 | Float | Float | Float |
| 2 | Float2 | Float2 | Float2 |
| 3 | Float3 | Float3 | Float3 |
| 4 | Float4 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
