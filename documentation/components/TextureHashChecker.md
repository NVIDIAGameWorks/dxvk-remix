# Texture Hash Checker

Detects if a specific texture is being used in the current frame\.<br/><br/>Checks if a specific texture hash was used for material replacement in the current frame\. This includes textures in all categories, including ignored textures\.

## Component Information

- **Name:** `TextureHashChecker`
- **UI Name:** Texture Hash Checker
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| textureHash | Texture Hash | Hash | Input | 0x0 | No | 

### Texture Hash

The texture hash to check for usage in the current frame\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| isUsed | Is Used | Bool | Output | false | No | 
| usageCount | Usage Count | Float | Output | 0\.0 | No | 

### Is Used

True if the texture hash was used in the current frame\.


### Usage Count

Number of times the texture hash was used in the current frame\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
