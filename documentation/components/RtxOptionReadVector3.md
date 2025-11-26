# Rtx Option Read Vector3

Reads the current value of a Vector3 RTX option\.<br/><br/>Outputs the current value of a given RTX option Vector3\. The option name should be the full name including category \(e\.g\., 'rtx\.fallbackLightRadiance'\)\.

## Component Information

- **Name:** `RtxOptionReadVector3`
- **UI Name:** Rtx Option Read Vector3
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
| value | Value | Float3 | Output | \[0\.0, 0\.0, 0\.0\] | No | 

### Value

The current value of the RTX option as a Vector3\. Returns \(0,0,0\) if the option is not found or is not a Vector3 type\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
