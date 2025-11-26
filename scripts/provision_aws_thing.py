#!/usr/bin/env python3
"""
Provision an AWS IoT Core Thing with certificate material for Naphome devices.

This helper creates (or reuses) a Thing, generates an X.509 certificate +
private key, attaches an IoT policy, and writes the artifacts to disk.

Prerequisites:
  * AWS credentials with IoT administrative permissions available in the
    environment (via ~/.aws/credentials, environment variables, SSO, etc.)
  * boto3 installed in the active Python environment.

Example:
    python scripts/provision_aws_thing.py \\
        --thing-name SOMNUS_ABCDEF123456 \\
        --policy-name SomnusDevicePolicy \\
        --output-dir components/aws_iot/certs/generated/SOMNUS_ABCDEF123456
"""

from __future__ import annotations

import argparse
import json
import sys
import textwrap
import urllib.request
from pathlib import Path
from typing import Any, Dict

import boto3
from botocore.exceptions import ClientError

DEFAULT_ROOT_CA_URL = "https://www.amazontrust.com/repository/AmazonRootCA1.pem"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Provision an AWS IoT Thing, certificates, and policy bindings.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--thing-name",
        required=True,
        help="Name of the Thing to create or reuse (typically the device ID).",
    )
    parser.add_argument(
        "--policy-name",
        default="SomnusDevicePolicy",
        help="IoT policy to attach to the certificate. Created if it does not exist.",
    )
    parser.add_argument(
        "--policy-document",
        type=Path,
        help="Optional path to a JSON policy document. If omitted, a permissive default is used.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("components/aws_iot/certs/generated"),
        help="Directory where certificate files will be written.",
    )
    parser.add_argument(
        "--region",
        default="ap-south-1",
        help="AWS region for the IoT Core deployment.",
    )
    parser.add_argument(
        "--root-ca-url",
        default=DEFAULT_ROOT_CA_URL,
        help="URL to download the Amazon Root CA PEM.",
    )
    parser.add_argument(
        "--no-activate",
        action="store_true",
        help="Generate the certificate without setting it ACTIVE.",
    )
    return parser.parse_args()


def ensure_thing(iot, thing_name: str) -> None:
    try:
        iot.describe_thing(thingName=thing_name)
        print(f"[INFO] Thing '{thing_name}' already exists.")
    except ClientError as err:
        if err.response["Error"]["Code"] != "ResourceNotFoundException":
            raise
        print(f"[INFO] Creating Thing '{thing_name}'.")
        iot.create_thing(thingName=thing_name)


def load_policy_document(args: argparse.Namespace, region: str, account: str) -> Dict[str, Any]:
    if args.policy_document:
        with args.policy_document.open("r", encoding="utf-8") as fh:
            return json.load(fh)

    # Default policy grants connect/publish/subscribe access scoped to Thing name topics.
    thing_client_arn = f"arn:aws:iot:{region}:{account}:client/${{iot:Connection.Thing.ThingName}}"
    thing_topic_prefix = f"arn:aws:iot:{region}:{account}:topic"
    thing_topicfilter_prefix = f"arn:aws:iot:{region}:{account}:topicfilter"

    topic_patterns = [
        f"{thing_topic_prefix}/device/*/${{iot:Connection.Thing.ThingName}}",
        f"{thing_topic_prefix}/device/somnus/${{iot:Connection.Thing.ThingName}}/*",
        f"{thing_topic_prefix}/device/telemetry/${{iot:Connection.Thing.ThingName}}",
        f"{thing_topic_prefix}/device/receive/*/${{iot:Connection.Thing.ThingName}}",
    ]

    topic_filter_patterns = [
        f"{thing_topicfilter_prefix}/device/*/${{iot:Connection.Thing.ThingName}}",
        f"{thing_topicfilter_prefix}/device/somnus/${{iot:Connection.Thing.ThingName}}/*",
        f"{thing_topicfilter_prefix}/device/telemetry/${{iot:Connection.Thing.ThingName}}",
        f"{thing_topicfilter_prefix}/device/receive/*/${{iot:Connection.Thing.ThingName}}",
    ]

    return {
        "Version": "2012-10-17",
        "Statement": [
            {
                "Effect": "Allow",
                "Action": ["iot:Connect"],
                "Resource": thing_client_arn,
            },
            {
                "Effect": "Allow",
                "Action": ["iot:Publish", "iot:Receive"],
                "Resource": topic_patterns,
            },
            {
                "Effect": "Allow",
                "Action": ["iot:Subscribe"],
                "Resource": topic_filter_patterns,
            },
        ],
    }


