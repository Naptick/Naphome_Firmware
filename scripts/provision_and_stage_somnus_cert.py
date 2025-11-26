#!/usr/bin/env python3
"""
Provision an AWS IoT Thing (via provision_aws_thing.py) and stage the resulting
certificates into the Korvo Voice Assistant SPIFFS image so builds automatically
bundle the credential set.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List

REPO_ROOT = Path(__file__).resolve().parent.parent
PROVISION_SCRIPT = REPO_ROOT / "scripts" / "provision_aws_thing.py"
DEFAULT_OUTPUT_DIR = REPO_ROOT / "components" / "aws_iot" / "certs" / "generated"
DEFAULT_SPIFFS_CERT_DIR = (
    REPO_ROOT / "samples" / "korvo_voice_assistant" / "spiffs" / "Cert"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Provision an AWS IoT Thing and stage certs into the SPIFFS bundle."
    )
    parser.add_argument(
        "--thing-name",
        required=True,
        help="Thing/Device ID (e.g., SOMNUS_ABCDEF123456)",
    )
    parser.add_argument(
        "--policy-name",
        default="SomnusDevicePolicy",
        help="IoT policy to attach when provisioning",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Directory where provision_aws_thing.py writes artifacts",
    )
    parser.add_argument(
        "--spiffs-cert-dir",
        type=Path,
        default=DEFAULT_SPIFFS_CERT_DIR,
        help="Destination directory mirrored into /spiffs/Cert on build",
    )
    parser.add_argument(
        "--region",
        default="ap-south-1",
        help="AWS region for the IoT Core deployment",
    )
    parser.add_argument(
        "--root-ca-url",
        default=None,
        help="Override the Root CA download URL",
    )
    parser.add_argument(
        "--skip-provision",
        action="store_true",
        help="Reuse existing artifacts instead of invoking provision_aws_thing.py",
    )
    return parser.parse_args()


def run_provision(args: argparse.Namespace) -> None:
    cmd: List[str] = [
        sys.executable,
        str(PROVISION_SCRIPT),
        "--thing-name",
        args.thing_name,
        "--policy-name",
        args.policy_name,
        "--output-dir",
        str(args.output_dir),
        "--region",
        args.region,
    ]
    if args.root_ca_url:
        cmd.extend(["--root-ca-url", args.root_ca_url])
    print(f"[INFO] Running {' '.join(cmd)}")
    subprocess.check_call(cmd, cwd=REPO_ROOT)


def stage_files(args: argparse.Namespace) -> None:
    source_dir = args.output_dir / args.thing_name
    if not source_dir.exists():
        raise FileNotFoundError(
            f"Provisioning output not found at {source_dir}. "
            "Run without --skip-provision or adjust --output-dir."
        )

    cert_path = source_dir / "device_cert.pem"
    key_path = source_dir / "private_key.pem"
    root_ca_path = source_dir / "root_ca.pem"

    for path in (cert_path, key_path, root_ca_path):
        if not path.exists():
            raise FileNotFoundError(f"Expected file missing: {path}")

    dest_dir = args.spiffs_cert_dir
    dest_dir.mkdir(parents=True, exist_ok=True)

    rename_map = {
        cert_path: dest_dir / f"{args.thing_name}-certificate.pem.crt",
        key_path: dest_dir / f"{args.thing_name}-private.pem.key",
        root_ca_path: dest_dir / "AmazonRootCA1.pem",
    }

    for src, dst in rename_map.items():
        shutil.copy2(src, dst)
        print(f"[INFO] Copied {src} -> {dst}")

    print(
        "[INFO] Certificate bundle ready. "
        "Next build/flash will embed the updated SPIFFS partition."
    )


def main() -> int:
    args = parse_args()

    if not PROVISION_SCRIPT.exists():
        print(f"[ERROR] Missing helper script at {PROVISION_SCRIPT}")
        return 1

    if not args.skip_provision:
        run_provision(args)
    else:
        print("[INFO] Skipping provisioning; reusing existing artifacts.")

    stage_files(args)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as exc:
        print(f"[ERROR] Provisioning script failed with code {exc.returncode}")
        sys.exit(exc.returncode)
    except Exception as exc:  # pylint: disable=broad-except
        print(f"[ERROR] {exc}")
        sys.exit(1)
