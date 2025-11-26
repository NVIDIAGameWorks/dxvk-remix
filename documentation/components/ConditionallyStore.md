# Conditionally Store

Stores a value when a condition is true, otherwise keeps the previous value\.<br/><br/>If the store input is true, captures the input value and stores it\. If the store input is false, continues outputting the previously stored value\. Useful for sample\-and\-hold behavior\.

## Component Information

- **Name:** `ConditionallyStore`
- **UI Name:** Conditionally Store
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| store | Store | Bool | Input | false | No | 
| input | Input | Any | Input | 0 | No | 

### Store

If true, write the input value to state\. If false, keep the previous stored value\.


### Input

The value to store when store is true\.


## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| storedValue |  | Any | State | 0 | No | 

### 

The stored value\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| output | Output | Any | Output | 0 | No | 

### Output

The currently stored value\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | input | output | store | storedValue |
|---|---|---|---|---|
| 1 | Bool | Bool | Bool | Bool |
| 2 | Float | Float | Bool | Float |
| 3 | Float2 | Float2 | Bool | Float2 |
| 4 | Float3 | Float3 | Bool | Float3 |
| 5 | Float4 | Float4 | Bool | Float4 |
| 6 | Enum | Enum | Bool | Enum |
| 7 | String | String | Bool | String |
| 8 | Hash | Hash | Bool | Hash |
| 9 | Prim | Prim | Bool | Prim |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
