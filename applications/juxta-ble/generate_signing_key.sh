#!/bin/bash

# Generate MCUboot signing key for JUXTA BLE application
# This script creates an RSA-2048 keypair for signing firmware images

set -e

KEY_DIR="signing_keys"
PRIVATE_KEY="$KEY_DIR/private.pem"
PUBLIC_KEY="$KEY_DIR/public.pem"

echo "Generating MCUboot signing keys for JUXTA BLE..."

# Create keys directory if it doesn't exist
mkdir -p "$KEY_DIR"

# Generate RSA-2048 private key
openssl genrsa -out "$PRIVATE_KEY" 2048

# Extract public key
openssl rsa -in "$PRIVATE_KEY" -pubout -out "$PUBLIC_KEY"

echo "✅ Keys generated successfully:"
echo "   Private key: $PRIVATE_KEY"
echo "   Public key:  $PUBLIC_KEY"
echo ""
echo "⚠️  IMPORTANT:"
echo "   - Keep the private key secure and do NOT commit it to version control"
echo "   - Add '$KEY_DIR/' to your .gitignore file"
echo "   - The public key can be shared for verification purposes"
echo ""
echo "🔧 Next steps:"
echo "   1. Update mcuboot.conf to point to: $PRIVATE_KEY"
echo "   2. Build with sysbuild to create signed images"
echo "   3. Use the signed image for DFU uploads"
