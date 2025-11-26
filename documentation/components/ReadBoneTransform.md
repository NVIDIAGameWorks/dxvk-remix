# Read Bone Transform

Reads the transform \(position, rotation, scale\) of a bone from a skinned mesh\.<br/><br/>Extracts the transform information for a specific bone from a skinned mesh prim\. Outputs position, rotation \(as quaternion\), and scale in world space\. Returns identity transform if the target is not a skinned mesh or the bone index is invalid\.

## Component Information

- **Name:** `ReadBoneTransform`
- **UI Name:** Read Bone Transform
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| target | Target | Prim | Input | None | No | 
| boneIndex | Bone Index | Float | Input | 0\.0 | No | 

### Target

The mesh prim to read the bone transform from\. Must be a skinned mesh\.


### Bone Index

The index of the bone to read\. Will be rounded to the nearest integer\.


**Value Constraints:**

- **Minimum Value:** 0\.0

## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| position | Position | Float3 | Output | \[0\.0, 0\.0, 0\.0\] | No | 
| rotation | Rotation | Float4 | Output | \[0\.0, 0\.0, 0\.0, 1\.0\] | No | 
| scale | Scale | Float3 | Output | \[1\.0, 1\.0, 1\.0\] | No | 

### Position

The world space position of the bone\.


### Rotation

The world space rotation of the bone as a quaternion \(x, y, z, w\)\.


### Scale

The world space scale of the bone\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
