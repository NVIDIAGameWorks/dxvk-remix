# Normalize

Normalizes a vector to have length 1\.<br/><br/>Divides the vector by its length to produce a unit vector \(length 1\) in the same direction\. If the input vector has zero length, returns a default vector to avoid division by zero\.

## Component Information

- **Name:** `Normalize`
- **UI Name:** Normalize
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| input | Input | NumberOrVector | Input | 0 | No | 

### Input

The vector to normalize\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| output | Output | NumberOrVector | Output | 0 | No | 

### Output

The normalized vector with length 1\. Returns \(0,1\), \(0,0,1\), or \(0,0,0,1\) if the input vector has zero length\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | input | output |
|---|---|---|
| 1 | Float2 | Float2 |
| 2 | Float3 | Float3 |
| 3 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
