# Mesh Hash Checker

Checks if a specific mesh hash was processed in the current frame\.

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
| usageCount | Usage Count | Uint32 | Output | 0 | No | 

### Is Used

True if the mesh hash was used in the current frame\.


### Usage Count

Number of times the mesh hash was used in the current frame\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
