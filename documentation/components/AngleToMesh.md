# Angle to Mesh

Measures the angle between a ray and a mesh's center point\.  This can be used to determine if the camera is looking at a mesh\.<br/><br/>Calculates the angle between a ray \(from position \+ direction\) and the direction to a mesh's transformed centroid\.

## Component Information

- **Name:** `AngleToMesh`
- **UI Name:** Angle to Mesh
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| worldPosition | World Position | Float3 | Input | \[0\.0, 0\.0, 0\.0\] | No | 
| direction | Direction | Float3 | Input | \[0\.0, 0\.0, 1\.0\] | No | 
| target | Target | Prim | Input | None | No | 

### World Position

The world space position to use as the origin of the ray\.


### Direction

The direction vector of the ray \(does not need to be normalized\)\.


### Target

The mesh prim to get the centroid from\. Must be a mesh prim\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| angleDegrees | Angle \(Degrees\) | Float | Output | 0\.0 | No | 
| angleRadians | Angle \(Radians\) | Float | Output | 0\.0 | No | 
| directionToCentroid | Direction to Centroid | Float3 | Output | \[0\.0, 0\.0, 0\.0\] | No | 

### Angle \(Degrees\)

The angle in degrees between the ray direction and the direction to the mesh centroid\.


### Angle \(Radians\)

The angle in radians between the ray direction and the direction to the mesh centroid\.


### Direction to Centroid

The normalized direction vector from the world position to the mesh centroid\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
