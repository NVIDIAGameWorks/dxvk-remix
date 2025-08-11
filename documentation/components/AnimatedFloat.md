# Animated Float

A single animated float value\.

## Component Information

- **Name:** `AnimatedFloat`
- **UI Name:** Animated Float
- **Version:** 1
- **Categories:** animation

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| enabled | Enabled | Bool | Input | true | Yes | 
| initialValue | Initial Value | Float | Input | 0\.000000 | No | 
| finalValue | Final Value | Float | Input | 1\.000000 | No | 
| duration | Duration | Float | Input | 1\.000000 | No | 
| loopingType | Looping Type | Uint32 | Input | 0 | No | 
| interpolation | Interpolation | Uint32 | Input | 0 | Yes | 

### Enabled

If true, the float will be animated\.


### Initial Value

The value at time t=0\.


### Final Value

The value at time t=duration\.


### Duration

How long it takes to animate from initial value to final value, in seconds\.


### Looping Type

What happens when the float reaches the final value\.


**Allowed Values:**

- Continue: The value will continue accumulating \(a linear animation will preserve the velocity\)\.
- Freeze: The value will freeze at the final value\.
- Loop: The value will return to the initial value\.
- PingPong: The value will play in reverse until it reaches the initial value, then loop\.

### Interpolation

How the float will change over time\.


**Allowed Values:**

- Bounce: Bouncy, playful motion\.
- Cubic: The float will change in a cubic curve over time\.
- EaseIn: The float will start slow, then accelerate\.
- EaseInOut: The float will start slow, accelerate, then decelerate\.
- EaseOut: The float will start fast, then decelerate\.
- Elastic: Spring\-like motion\.
- Exponential: Dramatic acceleration effect\.
- Linear: The float will have a constant velocity\.
- Sine: Smooth, natural motion using sine wave\.

## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| accumulatedTime |  | Float | State | 0\.000000 | No | 

### 

How much time has passed since the animation started\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| currentValue | Current Value | Float | Output | 0\.000000 | No | 

### Current Value

The animated float value\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
