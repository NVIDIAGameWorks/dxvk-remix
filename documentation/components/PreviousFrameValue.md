# Previous Frame Value

Outputs the value from the previous frame\.<br/><br/>Stores the input value and outputs it on the next frame\. Useful for detecting changes between frames or implementing delay effects\.

## Component Information

- **Name:** `PreviousFrameValue`
- **UI Name:** Previous Frame Value
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| input | Input | Any | Input | 0 | No | 

### Input

The value to store for the next frame\.


## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| previousValue |  | Any | State | 0 | No | 

### 

The value from the previous frame\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| output | Output | Any | Output | 0 | No | 

### Output

The value from the previous frame\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | input | output | previousValue |
|---|---|---|---|
| 1 | Bool | Bool | Bool |
| 2 | Float | Float | Float |
| 3 | Float2 | Float2 | Float2 |
| 4 | Float3 | Float3 | Float3 |
| 5 | Float4 | Float4 | Float4 |
| 6 | Enum | Enum | Enum |
| 7 | String | String | String |
| 8 | Hash | Hash | Hash |
| 9 | Prim | Prim | Prim |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
