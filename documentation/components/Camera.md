# Camera

Provides information about the camera's position, direction, and field of view\.<br/><br/>Outputs current camera properties including position, orientation vectors, and projection parameters\.<br/><br/>Uses free camera when both 'rtx\.camera\.useFreeCameraForComponents' and free camera are enabled\.

## Component Information

- **Name:** `Camera`
- **UI Name:** Camera
- **Version:** 1
- **Categories:** Sense

## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| position | Position | Float3 | Output | \[0\.0, 0\.0, 0\.0\] | No | 
| forward | Forward | Float3 | Output | \[0\.0, 0\.0, \-1\.0\] | No | 
| right | Right | Float3 | Output | \[1\.0, 0\.0, 0\.0\] | No | 
| up | Up | Float3 | Output | \[0\.0, 1\.0, 0\.0\] | No | 
| fovRadians | FOV \(radians\) | Float | Output | 1\.047198 | No | 
| fovDegrees | FOV \(degrees\) | Float | Output | 60\.0 | No | 
| aspectRatio | Aspect Ratio | Float | Output | 1\.0 | No | 
| nearPlane | Near Plane | Float | Output | 0\.1 | No | 
| farPlane | Far Plane | Float | Output | 1000\.0 | No | 

### Position

The current camera position in world space\.


### Forward

The camera's normalized forward direction vector in world space\.


### Right

The camera's normalized right direction vector in world space\.


### Up

The camera's normalized up direction vector in world space\.


### FOV \(radians\)

The Y axis \(vertical\) Field of View of the camera in radians\. Note this value will always be positive\.


### FOV \(degrees\)

The Y axis \(vertical\) Field of View of the camera in degrees\. Note this value will always be positive\.


### Aspect Ratio

The camera's aspect ratio \(width/height\)\.


### Near Plane

The camera's near clipping plane distance\.


### Far Plane

The camera's far clipping plane distance\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
