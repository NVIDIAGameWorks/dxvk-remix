# RTX Remix Component Documentation

This documentation provides detailed information about all available components in the RTX Remix graph system.

## Available Components

### Act

| Component | Description | Version |
|-----------|-------------|---------|
| [Rtx Option Layer Action](RtxOptionLayerAction.md) | Activates and controls configuration layers at runtime based on game conditions\.<br/><br/>Controls an RtxOpt\.\.\. | 1 |

### Transform

| Component | Description | Version |
|-----------|-------------|---------|
| [Add](Add.md) | Adds two numbers or vectors together\.<br/><br/>Vector \+ Number will add the number to all components of the \.\.\. | 1 |
| [Between](Between.md) | Tests if a value is within a range \(inclusive\)\.<br/><br/>Returns true if the value is >= Min Value AND <= Ma\.\.\. | 1 |
| [Bool AND](BoolAnd.md) | Returns true only if both A and B are true\. | 1 |
| [Bool NOT](BoolNot.md) | Flips a true/false value to its opposite\. | 1 |
| [Bool OR](BoolOr.md) | Returns true if either A or B \(or both\) are true\. | 1 |
| [Ceil](Ceil.md) | Rounds a value up to the next integer\.<br/><br/>Returns the smallest integer greater than or equal to the in\.\.\. | 1 |
| [Clamp](Clamp.md) | Constrains a value to a specified range\.<br/><br/>If the value is less than Min Value, returns Min Value\. If\.\.\. | 1 |
| [Compose Vector2](ComposeVector2.md) | Combines two separate numbers into a single Vector2\. | 1 |
| [Compose Vector3](ComposeVector3.md) | Combines three separate numbers into a single Vector3\. | 1 |
| [Compose Vector4](ComposeVector4.md) | Combines four separate numbers into a single Vector4\. | 1 |
| [Conditionally Store](ConditionallyStore.md) | Stores a value when a condition is true, otherwise keeps the previous value\.<br/><br/>If the store input is \.\.\. | 1 |
| [Count Toggles](CountToggles.md) | Counts how many times an input switches from off to on\.<br/><br/>Tracks the number of times a boolean input \.\.\. | 1 |
| [Counter](Counter.md) | Counts up by a value every frame when a condition is true\.<br/><br/>Increments a counter by a specified valu\.\.\. | 1 |
| [Decompose Vector2](DecomposeVector2.md) | Splits a Vector2 into two separate numbers\. | 1 |
| [Decompose Vector3](DecomposeVector3.md) | Splits a Vector3 into three separate numbers\. | 1 |
| [Decompose Vector4](DecomposeVector4.md) | Splits a Vector4 into four separate numbers\. | 1 |
| [Divide](Divide.md) | Divides one number or vector by another\.<br/><br/>Vector / Number will divide all components of the vector b\.\.\. | 1 |
| [Equal To](EqualTo.md) | Returns true if A is equal to B, false otherwise\.<br/><br/>For floating point values, this performs exact eq\.\.\. | 1 |
| [Floor](Floor.md) | Rounds a value down to the previous integer\.<br/><br/>Returns the largest integer less than or equal to the \.\.\. | 1 |
| [Greater Than](GreaterThan.md) | Returns true if A is greater than B, false otherwise\. | 1 |
| [Invert](Invert.md) | Outputs 1 minus the input value\.<br/><br/>Calculates 1 \- input\. Useful for inverting normalized values \(e\.g\.\.\.\. | 1 |
| [Less Than](LessThan.md) | Returns true if A is less than B, false otherwise\. | 1 |
| [Loop](Loop.md) | Wraps a number back into a range when it goes outside the boundaries\.<br/><br/>Applies looping behavior to a\.\.\. | 1 |
| [Max](Max.md) | Returns the larger of two values\.<br/><br/>Outputs the maximum value between A and B\. | 1 |
| [Min](Min.md) | Returns the smaller of two values\.<br/><br/>Outputs the minimum value between A and B\. | 1 |
| [Multiply](Multiply.md) | Multiplies two values together\.<br/><br/>Vector \* Number will multiply all components of the vector by the n\.\.\. | 1 |
| [Normalize](Normalize.md) | Normalizes a vector to have length 1\.<br/><br/>Divides the vector by its length to produce a unit vector \(le\.\.\. | 1 |
| [Previous Frame Value](PreviousFrameValue.md) | Outputs the value from the previous frame\.<br/><br/>Stores the input value and outputs it on the next frame\.\.\.\. | 1 |
| [Remap](Remap.md) | Smoothly maps a value from one range to another range with customizable easing curves\.<br/><br/>Remaps a val\.\.\. | 1 |
| [Round](Round.md) | Rounds a value to the nearest integer\.<br/><br/>Rounds to the nearest whole number\. For example: 1\.4 becomes\.\.\. | 1 |
| [Select](Select.md) | Selects between two values based on a boolean condition\.<br/><br/>If the condition is true, outputs Input A\.\.\.\. | 1 |
| [Smooth](Smooth.md) | Applies exponential smoothing to a value over time\.<br/><br/>Uses a moving average filter to smooth out rapi\.\.\. | 1 |
| [Subtract](Subtract.md) | Subtracts one number or vector from another\.<br/><br/>Vector \- Number will subtract the number from all comp\.\.\. | 1 |
| [Toggle](Toggle.md) | A switch that alternates between on \(true\) and off \(false\) states\.<br/><br/>Think of this like a light switc\.\.\. | 1 |
| [Vector Length](VectorLength.md) | Calculates the length \(magnitude\) of a vector\.<br/><br/>Computes the Euclidean length of the vector using th\.\.\. | 1 |
| [Velocity](Velocity.md) | Detects the rate of change of a value from frame to frame\.<br/><br/>Calculates the difference between the cu\.\.\. | 1 |

### Sense

| Component | Description | Version |
|-----------|-------------|---------|
| [Angle to Mesh](AngleToMesh.md) | Measures the angle between a ray and a mesh's center point\.  This can be used to determine if the ca\.\.\. | 1 |
| [Camera](Camera.md) | Provides information about the camera's position, direction, and field of view\.<br/><br/>Outputs current cam\.\.\. | 1 |
| [Fog Hash Checker](FogHashChecker.md) | Detects if a specific fog state is currently active in the scene\.<br/><br/>Checks if a given fog hash matche\.\.\. | 1 |
| [Keyboard Input](KeyboardInput.md) | Detects when keyboard keys are pressed, held, or released\.<br/><br/>Checks the state of a keyboard key or ke\.\.\. | 1 |
| [Light Hash Checker](LightHashChecker.md) | Detects if a specific light is currently active in the scene\.<br/><br/>Checks if a specific light hash is pr\.\.\. | 1 |
| [Mesh Hash Checker](MeshHashChecker.md) | Detects if a specific mesh is currently being drawn in the scene\.<br/><br/>This checks all meshes that the g\.\.\. | 1 |
| [Mesh Proximity](MeshProximity.md) | Measures how far a point is from a mesh's bounding box\. This can be used to determine if the camera \.\.\. | 1 |
| [Ray Mesh Intersection](RayMeshIntersection.md) | Tests if a ray intersects with a mesh\.<br/><br/>Performs a ray\-mesh intersection test\. Currently supports bo\.\.\. | 1 |
| [Read Bone Transform](ReadBoneTransform.md) | Reads the transform \(position, rotation, scale\) of a bone from a skinned mesh\.<br/><br/>Extracts the transfo\.\.\. | 1 |
| [Read Transform](ReadTransform.md) | Reads the transform \(position, rotation, scale\) of a mesh or light in world space\.<br/><br/>Extracts the tra\.\.\. | 1 |
| [Rtx Option Layer Sensor](RtxOptionLayerSensor.md) | Reads the state of a configuration layer\.<br/><br/>Outputs whether a given RtxOptionLayer is enabled, along \.\.\. | 1 |
| [Rtx Option Read Bool](RtxOptionReadBool.md) | Reads the current value of a boolean RTX option\.<br/><br/>Outputs the current value of a given RTX option bo\.\.\. | 1 |
| [Rtx Option Read Color3](RtxOptionReadColor3.md) | Reads the current value of a Color3 \(RGB\) RTX option\.<br/><br/>Outputs the current value of a given RTX opti\.\.\. | 1 |
| [Rtx Option Read Color4](RtxOptionReadColor4.md) | Reads the current value of a Color4 \(RGBA\) RTX option\.<br/><br/>Outputs the current value of a given RTX opt\.\.\. | 1 |
| [Rtx Option Read Number](RtxOptionReadNumber.md) | Reads the current value of a numeric RTX option\.<br/><br/>Outputs the current value of a given RTX option\. S\.\.\. | 1 |
| [Rtx Option Read Vector2](RtxOptionReadVector2.md) | Reads the current value of a Vector2 RTX option\.<br/><br/>Outputs the current value of a given RTX option Ve\.\.\. | 1 |
| [Rtx Option Read Vector3](RtxOptionReadVector3.md) | Reads the current value of a Vector3 RTX option\.<br/><br/>Outputs the current value of a given RTX option Ve\.\.\. | 1 |
| [Texture Hash Checker](TextureHashChecker.md) | Detects if a specific texture is being used in the current frame\.<br/><br/>Checks if a specific texture hash\.\.\. | 1 |
| [Time](Time.md) | Provides a continuously increasing time value for creating animations and time\-based effects\.<br/><br/>Outpu\.\.\. | 1 |

### Constants

| Component | Description | Version |
|-----------|-------------|---------|
| [Constant Asset Path](ConstAssetPath.md) | Provides a constant file or asset path that you can set\.<br/><br/>Use this to provide fixed paths to texture\.\.\. | 1 |
| [Constant Bool](ConstBool.md) | Provides a constant true or false value that you can set\.<br/><br/>Use this to provide fixed on/off, yes/no,\.\.\. | 1 |
| [Constant Color3](ConstColor3.md) | Provides a constant RGB color \(Red, Green, Blue\) that you can set\.<br/><br/>Use this to provide fixed colors\.\.\. | 1 |
| [Constant Color4](ConstColor4.md) | Provides a constant RGBA color \(Red, Green, Blue, Alpha\) that you can set\.<br/><br/>Use this for fixed color\.\.\. | 1 |
| [Constant Float2](ConstFloat2.md) | Provides a constant 2D vector \(two numbers: X and Y\) that you can set\.<br/><br/>Use this for fixed 2D coordi\.\.\. | 1 |
| [Constant Float3](ConstFloat3.md) | Provides a constant 3D vector \(three numbers: X, Y, Z\) that you can set\.<br/><br/>Use this for fixed 3D posi\.\.\. | 1 |
| [Constant Float4](ConstFloat4.md) | Provides a constant 4D vector \(four numbers: X, Y, Z, W\) that you can set\. | 1 |
| [Constant Hash](ConstHash.md) | Provides a constant hash value that you can set\.<br/><br/>Use this to provide fixed hash identifiers for mes\.\.\. | 1 |
| [Constant Number](ConstFloat.md) | Provides a constant decimal number that you can set\.<br/><br/>Use this to provide fixed values like 0\.5, 3\.1\.\.\. | 1 |
| [Constant Prim](ConstPrim.md) | Provides a constant reference to a scene object \(prim\) that you can set\.<br/><br/>Use this to provide fixed \.\.\. | 1 |
| [Constant String](ConstString.md) | Provides a constant text string that you can set\.<br/><br/>Use this to provide fixed text values, labels, or\.\.\. | 1 |

### TODO

| Component | Description | Version |
|-----------|-------------|---------|
| [\[Non Functional\] Sphere Light](SphereLightOverride.md) | Modifies properties of a sphere light, such as its radius\.<br/><br/>Note: This component is currently non\-fu\.\.\. | 1 |

## Statistics

- **Total Components:** 68
- **Categorized Components:** 68
- **Categories:** 5

---
*Generated automatically from component specifications*
