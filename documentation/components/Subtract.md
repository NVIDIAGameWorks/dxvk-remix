# Subtract

Subtracts one number or vector from another\.<br/><br/>Vector \- Number will subtract the number from all components of the vector\. Vector \- Vector will error if the vectors aren't the same size\.

## Component Information

- **Name:** `Subtract`
- **UI Name:** Subtract
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| a | A | NumberOrVector | Input | 0 | No | 
| b | B | NumberOrVector | Input | 0 | No | 

### A

The value to subtract from\.


### B

The value to subtract\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| difference | Difference | NumberOrVector | Output | 0 | No | 

### Difference

A \- B


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | a | b | difference |
|---|---|---|---|
| 1 | Float | Float | Float |
| 2 | Float2 | Float2 | Float2 |
| 3 | Float3 | Float3 | Float3 |
| 4 | Float4 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
