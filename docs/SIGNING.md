# Code signing

Unsigned builds work, but Windows SmartScreen warns about them ("Windows protected your PC")
and some antivirus tools are stricter with them. A signature says who published the file and
that nobody changed it since.

## What CI does

`.github/workflows/build.yml` runs `installer/sign.ps1`:

- on every push to `main`: signs `OpenMix.exe` and `df.dll` before they are zipped;
- on `v*` tags: also signs the installer.

It signs only when the repository has two secrets (Settings > Secrets and variables > Actions):

| Secret | Value |
| --- | --- |
| `SIGN_PFX_BASE64` | the `.pfx` file as base64: `[Convert]::ToBase64String([IO.File]::ReadAllBytes("openmix.pfx"))` |
| `SIGN_PFX_PASSWORD` | the `.pfx` password |

Without them the step prints a note and the build carries on unsigned. Pull requests never
sign. Every signature gets an RFC 3161 timestamp, so it stays valid after the certificate
expires.

## Signing locally

From PowerShell with the Windows SDK installed:

```powershell
$env:SIGN_PFX_PASSWORD = '...'
.\installer\sign.ps1 -PfxPath C:\certs\openmix.pfx build\OpenMix_artefacts\Release\OpenMix.exe build\OpenMix_artefacts\Release\df.dll
```

## Getting a certificate

- **A `.pfx` you hold** (what the script takes): a self-signed certificate for testing, or a
  certificate whose private key you are allowed to export. Since June 2023, certificate
  authorities issue new publicly trusted code-signing keys only on hardware tokens or cloud
  HSMs, so a certificate bought today usually can't be exported as a `.pfx`.
- **Azure Trusted Signing** (Microsoft's paid signing service): Microsoft checks your identity
  and keeps the key; CI signs through the `azure/trusted-signing-action` GitHub Action
  instead of this script.
- **SignPath Foundation**: free signing for open-source projects that meet its rules; it signs
  release builds from CI.
- **A certificate on a hardware token or cloud HSM** from a certificate authority: sign with
  the vendor's tools (signtool with their CSP or KSP), not with a `.pfx`.

SmartScreen reputation still builds over time: a new certificate can get warnings until
enough people have downloaded files signed with it.
