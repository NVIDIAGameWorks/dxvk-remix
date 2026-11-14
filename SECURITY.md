# Security Policy

## Supported Versions

The following versions of dxvk-remix receive active security support:

| Version | Supported          |
| ------- | ------------------ |
| Latest release (main branch) | ✅ Active support |
| Previous minor releases       | ⚠️ Best-effort only |
| Older / unsupported releases  | ❌ No support |

We strongly recommend always using the latest tagged release or building from the `main` branch.

---

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub Issues, Pull Requests, or Discussions.**

If you believe you have found a security vulnerability in dxvk-remix — including issues in the rendering pipeline, the bridge subsystem, the Remix API, shader handling, or any bundled third-party dependency — please report it responsibly using one of the methods below.

### Option 1: GitHub Private Security Advisory (Preferred)

Use GitHub's built-in private vulnerability reporting:

1. Navigate to the [Security tab](https://github.com/NVIDIAGameWorks/dxvk-remix/security) of this repository.
2. Click **"Report a vulnerability"**.
3. Fill in the details of the vulnerability and submit.

This keeps the report confidential and allows the maintainers to triage and respond without public exposure.

### Option 2: NVIDIA Product Security

For vulnerabilities that may affect NVIDIA products or the broader RTX Remix ecosystem, you may also report through NVIDIA's official security channel:

- **NVIDIA Product Security:** https://www.nvidia.com/en-us/security/
- **Email:** psirt@nvidia.com

Please include `[dxvk-remix]` in the subject line.

---

## What to Include in Your Report

To help us triage your report quickly, please include:

- A clear **description** of the vulnerability and its potential impact.
- The **component affected** (e.g., bridge subsystem, D3D9 translation layer, Remix API, GLSL shaders, a specific source file).
- **Steps to reproduce** the issue, including a minimal proof-of-concept if possible.
- The **version or commit hash** you tested against.
- Your **assessment of severity** (e.g., local privilege escalation, memory corruption, denial of service).
- Any **suggested mitigations or fixes** you may have identified.

---

## Response Process

1. **Acknowledgment** — We aim to acknowledge receipt of your report within **5 business days**.
2. **Triage** — The security team will assess severity and reproducibility, typically within **14 days**.
3. **Remediation** — We will work on a fix and coordinate a release timeline with you.
4. **Disclosure** — We follow a **coordinated disclosure** model. We ask that you allow us a reasonable timeframe (typically **90 days**) to address the vulnerability before public disclosure.
5. **Credit** — With your permission, we are happy to credit you in the release notes or security advisory.

---

## Scope

The following are considered **in scope** for security reports:

- Memory safety issues (buffer overflows, use-after-free, etc.) in the C/C++ codebase.
- Logic vulnerabilities in the D3D8/D3D9 → Vulkan translation layer.
- Security issues in the 32-bit bridge subsystem (`bridge/`).
- Vulnerabilities in the Remix API (`public/include/`, `documentation/RemixSDK.md`).
- Unsafe handling of untrusted game data or configuration files (e.g., `dxvk.conf`, `gametargets.conf`).
- Shader-level vulnerabilities in the GLSL/HLSL pipeline.
- Dependency vulnerabilities in bundled submodules.

The following are generally considered **out of scope**:

- Vulnerabilities in the upstream [DXVK](https://github.com/doitsujin/dxvk) project unrelated to dxvk-remix changes (please report those upstream).
- Issues that require physical access to the target machine.
- Social engineering or phishing attacks.
- Bugs that are not security-relevant (please open a regular [GitHub Issue](https://github.com/NVIDIAGameWorks/dxvk-remix/issues) instead).

---

## Security Best Practices for Users

- Only deploy dxvk-remix DLLs (`d3d9.dll`, `d3d8.dll`, etc.) obtained from official releases or built from this repository's source.
- Be cautious when using third-party RTX Remix mods or configuration files — malformed game data processed by the runtime could trigger edge-case behavior.
- Keep your Vulkan drivers and Windows SDK up to date, as vulnerabilities in those layers are outside the scope of this project.

---

## Acknowledgments

We appreciate the security research community's efforts in responsibly disclosing vulnerabilities. Thank you for helping keep dxvk-remix and the RTX Remix ecosystem safe.
