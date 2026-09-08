# Web PWA

`index.html` is generated from the original project by `../scripts/Prepare-PublicPackage.ps1` with secrets removed. Create `config.local.js` by copying `config.example.js`, then fill it only on the deployment machine. The local configuration file is ignored by Git.

The legacy PWA passes a database credential from the browser to support direct REST calls. This is unsuitable for a public or broadly exposed deployment. Prefer Firebase Authentication with restrictive rules or an authenticated backend before granting remote access.
