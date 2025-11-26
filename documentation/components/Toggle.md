# Toggle

A switch that alternates between on \(true\) and off \(false\) states\.<br/><br/>Think of this like a light switch: each frame \`Trigger Toggle\` is true, the switch flips to the opposite position\. Use \`Starting State\` to choose whether the switch begins in the on or off position\.

## Component Information

- **Name:** `Toggle`
- **UI Name:** Toggle
- **Version:** 1
- **Categories:** Transform

## Input Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| triggerToggle | Trigger Toggle | Bool | Input | false | No | 
| defaultState | Starting State | Bool | Input | false | No | 

### Trigger Toggle

When this is true, the toggle switches to its opposite state \(on becomes off, or off becomes on\)\. Set this to true each time you want to flip the switch\.


### Starting State

The initial state of the toggle when the component is created\. Set to true to start in the 'on' state, or false to start in the 'off' state\.


## Output Properties

| Property | Display Name | Type | IO Type | Default Value | Optional |
|----------|--------------|------|---------|---------------|----------|
| isOn | Is On | Bool | Output | false | No | 

### Is On

The current state of the toggle: true means 'on', false means 'off'\. This starts at the \`Starting State\` value and changes each time \`Trigger Toggle\` becomes true\.


## Usage Notes

This component is part of the RTX Remix graph system. It is intended for use in the Remix Toolkit and Runtime only.

---
[← Back to Component Index](index.md)
