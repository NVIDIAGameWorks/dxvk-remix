# Velocity

Detects the rate of change of a value from frame to frame\.<br/><br/>Calculates the difference between the current value and the previous frame's value\. Outputs the change per frame \(velocity = \(current \- previous\) / deltaTime\)\.

## Component Information

- **Name:** `Velocity`
- **UI Name:** Velocity
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| input | Input | NumberOrVector | Input | 0 | No | 

### Input

The value to detect changes from\.


## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| previousValue |  | NumberOrVector | State | 0 | No | 

### 

The value from the previous frame\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| velocity | Velocity | NumberOrVector | Output | 0 | No | 

### Velocity

The change in value from the previous frame \(current \- previous\) / deltaTime\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | input | previousValue | velocity |
|---|---|---|---|
| 1 | Float | Float | Float |
| 2 | Float2 | Float2 | Float2 |
| 3 | Float3 | Float3 | Float3 |
| 4 | Float4 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
