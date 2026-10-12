"""HomeDeck Mini release signing helpers: ECDSA P-256 / SHA-256 through the
`openssl` command (no Python packages needed).

Encodings (the firmware uses exactly these):
  public key   65 bytes, uncompressed point 04 || X(32) || Y(32)
  signature    64 bytes, r(32) || s(32), big-endian, zero-padded; base64
               (standard alphabet, padded: 88 characters) in the manifest
  signed data  firmware_updates.canonical_payload(...); ECDSA over its SHA-256

Private keys are only ever passed to openssl as a file path: this module
never reads, prints or copies their contents. openssl runs without a shell;
a passphrase-protected key makes openssl prompt on the terminal.
"""
import base64
import os
from pathlib import Path
import subprocess
import tempfile

# DER SubjectPublicKeyInfo header for an uncompressed P-256 (prime256v1) key:
# SEQUENCE { SEQUENCE { id-ecPublicKey, prime256v1 }, BIT STRING (66 bytes) }.
P256_SPKI_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")
PUBLIC_KEY_BYTES = 65
SIGNATURE_BYTES = 64


class SigningError(Exception):
    """Signing or verification could not be done safely."""


def _openssl(args, timeout=60):
    try:
        result = subprocess.run(["openssl", *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=timeout, check=False)
    except FileNotFoundError:
        raise SigningError("openssl is not installed") from None
    except subprocess.TimeoutExpired:
        raise SigningError(f"openssl {args[0]} timed out") from None
    return result


def public_key_from_spki(der):
    """The 65-byte point from a DER SubjectPublicKeyInfo; SigningError unless
    it is an uncompressed EC P-256 key."""
    if len(der) != len(P256_SPKI_PREFIX) + PUBLIC_KEY_BYTES or not der.startswith(P256_SPKI_PREFIX) \
            or der[len(P256_SPKI_PREFIX)] != 0x04:
        raise SigningError("not an EC P-256 (prime256v1) key")
    return der[len(P256_SPKI_PREFIX):]


def public_key_from_private(key_path):
    """The private key's public point; checks the key type and curve."""
    if not Path(key_path).is_file():
        raise SigningError(f"signing key not found: {key_path}")
    result = _openssl(["pkey", "-in", str(key_path), "-pubout", "-outform", "DER"])
    if result.returncode != 0:
        raise SigningError("openssl could not read the signing key (wrong passphrase or not a private key)")
    return public_key_from_spki(result.stdout)


def _der_integer(value):
    body = value.lstrip(b"\x00") or b"\x00"
    if body[0] & 0x80:
        body = b"\x00" + body
    return b"\x02" + bytes([len(body)]) + body


def raw_to_der(signature):
    if len(signature) != SIGNATURE_BYTES:
        raise SigningError("a raw signature is 64 bytes")
    body = _der_integer(signature[:32]) + _der_integer(signature[32:])
    return b"\x30" + bytes([len(body)]) + body


def der_to_raw(der):
    """r||s from a DER ECDSA-Sig-Value; strict about its shape."""
    def integer(data, pos):
        if pos + 2 > len(data) or data[pos] != 0x02:
            raise SigningError("malformed signature")
        length = data[pos + 1]
        value = data[pos + 2:pos + 2 + length]
        if length == 0 or len(value) != length or length > 33:
            raise SigningError("malformed signature")
        value = value.lstrip(b"\x00")
        if len(value) > 32:
            raise SigningError("malformed signature")
        return value.rjust(32, b"\x00"), pos + 2 + length

    if len(der) < 8 or der[0] != 0x30 or der[1] != len(der) - 2:
        raise SigningError("malformed signature")
    r, pos = integer(der, 2)
    s, pos = integer(der, pos)
    if pos != len(der):
        raise SigningError("malformed signature")
    return r + s


def sign(key_path, payload):
    """Raw 64-byte ECDSA P-256 signature of SHA-256(payload)."""
    with tempfile.TemporaryDirectory() as directory:
        data = Path(directory) / "payload"
        out = Path(directory) / "signature.der"
        data.write_bytes(payload)
        result = _openssl(["dgst", "-sha256", "-sign", str(key_path), "-out", str(out), str(data)])
        if result.returncode != 0 or not out.is_file():
            raise SigningError("openssl could not sign (wrong passphrase or unsupported key)")
        return der_to_raw(out.read_bytes())


def verify(public_key, payload, signature):
    """True if signature (raw r||s) is valid for payload under the 65-byte key."""
    if len(public_key) != PUBLIC_KEY_BYTES or public_key[0] != 0x04 or len(signature) != SIGNATURE_BYTES:
        return False
    with tempfile.TemporaryDirectory() as directory:
        key = Path(directory) / "public.der"
        sig = Path(directory) / "signature.der"
        data = Path(directory) / "payload"
        key.write_bytes(P256_SPKI_PREFIX + public_key)
        sig.write_bytes(raw_to_der(signature))
        data.write_bytes(payload)
        result = _openssl(["dgst", "-sha256", "-verify", str(key), "-keyform", "DER",
                           "-signature", str(sig), str(data)])
        return result.returncode == 0 and b"Verified OK" in result.stdout


def generate_private_key(path, encrypt=True):
    """A new P-256 private key at path (mode 0600; refuses to overwrite).
    encrypt: AES-256 with a passphrase openssl asks for on the terminal."""
    path = Path(path)
    if path.exists():
        raise SigningError(f"{path} already exists; refusing to overwrite a key")
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    args = ["genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
            "-pkeyopt", "ec_param_enc:named_curve", "-out", str(path)]
    if encrypt:
        args.append("-aes-256-cbc")
    old = os.umask(0o077)
    try:
        # Interactive passphrase prompt: openssl reads the terminal directly.
        result = subprocess.run(["openssl", *args], stdout=subprocess.PIPE, stderr=None, check=False)
    except FileNotFoundError:
        raise SigningError("openssl is not installed") from None
    finally:
        os.umask(old)
    if result.returncode != 0 or not path.is_file():
        raise SigningError("openssl could not generate the key")
    os.chmod(path, 0o600)


def b64(signature):
    return base64.b64encode(signature).decode()


def c_array(name, data, per_line=12):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + per_line]) + ",")
    return f"static const uint8_t {name}[{len(data)}] = {{\n" + "\n".join(lines) + "\n};"
