# Read Transform

Reads the transform \(position, rotation, scale\) of a mesh or light in world space\.<br/><br/>Extracts the transform information from a given mesh or light prim\. Outputs position, rotation \(as quaternion\), and scale in world space\.

## Component Information

- **Name:** `ReadTransform`
- **UI Name:** Read Transform
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| target | Target | Prim | Input | None | No | 

### Target

The mesh or light prim to read the transform from\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| position | Position | Float3 | Output | \[0\.0, 0\.0, 0\.0\] | No | 
| rotation | Rotation | Float4 | Output | \[0\.0, 0\.0, 0\.0, 1\.0\] | No | 
| scale | Scale | Float3 | Output | \[1\.0, 1\.0, 1\.0\] | No | 

### Position

The world space position of the target\.


### Rotation

The world space rotation of the target as a quaternion \(x, y, z, w\)\.


### Scale

The world space scale of the target\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
