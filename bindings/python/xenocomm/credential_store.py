import os
import json
import base64
from typing import Dict, Any, Optional
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import padding
from cryptography.exceptions import InvalidKey

class EncryptedCredentialStore:
    """
    Secure credential store using AES-256 encryption and PBKDF2 key derivation.
    Credentials are stored encrypted at rest in a JSON file.
    """
    def __init__(self, path: str, passphrase: str, salt: Optional[bytes] = None):
        self.path = path
        self.passphrase = passphrase.encode()
        self.salt = salt or os.urandom(16)
        self._key = self._derive_key(self.passphrase, self.salt)
        self._creds: Dict[str, Any] = {}
        if os.path.exists(self.path):
            self.load()

    def _derive_key(self, passphrase: bytes, salt: bytes) -> bytes:
        kdf = PBKDF2HMAC(
            algorithm=hashes.SHA256(),
            length=32,
            salt=salt,
            iterations=100_000,
            backend=default_backend()
        )
        return kdf.derive(passphrase)

    def set_credentials(self, service: str, creds: Dict[str, Any]):
        self._creds[service] = creds

    def get_credentials(self, service: str) -> Optional[Dict[str, Any]]:
        return self._creds.get(service)

    def save(self):
        data = json.dumps(self._creds).encode()
        iv = os.urandom(16)
        padder = padding.PKCS7(128).padder()
        padded_data = padder.update(data) + padder.finalize()
        cipher = Cipher(algorithms.AES(self._key), modes.CBC(iv), backend=default_backend())
        encryptor = cipher.encryptor()
        ct = encryptor.update(padded_data) + encryptor.finalize()
        payload = {
            "salt": base64.b64encode(self.salt).decode(),
            "iv": base64.b64encode(iv).decode(),
            "ct": base64.b64encode(ct).decode()
        }
        with open(self.path, "w") as f:
            json.dump(payload, f)

    def load(self):
        with open(self.path, "r") as f:
            payload = json.load(f)
        salt = base64.b64decode(payload["salt"])
        iv = base64.b64decode(payload["iv"])
        ct = base64.b64decode(payload["ct"])
        key = self._derive_key(self.passphrase, salt)
        cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())
        decryptor = cipher.decryptor()
        padded_data = decryptor.update(ct) + decryptor.finalize()
        unpadder = padding.PKCS7(128).unpadder()
        data = unpadder.update(padded_data) + unpadder.finalize()
        self._creds = json.loads(data.decode())

"""
Example usage:
    from xenocomm.credential_store import EncryptedCredentialStore
    
    store = EncryptedCredentialStore("creds.enc.json", passphrase="mysecret")
    store.set_credentials("my_service", {"api_key": "secret123"})
    store.save()
    # Later or in another process
    store2 = EncryptedCredentialStore("creds.enc.json", passphrase="mysecret")
    creds = store2.get_credentials("my_service")
    print(creds)
""" 