# Invert

Outputs 1 minus the input value\.<br/><br/>Calculates 1 \- input\. Useful for inverting normalized values \(e\.g\., turning 0\.2 into 0\.8\)\.

## Component Information

- **Name:** `Invert`
- **UI Name:** Invert
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| input | Input | NumberOrVector | Input | 0 | No | 

### Input

The value to invert\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| output | Output | NumberOrVector | Output | 0 | No | 

### Output

1 \- input


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | input | output |
|---|---|---|
| 1 | Float | Float |
| 2 | Float2 | Float2 |
| 3 | Float3 | Float3 |
| 4 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
