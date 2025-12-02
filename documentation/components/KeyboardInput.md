# Keyboard Input

Detects when keyboard keys are pressed, held, or released\.<br/><br/>Checks the state of a keyboard key or key combination using the same format as RTX options\.

## Component Information

- **Name:** `KeyboardInput`
- **UI Name:** Keyboard Input
- **Version:** 1
- **Categories:** Sense

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| keyString | Key String | String | Input | "A" | No | 

### Key String

The key combination string to detect\.<br/>Examples: 'A', 'CTRL, A', 'SHIFT, SPACE'\.<br/>Full list of key names available in \`src/util/util\_keybind\.h\`\.


## State Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| wasPressedLastFrame |  | Bool | State | false | No | 

### 

Internal state to track if the key was pressed in the previous frame\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| isPressed | Is Pressed | Bool | Output | false | No | 
| wasJustPressed | Was Just Pressed | Bool | Output | false | No | 
| wasClicked | Was Clicked | Bool | Output | false | No | 

### Is Pressed

True if the key combination is currently being pressed\.


### Was Just Pressed

True if the key combination was just pressed this frame\.


### Was Clicked

True for one frame after the key combination is released \(press then release cycle\)\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
