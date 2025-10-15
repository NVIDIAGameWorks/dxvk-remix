# Loop Float

Applies looping behavior to a float value\.  Value is unchanged if it is inside the range\.<br/>Component outputs Min Range if Min Range == Max Range and looping type is not None\.<br/>Inverted ranges \(max < min\) are supported, but the results are undefined and may change without warning\.

## Component Information

- **Name:** `LoopFloat`
- **UI Name:** Loop Float
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| value | Value | Float | Input | 0\.0 | No | 
| minRange | Min Range | Float | Input | 0\.0 | No | 
| maxRange | Max Range | Float | Input | 1\.0 | No | 
| loopingType | Looping Type | Uint32 | Input | Loop | No | 

### Value

The input float value to apply looping to\.


### Min Range

The minimum value of the looping range\.


### Max Range

The maximum value of the looping range\.


### Looping Type

How the value should loop within the range\.

Underlying Type: `Uint32`


**Allowed Values:**

- Loop (`Loop`): The value will wrap around from max to min\. *(default)*
- PingPong (`PingPong`): The value will bounce back and forth between min and max\.
- NoLoop (`NoLoop`): The value will be unchanged\.
- Clamp (`Clamp`): The value will be clamped to the range\.

## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| loopedValue | Looped Value | Float | Output | 0\.0 | No | 
| isReversing | Is Reversing | Bool | Output | false | No | 

### Looped Value

The value with looping applied\.


### Is Reversing

True if the value is in the reverse phase of ping pong looping\. If passing \`loopedValue\` to an \`interpolateFloat\` component, hook this up to \`shouldReverse\` from that component\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
