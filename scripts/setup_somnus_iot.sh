#!/usr/bin/env bash
#
# Provision a Somnus-compatible AWS IoT Thing, certificate, and policy using AWS CLI.
# Creates a certificate bundle suitable for placing in the firmware's certificate directory.
#
# Requirements:
#   - aws CLI v2 with credentials configured (`aws configure`)
#   - curl
#   - jq
#
# Usage:
#   ./scripts/setup_somnus_iot.sh [-n THING_NAME] [-o OUTPUT_DIR] [-r AWS_REGION]
#     - THING_NAME defaults to "SomnusTestDevice-<timestamp>"
#     - OUTPUT_DIR defaults to "./somnus-iot-cert"
#     - AWS_REGION defaults to current AWS CLI region (or environment)
#
set -euo pipefail

usage() {
  sed -n '1,33p' "$0"
}

THING_NAME=""
OUTPUT_DIR=""
AWS_REGION=""

while getopts ":n:o:r:h" opt; do
  case "${opt}" in
    n) THING_NAME="${OPTARG}" ;;
    o) OUTPUT_DIR="${OPTARG}" ;;
    r) AWS_REGION="${OPTARG}" ;;
    h)
      usage
      exit 0
      ;;
    \?)
      echo "Unknown option: -${OPTARG}" >&2
      usage
      exit 1
      ;;
    :)
      echo "Option -${OPTARG} requires an argument." >&2
      usage
      exit 1
      ;;
  esac
done

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Error: required command '$1' not found in PATH" >&2
    exit 1
  fi
}

require_cmd aws
require_cmd jq
require_cmd curl

if [[ -z "${THING_NAME}" ]]; then
  THING_NAME="SomnusTestDevice-$(date +%Y%m%d-%H%M%S)"
fi

if [[ -z "${OUTPUT_DIR}" ]]; then
  OUTPUT_DIR="./somnus-iot-cert"
fi

mkdir -p "${OUTPUT_DIR}"

AWS_ARGS=()

aws_cli() {
  if [[ ${#AWS_ARGS[@]} -gt 0 ]]; then
    aws "${AWS_ARGS[@]}" "$@"
  else
    aws "$@"
  fi
}

if [[ -n "${AWS_REGION}" ]]; then
  AWS_ARGS+=(--region "${AWS_REGION}")
fi

echo "Using Thing name: ${THING_NAME}"
echo "Writing files to: ${OUTPUT_DIR}"

METADATA_PATH="${OUTPUT_DIR}/${THING_NAME}-cert-metadata.json"
POLICY_DOC_PATH="${OUTPUT_DIR}/${THING_NAME}-policy.json"
POLICY_NAME="${THING_NAME}-Policy"
CERT_PATH="${OUTPUT_DIR}/${THING_NAME}-certificate.pem.crt"
KEY_PATH="${OUTPUT_DIR}/${THING_NAME}-private.pem.key"
CA_PATH="${OUTPUT_DIR}/AmazonRootCA1.pem"

# 1. Create (or ensure) the IoT Thing exists.
if aws_cli iot describe-thing --thing-name "${THING_NAME}" >/dev/null 2>&1; then
  echo "Thing '${THING_NAME}' already exists — continuing."
else
  echo "Creating IoT Thing '${THING_NAME}'..."
  aws_cli iot create-thing --thing-name "${THING_NAME}" >/dev/null
fi

# 2. Generate keys & certificate (active).
echo "Creating certificate and private key..."
aws_cli iot create-keys-and-certificate \
  --set-as-active \
  --certificate-pem-outfile "${CERT_PATH}" \
  --private-key-outfile "${KEY_PATH}" \
  > "${METADATA_PATH}"

CERT_ARN=$(jq -r '.certificateArn' "${METADATA_PATH}")
CERT_ID=$(jq -r '.certificateId' "${METADATA_PATH}")
echo "Certificate ARN: ${CERT_ARN}"

# 3. Build permissive policy document (for test use).
cat > "${POLICY_DOC_PATH}" <<'EOF'
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "iot:Connect",
        "iot:Publish",
        "iot:Receive",
        "iot:Subscribe"
      ],
      "Resource": "*"
    }
  ]
}
EOF

# 4. Create policy if missing, otherwise skip.
if aws_cli iot get-policy --policy-name "${POLICY_NAME}" >/dev/null 2>&1; then
  echo "Policy '${POLICY_NAME}' already exists — skipping creation."
else
  echo "Creating policy '${POLICY_NAME}'..."
  aws_cli iot create-policy \
    --policy-name "${POLICY_NAME}" \
    --policy-document "file://${POLICY_DOC_PATH}" >/dev/null
fi

# 5. Attach policy to the certificate.
echo "Attaching policy to certificate..."
aws_cli iot attach-policy \
  --policy-name "${POLICY_NAME}" \
  --target "${CERT_ARN}"

# 6. Attach thing principal.
echo "Attaching certificate to Thing..."
aws_cli iot attach-thing-principal \
  --thing-name "${THING_NAME}" \
  --principal "${CERT_ARN}"

# 7. Download AWS IoT Root CA.
if [[ ! -f "${CA_PATH}" ]]; then
  echo "Downloading AmazonRootCA1.pem..."
  curl -sSL "https://www.amazontrust.com/repository/AmazonRootCA1.pem" -o "${CA_PATH}"
else
  echo "CA file already exists at ${CA_PATH} — leaving untouched."
fi

# 8. Output the ATS endpoint.
ENDPOINT=$(aws_cli iot describe-endpoint --endpoint-type iot:Data-ATS --output text)
echo "AWS IoT ATS endpoint: ${ENDPOINT}"

cat <<EOF

Provisioning complete.
- Thing: ${THING_NAME}
- Certificate ID: ${CERT_ID}
- Bundle directory: ${OUTPUT_DIR}
  * ${THING_NAME}-certificate.pem.crt
  * ${THING_NAME}-private.pem.key
  * AmazonRootCA1.pem

Copy these files into the firmware's Somnus certificate directory (default: /spiffs/Cert).
Remember to adjust the policy for production use and clean up test resources when finished.
EOF
