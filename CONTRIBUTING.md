# Contributing

Thanks for helping improve Smart Gate Automation.

## Before opening an issue or pull request

- Search existing issues first.
- Never include Wi-Fi names/passwords, bot tokens, Firebase secrets, private URLs/IPs, camera images, device tokens, home addresses, or logs that contain them.
- Test gate-control changes using a safe bench setup or with the gate motor disconnected.
- Keep changes small and explain any electrical or safety impact.

## Development workflow

1. Create a branch from the default branch.
2. Use the templates in `.github/`.
3. Keep credentials in local ignored files only.
4. Update documentation when changing wiring, database paths, or configuration.
5. Submit a pull request describing testing and rollback considerations.

## Code style

Prefer descriptive names, small functions, comments that explain hardware assumptions, and explicit error handling for network requests. Do not weaken security rules merely to make an example work.
