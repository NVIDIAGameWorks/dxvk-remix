# Interpolate Float

Smoothly maps a value from one range to another range with customizable easing curves\.<br/><br/>Interpolates a value from an input range to an output range with optional easing\. Values will be normalized \(mapped from input range to 0\-1\), eased \(changed from linear to some curve\), then mapped \(0\-1 value to output range\)\.<br/><br/>Note: Input values outside of input range are valid, and easing can lead to the output value being outside of the output range even when input is inside the input range\.<br/><br/>Inverted ranges \(max < min\) are supported, but the results are undefined and may change without warning\.

## Component Information

- **Name:** `InterpolateFloat`
- **UI Name:** Interpolate Float
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| value | Value | Float | Input | 0\.0 | No | 
| inputMin | Input Min | Float | Input | 0\.0 | No | 
| inputMax | Input Max | Float | Input | 1\.0 | No | 
| clampInput | Clamp Input | Bool | Input | false | Yes | 
| easingType | Easing Type | Enum | Input | Linear | No | 
| shouldReverse | Should Reverse | Bool | Input | false | Yes | 
| outputMin | Output Min | Float | Input | 0\.0 | No | 
| outputMax | Output Max | Float | Input | 1\.0 | No | 

### Value

The input value to interpolate\.


### Input Min

If \`Value\` equals \`Input Min\`, the output will be \`Output Min\`\.


### Input Max

If \`Value\` equals \`Input Max\`, the output will be \`Output Max\`\.


### Clamp Input

If true, \`value\` will be clamped to the input range\.


### Easing Type

The type of easing to apply\.

Underlying Type: `Enum`


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

### Should Reverse

If true, the easing is applied backwards\. If \`Value\` is coming from a Loop component that is using \`pingpong\`, hook this up to \`isReversing\` from that component\.


### Output Min

What a \`Value\` of \`Input Min\` maps to\.


### Output Max

What a \`Value\` of \`Input Max\` maps to\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| output | Output | Float | Output | 0\.0 | No | 

### Output

The final interpolated value after applying input normalization, easing, and output mapping\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
