# Security Policy

## Supported versions

Security fixes are made on the latest version in the default branch.

## Reporting a vulnerability

Please do not open a public issue for vulnerabilities that could expose a property, gate, camera, credentials, or Firebase project. Use GitHub's private vulnerability reporting for this repository if enabled, or contact the maintainer privately. Include the affected component, reproduction steps, and any suggested mitigation. Do not include real credentials, camera snapshots, addresses, or network details.

## Deployment requirements

- Treat gate actuation as safety-critical. Preserve manufacturer safety interlocks, manual release, obstruction sensing, and compliant installation practices.
- Use electrically isolated interfaces. An ESP32 GPIO must never be connected directly to gate-controller or doorbell voltages.
- Protect Firebase with authenticated, least-privilege rules. Never use wide-open production database rules.
- Rotate any token, password, or database secret that has appeared in a commit, chat, screenshot, or log.
- Restrict cloud API keys where the provider supports restrictions, and regularly review Firebase tokens and database access.
- Avoid exposing the ESP32 web server to the public internet. Use a trusted VPN or authenticated gateway for remote administration.
