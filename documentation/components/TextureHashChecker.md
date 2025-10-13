# Texture Hash Checker

Checks if a specific hash was used for material replacement in the current frame\.

## Component Information

- **Name:** `TextureHashChecker`
- **UI Name:** Texture Hash Checker
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| textureHash | Texture Hash | Uint64 | Input | 0 | No | 

### Texture Hash

The texture hash to check for usage in the current frame\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| isUsed | Is Used | Bool | Output | false | No | 
| usageCount | Usage Count | Uint32 | Output | 0 | No | 

### Is Used

True if the texture hash was used in the current frame\.


### Usage Count

Number of times the texture hash was used in the current frame\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
