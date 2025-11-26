# Add

Adds two numbers or vectors together\.<br/><br/>Vector \+ Number will add the number to all components of the vector\. Vector \+ Vector will add each piece separately, to create \(a\.x \+ b\.x, a\.y \+ b\.y, \.\.\.\)\. Vector \+ Vector will error if the vectors aren't the same size\.

## Component Information

- **Name:** `Add`
- **UI Name:** Add
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| a | A | NumberOrVector | Input | 0 | No | 
| b | B | NumberOrVector | Input | 0 | No | 

### A

The first value to be added\.


### B

The second value to be added\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| sum | Sum | NumberOrVector | Output | 0 | No | 

### Sum

A \+ B


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | a | b | sum |
|---|---|---|---|
| 1 | Float | Float | Float |
| 2 | Float2 | Float2 | Float2 |
| 3 | Float3 | Float3 | Float3 |
| 4 | Float4 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
