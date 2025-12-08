# Counter

Counts up by a value every frame when a condition is true\.<br/><br/>Increments a counter by a specified value every frame that the input bool is true\. Use \`Starting Value\` to set the initial counter value\. Useful for tracking how many frames a condition has been active\.

## Component Information

- **Name:** `Counter`
- **UI Name:** Counter
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| increment | Increment | Bool | Input | false | No | 
| incrementValue | Increment Value | Float | Input | 1\.0 | Yes | 
| defaultValue | Starting Value | Float | Input | 0\.0 | No | 

### Increment

When true, the counter increments by the increment value each frame\.


### Increment Value

The value to add to the counter each frame when increment is true\.


### Starting Value

The initial value of the counter when the component is created\.


## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| count |  | Float | State | 0\.0 | No | 

### 

The current counter value\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| value | Value | Float | Output | 0\.0 | No | 

### Value

The current counter value\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
