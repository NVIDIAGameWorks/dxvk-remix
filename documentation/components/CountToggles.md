# Count Toggles

Counts how many times an input switches from off to on\.<br/><br/>Tracks the number of times a boolean input transitions from false to true, useful for counting button presses or state changes\.

## Component Information

- **Name:** `CountToggles`
- **UI Name:** Count Toggles
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| value | Value | Bool | Input | false | No | 
| resetValue | Reset Value | Float | Input | 0\.0 | No | 

### Value

An input boolean\.  Every time this goes from false to true, the count is incremented\.


### Reset Value

If count reaches this value, it is reset to 0\.  Does nothing if left as 0\.


## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| prevFrameValue |  | Bool | State | true | No | 

### 

The value of the boolean from the previous frame\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| count | Count | Float | Output | 0\.0 | No | 

### Count

The current count value\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
