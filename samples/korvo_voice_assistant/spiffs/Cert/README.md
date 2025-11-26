# SPIFFS Credential Bundle

The files in this directory are mirrored into `/spiffs/Cert` on the Korvo-1 at
build time. Provision fresh AWS IoT credentials and stage them here with:

```bash
python scripts/provision_and_stage_somnus_cert.py --thing-name SOMNUS_<DEVICE_ID>
```

The helper script invokes `provision_aws_thing.py`, downloads the PEM bundle,
and copies/renames the outputs to the layout `somnus_mqtt` expects:

```
/spiffs/Cert/
  ├── SOMNUS_<DEVICE_ID>-certificate.pem.crt
  ├── SOMNUS_<DEVICE_ID>-private.pem.key
  └── AmazonRootCA1.pem
```

The `.pem` files themselves are ignored by git—only the placeholder README is
tracked—so you can generate device-specific credentials without risking leaks
in version control.
