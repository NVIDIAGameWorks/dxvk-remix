# Animated Float

A single animated float value\.

## Component Information

- **Name:** `AnimatedFloat`
- **UI Name:** Animated Float
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| enabled | Enabled | Bool | Input | true | Yes | 
| initialValue | Initial Value | Float | Input | 0\.0 | No | 
| finalValue | Final Value | Float | Input | 1\.0 | No | 
| duration | Duration | Float | Input | 1\.0 | No | 
| loopingType | Looping Type | Uint32 | Input | Loop | No | 
| interpolation | Interpolation | Uint32 | Input | Linear | Yes | 

### Enabled

If true, the float will be animated\.


### Initial Value

The value at time t=0\.


### Final Value

The value at time t=duration\.


### Duration

How long it takes to animate from initial value to final value, in seconds\.


**Value Constraints:**

- **Minimum Value:** 0\.000001

### Looping Type

What happens when the float reaches the final value\.

Underlying Type: `Uint32`


**Allowed Values:**

- Loop (`Loop`): The value will wrap around from max to min\. *(default)*
- PingPong (`PingPong`): The value will bounce back and forth between min and max\.
- NoLoop (`NoLoop`): The value will be unchanged\.
- Clamp (`Clamp`): The value will be clamped to the range\.

### Interpolation

How the float will change over time\.

Underlying Type: `Uint32`


**Allowed Values:**

- Linear (`Linear`): The float will have a constant velocity\. *(default)*
- Cubic (`Cubic`): The float will change in a cubic curve over time\.
- EaseIn (`EaseIn`): The float will start slow, then accelerate\.
- EaseOut (`EaseOut`): The float will start fast, then decelerate\.
- EaseInOut (`EaseInOut`): The float will start slow, accelerate, then decelerate\.
- Sine (`Sine`): Smooth, natural motion using a sine wave\.
- Exponential (`Exponential`): Dramatic acceleration effect\.
- Bounce (`Bounce`): Bouncy, playful motion\.
- Elastic (`Elastic`): Spring\-like motion\.

## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| accumulatedTime |  | Float | State | 0\.0 | No | 

### 

How much time has passed since the animation started\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| currentValue | Current Value | Float | Output | 0\.0 | No | 

### Current Value

The animated float value\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
