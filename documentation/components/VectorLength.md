# Vector Length

Calculates the length \(magnitude\) of a vector\.<br/><br/>Computes the Euclidean length of the vector using the formula: sqrt\(x² \+ y² \+ z² \+ \.\.\.\)\.

## Component Information

- **Name:** `VectorLength`
- **UI Name:** Vector Length
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| input | Input | NumberOrVector | Input | 0 | No | 

### Input

The value to measure\. For vectors, returns length\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| length | Length | Float | Output | 0\.0 | No | 

### Length

The length \(magnitude\) of the input vector\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | input | length |
|---|---|---|
| 1 | Float2 | Float |
| 2 | Float3 | Float |
| 3 | Float4 | Float |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
