# Ray Mesh Intersection

Tests if a ray intersects with a mesh\.<br/><br/>Performs a ray\-mesh intersection test\. Currently supports bounding box intersection tests\. Returns true if the ray intersects the mesh's bounding box\.

## Component Information

- **Name:** `RayMeshIntersection`
- **UI Name:** Ray Mesh Intersection
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| rayOrigin | Ray Origin | Float3 | Input | \[0\.0, 0\.0, 0\.0\] | No | 
| rayDirection | Ray Direction | Float3 | Input | \[0\.0, 0\.0, 1\.0\] | No | 
| target | Target | Prim | Input | None | No | 
| intersectionType | Intersection Type | Enum | Input | Bounding Box | Yes | 

### Ray Origin

The origin point of the ray in world space\.


### Ray Direction

The direction of the ray in world space\. Should be normalized\.


### Target

The mesh prim to test intersection against\. Must be a mesh prim\.


### Intersection Type

The type of intersection test to perform\.

Underlying Type: `Enum`


**Allowed Values:**

- Bounding Box (`Bounding Box`): Test intersection against the mesh's axis\-aligned bounding box\. *(default)*

## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| intersects | Intersects | Bool | Output | false | No | 

### Intersects

True if the ray intersects the mesh \(based on the selected intersection type\)\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
