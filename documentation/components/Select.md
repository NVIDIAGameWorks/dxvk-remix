# Select

Selects between two values based on a boolean condition\.<br/><br/>If the condition is true, outputs Input A\. If the condition is false, outputs Input B\. Acts like a ternary operator or if\-else statement\.

## Component Information

- **Name:** `Select`
- **UI Name:** Select
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| condition | Condition | Bool | Input | false | No | 
| inputA | Input A | Any | Input | 0 | No | 
| inputB | Input B | Any | Input | 0 | No | 

### Condition

If true, output A\. If false, output B\.


### Input A

The value to output when condition is true\.


### Input B

The value to output when condition is false\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| output | Output | Any | Output | 0 | No | 

### Output

The selected value based on the condition\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | condition | inputA | inputB | output |
|---|---|---|---|---|
| 1 | Bool | Bool | Bool | Bool |
| 2 | Bool | Float | Float | Float |
| 3 | Bool | Float2 | Float2 | Float2 |
| 4 | Bool | Float3 | Float3 | Float3 |
| 5 | Bool | Float4 | Float4 | Float4 |
| 6 | Bool | Enum | Enum | Enum |
| 7 | Bool | String | String | String |
| 8 | Bool | Hash | Hash | Hash |
| 9 | Bool | Prim | Prim | Prim |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
