## AWS IoT Credential Placeholders

This directory contains placeholder PEM files used to embed credentials at build
time. Replace the contents of the following files with the real certificates and
keys provisioned for each device:

- `root_ca.pem` – Amazon Root CA (or custom trust anchor)
- `device_cert.pem` – Device/Thing certificate issued by AWS IoT Core
- `private_key.pem` – Private key that matches the device certificate

### Important

- **Do not commit real credentials** to source control. After replacing the
  placeholder contents, mark the files as assumed unchanged or manage them
  outside of Git.
- Ensure each file remains PEM-formatted and null-terminated. Adding a newline at
  the end of the file is sufficient.
- Keep file names unchanged. The build system expects these exact paths when
  embedding the credentials.

