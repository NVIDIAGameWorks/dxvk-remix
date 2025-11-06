# Box Proximity

Calculates the signed distance from a world position to a mesh's bounding box\. Positive values indicate the point is outside the bounding box\.  Note that the output is in object space\.

## Component Information

- **Name:** `BoxProximity`
- **UI Name:** Box Proximity
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| target | Target | Prim | Input | None | No | 
| worldPosition | World Position | Float3 | Input | \[0\.0, 0\.0, 0\.0\] | No | 
| inactiveDistance | Inactive Distance | Float | Input | 0\.0 | Yes | 
| fullActivationDistance | Full Activation Distance | Float | Input | 1\.0 | Yes | 
| easingType | Easing Type | Uint32 | Input | Linear | Yes | 

### Target

The mesh prim to get bounding box from\. Must be a mesh prim\.


### World Position

The world space position to test against the mesh bounding box\.


### Inactive Distance

The distance inside the bounding box that corresponds to a normalized value of 0\.0\.  Negative numbers represent values outside the AABB\. 


### Full Activation Distance

The distance inside the bounding box that corresponds to a normalized value of 1\.0\.  Negative numbers represent values outside the AABB\. 


### Easing Type

The type of easing to apply to the normalized output\.

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

## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| signedDistance | Signed Distance | Float | Output | 0\.0 | No | 
| activationStrength | Activation Strength | Float | Output | 0\.0 | No | 

### Signed Distance

Distance in object space to the nearest bounding box plane\. Positive when outside, negative when inside\.  Outputs FLT\_MAX when no valid bounding box is found\.


### Activation Strength

Normalized 0\-1 value: 0 when on bounding box surface, 1 when at max distance inside \(with easing applied\)\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
