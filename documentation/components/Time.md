# Time

Provides a continuously increasing time value for creating animations and time\-based effects\.<br/><br/>Outputs the time in seconds since the component was created\. Can be paused and speed\-adjusted\.

## Component Information

- **Name:** `Time`
- **UI Name:** Time
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| enabled | Enabled | Bool | Input | true | Yes | 
| resetWhenDisabled | Reset When Disabled | Bool | Input | true | Yes | 
| speedMultiplier | Speed Multiplier | Float | Input | 1\.0 | Yes | 

### Enabled

If true, time accumulation continues\. If false, time is paused\.


### Reset When Disabled

If true and \`enabled\` is false, the accumulated time is reset to 0\.


### Speed Multiplier

Multiplier for time speed\. 1\.0 = normal speed, 2\.0 = double speed, 0\.5 = half speed\.


**Value Constraints:**

- **Minimum Value:** 0\.0

## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| accumulatedTime |  | Float | State | 0\.0 | No | 

### 

The accumulated time since component creation \(in seconds\)\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| currentTime | Current Time | Float | Output | 0\.0 | No | 

### Current Time

The time in seconds since component creation\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
