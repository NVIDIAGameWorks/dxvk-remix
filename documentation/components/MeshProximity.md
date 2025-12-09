# Mesh Proximity

Measures how far a point is from a mesh's bounding box\. This can be used to determine if the camera is close to a mesh, or inside of a room\.<br/><br/>Calculates the signed distance from a world position to a mesh's bounding box\. Positive values indicate the point is outside the bounding box, negative values indicate it's inside\.<br/><br/>Note that the output is in object space, so if the mesh is scaled, the distance may not correspond to world units\.

## Component Information

- **Name:** `MeshProximity`
- **UI Name:** Mesh Proximity
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| target | Target | Prim | Input | None | No | 
| worldPosition | World Position | Float3 | Input | \[0\.0, 0\.0, 0\.0\] | No | 
| inactiveDistance | Inactive Distance | Float | Input | 1\.0 | Yes | 
| fullActivationDistance | Full Activation Distance | Float | Input | 0\.0 | Yes | 
| easingType | Easing Type | Enum | Input | Linear | Yes | 

### Target

The mesh prim to get bounding box from\. Must be a UsdGeomMesh prim \(the actual geometry\)\.


### World Position

The world space position to test against the mesh bounding box\. This is often the \`Position\` output from the \`Camera\` component, if checking how close the camera is to the mesh\.


### Inactive Distance

When the \`World Position\` is this far from the mesh's bounding box, \`Activation Strength\` will be 0\. Positive numbers represent mean \`World Position\` is outside the box, negative numbers mean it is inside the box\.


### Full Activation Distance

When the \`World Position\` is this far from the mesh's bounding box, \`Activation Strength\` will be 1\. Positive numbers represent mean \`World Position\` is outside the box, negative numbers mean it is inside the box\.


### Easing Type

The type of easing to apply to the \`Activation Strength\` output\.  

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

## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| signedDistance | Signed Distance | Float | Output | 0\.0 | No | 
| activationStrength | Activation Strength | Float | Output | 0\.0 | No | 

### Signed Distance

Distance in object space to the nearest point on the surface of the bounding box\. Positive when outside the bounding box, negative when inside\. Outputs a very large number when no valid bounding box is found\. Because this is in object space, if the object is scaled, the distance may not correspond to world units\.


### Activation Strength

Normalized 0\-1 value: 0 when \`Signed Distance\` = \`Inactive Distance\`, 1 when it equals \`Full Activation Distance\`\. This is often passed directly to the \`Blend Strength\` of \`Rtx Option Layer Action\` to enable a conf layer when the camera is close to the mesh\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
