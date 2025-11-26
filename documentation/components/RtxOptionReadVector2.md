# Rtx Option Read Vector2

Reads the current value of a Vector2 RTX option\.<br/><br/>Outputs the current value of a given RTX option Vector2\. The option name should be the full name including category \(e\.g\., 'rtx\.someVector2Option'\)\.

## Component Information

- **Name:** `RtxOptionReadVector2`
- **UI Name:** Rtx Option Read Vector2
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
| value | Value | Float2 | Output | \[0\.0, 0\.0\] | No | 

### Value

The current value of the RTX option as a Vector2\. Returns \(0,0\) if the option is not found or is not a Vector2 type\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
