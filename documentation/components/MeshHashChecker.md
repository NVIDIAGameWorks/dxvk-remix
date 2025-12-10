# Mesh Hash Checker

Detects if a specific mesh is currently being drawn in the scene\.<br/><br/>This checks all meshes that the game sends to Remix in the current frame, which will probably include meshes that are off camera or occluded\.

## Component Information

- **Name:** `MeshHashChecker`
- **UI Name:** Mesh Hash Checker
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| meshHash | Mesh Hash | Hash | Input | 0x0 | No | 

### Mesh Hash

The mesh hash to check for usage in the current frame\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| isUsed | Is Used | Bool | Output | false | No | 
| usageCount | Usage Count | Float | Output | 0\.0 | No | 

### Is Used

True if the mesh hash was used in the current frame\.


### Usage Count

Number of times the mesh hash was used in the current frame\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
