# Particle drag

## Authoring

**Drag Coefficient** controls exponential velocity decay. Larger positive values slow particles more quickly. Zero disables drag. Existing effects with strong drag may travel farther because drag is integrated independently of the number of rendered frames.

## Implementation

`particle_drag.h` integrates linear drag and the acceleration sampled during an evolution step. For coefficient `k` and timestep `dt`, velocity retention is `exp(-k * dt)`. The velocity integral `(1 - exp(-k * dt)) / k` also advances position and integrates acceleration into velocity. Integrating that coefficient again advances position under acceleration. Series evaluation avoids cancellation near zero drag.

This gives the same velocity and displacement for different partitions of elapsed simulation time when acceleration is constant. Turbulence and attractor forces are sampled once per step, so their spatial or temporal variation still introduces integration error. Zero-drag effects retain semi-implicit integration.

Collision responses retain their existing post-response movement for that step. Restricted or speed-clamped axes retain their post-constraint movement, including axes that start above their speed limit and decay below it during the step. There are no extra dispatches or physics substeps. The simulation timestep remains capped at 1/30 second, so frame rates below 30 FPS can still slow simulation relative to wall time.
