# Multiply

Multiplies two values together\.<br/><br/>Vector \* Number will multiply all components of the vector by the number\. Vector \* Vector will multiply each piece separately, to create \(a\.x \* b\.x, a\.y \* b\.y, \.\.\.\)\. Vector \* Vector will error if the vectors aren't the same size\.

## Component Information

- **Name:** `Multiply`
- **UI Name:** Multiply
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| a | A | NumberOrVector | Input | 0 | No | 
| b | B | NumberOrVector | Input | 0 | No | 

### A

The first value to be multiplied\.


### B

The second value to be multiplied\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| product | Product | NumberOrVector | Output | 0 | No | 

### Product

A \* B


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | a | b | product |
|---|---|---|---|
| 1 | Float | Float | Float |
| 2 | Float | Float2 | Float2 |
| 3 | Float | Float3 | Float3 |
| 4 | Float | Float4 | Float4 |
| 5 | Float2 | Float | Float2 |
| 6 | Float2 | Float2 | Float2 |
| 7 | Float3 | Float | Float3 |
| 8 | Float3 | Float3 | Float3 |
| 9 | Float4 | Float | Float4 |
| 10 | Float4 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
