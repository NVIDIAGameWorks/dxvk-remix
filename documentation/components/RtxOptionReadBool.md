# Rtx Option Read Bool

Reads the current value of a boolean RTX option\.<br/><br/>Outputs the current value of a given RTX option bool\. The option name should be the full name including category \(e\.g\., 'rtx\.enableRaytracing'\)\.

## Component Information

- **Name:** `RtxOptionReadBool`
- **UI Name:** Rtx Option Read Bool
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
| value | Value | Bool | Output | false | No | 

### Value

The current value of the RTX option as a bool\. Returns false if the option is not found or is not a bool type\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