def ensure_policy(iot, policy_name: str, document: Dict[str, Any]) -> None:
    try:
        iot.get_policy(policyName=policy_name)
        print(f"[INFO] Policy '{policy_name}' already exists.")
    except ClientError as err:
        if err.response["Error"]["Code"] != "ResourceNotFoundException":
            raise
        print(f"[INFO] Creating policy '{policy_name}'.")
        iot.create_policy(policyName=policy_name, policyDocument=json.dumps(document))


def create_certificate(iot, set_active: bool) -> Dict[str, str]:
    response = iot.create_keys_and_certificate(setAsActive=set_active)
    return {
        "certificate_arn": response["certificateArn"],
        "certificate_id": response["certificateId"],
        "certificate_pem": response["certificatePem"],
        "private_key": response["keyPair"]["PrivateKey"],
        "public_key": response["keyPair"]["PublicKey"],
    }


def attach_resources(iot, thing_name: str, certificate_arn: str, policy_name: str) -> None:
    print(f"[INFO] Attaching certificate to Thing '{thing_name}'.")
    iot.attach_thing_principal(thingName=thing_name, principal=certificate_arn)
    print(f"[INFO] Attaching policy '{policy_name}' to certificate.")
    iot.attach_policy(policyName=policy_name, target=certificate_arn)


def download_root_ca(url: str) -> str:
    print(f"[INFO] Downloading root CA from {url}")
    with urllib.request.urlopen(url) as response:
        data = response.read()
    return data.decode("utf-8")


def write_artifacts(output_dir: Path, thing_name: str, cert_bundle: Dict[str, str], root_ca_pem: str) -> Dict[str, Path]:
    thing_dir = output_dir / thing_name
    thing_dir.mkdir(parents=True, exist_ok=True)

    paths = {
        "device_cert": thing_dir / "device_cert.pem",
        "private_key": thing_dir / "private_key.pem",
        "public_key": thing_dir / "public_key.pem",
        "root_ca": thing_dir / "root_ca.pem",
        "metadata": thing_dir / "provisioning.json",
    }

    paths["device_cert"].write_text(cert_bundle["certificate_pem"], encoding="utf-8")
    paths["private_key"].write_text(cert_bundle["private_key"], encoding="utf-8")
    paths["public_key"].write_text(cert_bundle["public_key"], encoding="utf-8")
    paths["root_ca"].write_text(root_ca_pem, encoding="utf-8")

    metadata = {
        "certificateArn": cert_bundle["certificate_arn"],
        "certificateId": cert_bundle["certificate_id"],
        "thingName": thing_name,
    }
    paths["metadata"].write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    return paths


def main() -> int:
    args = parse_args()

    session = boto3.Session(region_name=args.region)
    iot = session.client("iot")
    sts = session.client("sts")
    account_id = sts.get_caller_identity()["Account"]

    ensure_thing(iot, args.thing_name)

    policy_doc = load_policy_document(args, args.region, account_id)
    ensure_policy(iot, args.policy_name, policy_doc)

    cert_bundle = create_certificate(iot, set_active=not args.no_activate)
    attach_resources(iot, args.thing_name, cert_bundle["certificate_arn"], args.policy_name)

    root_ca_pem = download_root_ca(args.root_ca_url)
    paths = write_artifacts(args.output_dir, args.thing_name, cert_bundle, root_ca_pem)

    summary = textwrap.dedent(
        f"""
        Provisioning complete!

        Thing Name: {args.thing_name}
        Certificate ARN: {cert_bundle['certificate_arn']}
        Output Directory: {paths['device_cert'].parent}

        To use these credentials with the firmware:
          1. Copy the generated device_cert.pem, private_key.pem, and root_ca.pem into
             components/aws_iot/certs (overwriting the placeholders) or configure the
             firmware to load them from an external filesystem.
          2. Update menuconfig with the Thing name as the client ID and ensure the
             endpoint matches your AWS IoT Core instance.
          3. Do NOT commit real credentials to version control.
        """
    ).strip()

    print(summary)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ClientError as exc:
        print(f"[ERROR] AWS request failed: {exc}")
        sys.exit(1)
    except Exception as exc:  # pylint: disable=broad-except
        print(f"[ERROR] {exc}")
        sys.exit(1)

