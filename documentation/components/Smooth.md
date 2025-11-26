# Smooth

Applies exponential smoothing to a value over time\.<br/><br/>Uses a moving average filter to smooth out rapid changes in the input value\. The smoothing factor controls how much smoothing is applied: 0 means output never changes\. Larger values = faster changes\. <br/>

## Component Information

- **Name:** `Smooth`
- **UI Name:** Smooth
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| input | Input | NumberOrVector | Input | 0 | No | 
| smoothingFactor | Smoothing Factor | Float | Input | 0\.1 | Yes | 

### Input

The value to smooth\.


### Smoothing Factor

The smoothing factor \(0\-1000\)\. 0 means output never changes\. Larger values = faster changes\.<br/><br/>Time for output to be within 1% of input for different factors:<br/>\- 1: 6\.6 seconds<br/>\- 10: 0\.66 seconds<br/>\- 100: 0\.066 seconds<br/>\- 1000: 0\.0066 seconds<br/><br/>Formula: output = lerp\(input, previousOutput, exp2\(\-smoothingFactor\*deltaTime\)\)


**Value Constraints:**

- **Minimum Value:** 0\.0
- **Maximum Value:** 1000\.0

## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| initialized |  | Bool | State | false | No | 

### 

Tracks if the smooth value has been initialized\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| output | Output | NumberOrVector | Output | 0 | No | 

### Output

The smoothed output value\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | initialized | input | output | smoothingFactor |
|---|---|---|---|---|
| 1 | Bool | Float | Float | Float |
| 2 | Bool | Float2 | Float2 | Float |
| 3 | Bool | Float3 | Float3 | Float |
| 4 | Bool | Float4 | Float4 | Float |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
