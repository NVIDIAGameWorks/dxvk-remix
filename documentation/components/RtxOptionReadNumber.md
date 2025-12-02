# Rtx Option Read Number

Reads the current value of a numeric RTX option\.<br/><br/>Outputs the current value of a given RTX option\. Supports both float and int types\. The option name should be the full name including category \(e\.g\., 'rtx\.pathTracing\.enableReSTIRGI'\)\.

## Component Information

- **Name:** `RtxOptionReadNumber`
- **UI Name:** Rtx Option Read Number
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
| value | Value | Float | Output | 0\.0 | No | 

### Value

The current value of the RTX option as a float\. Returns 0 if the option is not found or is not a numeric type\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
