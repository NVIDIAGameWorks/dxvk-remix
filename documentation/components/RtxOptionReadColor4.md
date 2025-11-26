# Rtx Option Read Color4

Reads the current value of a Color4 \(RGBA\) RTX option\.<br/><br/>Outputs the current value of a given RTX option as a Color4\. Internally, Color4 is stored as Vector4\. The option name should be the full name including category\.

## Component Information

- **Name:** `RtxOptionReadColor4`
- **UI Name:** Rtx Option Read Color4
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| optionName | Option Name | String | Input | "" | No | 

### Option Name

The full name of the RTX option to read \(e\.g\., 'rtx\.someOption'\)\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| value | Value | Float4 | Output | \[0\.0, 0\.0, 0\.0, 1\.0\] | No | 

### Value

The current value of the RTX option as a Color4 \(RGBA\)\. Returns black with full alpha \(0,0,0,1\) if the option is not found or is not a Vector4 type\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
