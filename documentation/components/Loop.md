# Loop

Wraps a number back into a range when it goes outside the boundaries\.<br/><br/>Applies looping behavior to a value\. Value is unchanged if it is inside the range\.<br/>Component outputs Min Range if Min Range == Max Range and looping type is not None\.<br/>Inverted ranges \(max < min\) are supported, but the results are undefined and may change without warning\.

## Component Information

- **Name:** `Loop`
- **UI Name:** Loop
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| value | Value | NumberOrVector | Input | 0 | No | 
| minRange | Min Range | NumberOrVector | Input | 0 | No | 
| maxRange | Max Range | NumberOrVector | Input | 0 | No | 
| loopingType | Looping Type | Enum | Input | Loop | No | 

### Value

The input value to apply looping to\.


### Min Range

The minimum value of the looping range\.


### Max Range

The maximum value of the looping range\.


### Looping Type

How the value should loop within the range\.

Underlying Type: `Enum`


**Allowed Values:**

- Loop (`Loop`): The value will wrap around from max to min\. *(default)*
- PingPong (`PingPong`): The value will bounce back and forth between min and max\.
- NoLoop (`NoLoop`): The value will be unchanged\.
- Clamp (`Clamp`): The value will be clamped to the range\.

## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| loopedValue | Looped Value | NumberOrVector | Output | 0 | No | 
| isReversing | Is Reversing | Bool | Output | false | No | 

### Looped Value

The value with looping applied\.


### Is Reversing

True if the value is in the reverse phase of ping pong looping\. If passing \`loopedValue\` to a \`Remap\` component, hook this up to \`shouldReverse\` from that component\.


## Valid Type Combinations

This component supports flexible types. The following type combinations are valid:

| # | isReversing | loopedValue | loopingType | maxRange | minRange | value |
|---|---|---|---|---|---|---|
| 1 | Bool | Float | Enum | Float | Float | Float |
| 2 | Bool | Float2 | Enum | Float2 | Float2 | Float2 |
| 3 | Bool | Float3 | Enum | Float3 | Float3 | Float3 |
| 4 | Bool | Float4 | Enum | Float4 | Float4 | Float4 |

## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
