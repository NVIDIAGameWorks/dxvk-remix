# Divide

Divides one number or vector by another\.<br/><br/>Vector / Number will divide all components of the vector by the number\. Vector / vector will divide each piece separately, to create \(a\.x / b\.x, a\.y / b\.y, \.\.\.\)\. Vector / Vector will error if the vectors aren't the same size\.<br/><br/>Note: Division by zero will produce infinity or NaN\.

## Component Information

- **Name:** `Divide`
- **UI Name:** Divide
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| a | A | NumberOrVector | Input | 0 | No | 
| b | B | NumberOrVector | Input | 0 | No | 

### A

The dividend \(value to be divided\)\.


### B

The divisor \(value to divide by\)\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| quotient | Quotient | NumberOrVector | Output | 0 | No | 

### Quotient

A / B


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | a | b | quotient |
|---|---|---|---|
| 1 | Float | Float | Float |
| 2 | Float2 | Float | Float2 |
| 3 | Float2 | Float2 | Float2 |
| 4 | Float3 | Float | Float3 |
| 5 | Float3 | Float3 | Float3 |
| 6 | Float4 | Float | Float4 |
| 7 | Float4 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
