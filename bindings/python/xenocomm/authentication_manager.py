import threading
from typing import Any, Dict, Optional
from .exceptions import ApiError
from .credential_store import EncryptedCredentialStore
import time
import requests
import hmac
import hashlib
import base64
import logging
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

class AuthenticationError(ApiError):
    """Raised when authentication fails or is misconfigured."""
    pass

class AuthenticationManager:
    """
    Manages authentication for external services, supporting multiple auth methods via providers.
    Thread-safe and extensible for API key, OAuth2, JWT, request signing, and custom providers.
    Supports secure credential storage and audit logging of all authentication events.
    """
    def __init__(self, config: Optional[Dict[str, Any]] = None, credential_store: Optional[EncryptedCredentialStore] = None, logger: Optional[logging.Logger] = None):
        self.config = config or {}
        self.auth_providers: Dict[str, Any] = {}
        self.credential_store = credential_store
        self._lock = threading.RLock()
        self.logger = logger or logging.getLogger("xenocomm.auth")
        self._register_default_providers()

    def log_auth_event(self, service_name: str, event_type: str, success: bool, details: Optional[Dict[str, Any]] = None):
        log_entry = {
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "service": service_name,
            "event": event_type,
            "success": success,
            "details": details or {}
        }
        level = logging.INFO if success else logging.WARNING
        self.logger.log(level, f"Auth event: {log_entry}")

    def _register_default_providers(self):
        self.register_provider("api_key", ApiKeyAuthProvider(self))
        self.register_provider("oauth2", OAuth2Provider(self, self.credential_store))
        self.register_provider("jwt", JwtAuthProvider(self))
        self.register_provider("request_signing", RequestSigningAuthProvider(self))

    def register_provider(self, name: str, provider: Any):
        """Register a new authentication provider by name."""
        with self._lock:
            self.auth_providers[name] = provider

    def authenticate(self, service_name: str, request_obj: Any) -> Any:
        """
        Apply authentication to the request based on service configuration.
        Args:
            service_name: Name of the external service (must be in config or credential store)
            request_obj: The request object to modify (e.g., add headers)
        Returns:
            The modified request object with authentication applied.
        Raises:
            AuthenticationError if configuration or provider is missing/invalid.
        """
        with self._lock:
            service_config = self.config.get(service_name, {})
            auth_type = service_config.get("auth_type")
            if not auth_type or auth_type not in self.auth_providers:
                self.log_auth_event(service_name, "request_auth", False, {"reason": "Unknown or missing auth_type"})
                raise AuthenticationError(f"Unknown or missing authentication type for service '{service_name}': {auth_type}")
            provider = self.auth_providers[auth_type]
            # Prefer credential store if available
            credentials = None
            if self.credential_store:
                credentials = self.credential_store.get_credentials(service_name)
                self.log_auth_event(service_name, "credential_access", credentials is not None)
            if credentials is None:
                credentials = service_config.get("credentials")
            try:
                result = provider.authenticate(request_obj, credentials, service_config, service_name=service_name)
                self.log_auth_event(service_name, "request_auth", True)
                return result
            except Exception as e:
                self.log_auth_event(service_name, "request_auth", False, {"error": str(e)})
                raise

# Example provider stubs
class ApiKeyAuthProvider:
    def __init__(self, manager: AuthenticationManager):
        self.manager = manager
    def authenticate(self, request_obj: Any, credentials: Any, config: Dict[str, Any], service_name: Optional[str] = None, **kwargs) -> Any:
        api_key = credentials.get("api_key") if credentials else None
        header_name = config.get("api_key_header", "Authorization")
        if hasattr(request_obj, "headers"):
            request_obj.headers[header_name] = api_key
        self.manager.log_auth_event(service_name or "", "api_key_applied", api_key is not None)
        return request_obj

class OAuth2Provider:
    """
    OAuth2Provider supports automatic token refresh for expiring credentials.
    On each authenticate call, checks expiry and refreshes if needed.
    If using a credential store, updates and saves refreshed tokens.
    Thread-safe per service.
    Requires 'token_url', 'client_id', and 'client_secret' in config or credentials for real refresh.
    """
    def __init__(self, manager: AuthenticationManager, credential_store: Optional[EncryptedCredentialStore] = None):
        self.manager = manager
        self.credential_store = credential_store
        self._token_cache = {}  # service_name -> (token, expiry)
        self._refresh_locks = {}  # service_name -> threading.Lock

    def authenticate(self, request_obj: Any, credentials: Any, config: Dict[str, Any], service_name: Optional[str] = None) -> Any:
        if not credentials:
            self.manager.log_auth_event(service_name or "", "oauth2_auth", False, {"reason": "No credentials"})
            return request_obj
        token = credentials.get("access_token")
        expiry = credentials.get("expiry")  # Unix timestamp
        refresh_token = credentials.get("refresh_token")
        refresh_margin = config.get("refresh_margin", 300)  # seconds before expiry
        now = int(time.time())
        needs_refresh = False
        if token and expiry:
            if now > (expiry - refresh_margin):
                needs_refresh = True
        elif not token:
            needs_refresh = True
        if needs_refresh and service_name:
            lock = self._refresh_locks.setdefault(service_name, threading.Lock())
            with lock:
                # Double-check after acquiring lock
                if self.credential_store:
                    credentials = self.credential_store.get_credentials(service_name)
                    token = credentials.get("access_token")
                    expiry = credentials.get("expiry")
                    refresh_token = credentials.get("refresh_token")
                if not token or (expiry and now > (expiry - refresh_margin)):
                    # Perform token refresh
                    try:
                        new_token, new_expiry = self._refresh_token(credentials, config)
                        credentials["access_token"] = new_token
                        credentials["expiry"] = new_expiry
                        token = new_token
                        expiry = new_expiry
                        if self.credential_store:
                            self.credential_store.set_credentials(service_name, credentials)
                            self.credential_store.save()
                        self.manager.log_auth_event(service_name, "token_refresh", True)
                    except Exception as e:
                        self.manager.log_auth_event(service_name, "token_refresh", False, {"error": str(e)})
                        raise
        if hasattr(request_obj, "headers") and token:
            request_obj.headers["Authorization"] = f"Bearer {token}"
        self.manager.log_auth_event(service_name or "", "oauth2_auth", token is not None)
        return request_obj

    def _refresh_token(self, credentials: Dict[str, Any], config: Dict[str, Any]) -> (str, int):
        """
        Refresh the OAuth2 token using the real token endpoint.
        Returns (new_token, new_expiry_unix_timestamp)
        Raises AuthenticationError on failure.
        """
        token_url = config.get("token_url") or credentials.get("token_url")
        client_id = config.get("client_id") or credentials.get("client_id")
        client_secret = config.get("client_secret") or credentials.get("client_secret")
        refresh_token = credentials.get("refresh_token")
        scope = config.get("scope") or credentials.get("scope")
        if not all([token_url, client_id, client_secret, refresh_token]):
            raise AuthenticationError("Missing required OAuth2 refresh parameters (token_url, client_id, client_secret, refresh_token)")
        data = {
            "grant_type": "refresh_token",
            "refresh_token": refresh_token,
            "client_id": client_id,
            "client_secret": client_secret
        }
        if scope:
            data["scope"] = scope
        try:
            resp = requests.post(token_url, data=data, timeout=10)
            resp.raise_for_status()
            resp_json = resp.json()
            new_token = resp_json.get("access_token")
            expires_in = resp_json.get("expires_in")
            new_refresh_token = resp_json.get("refresh_token")
            if not new_token or not expires_in:
                raise AuthenticationError(f"OAuth2 refresh response missing fields: {resp_json}")
            new_expiry = int(time.time()) + int(expires_in)
            if new_refresh_token:
                credentials["refresh_token"] = new_refresh_token
            return new_token, new_expiry
        except Exception as e:
            raise AuthenticationError(f"OAuth2 token refresh failed: {e}")

class JwtAuthProvider:
    def __init__(self, manager: AuthenticationManager):
        self.manager = manager
    def authenticate(self, request_obj: Any, credentials: Any, config: Dict[str, Any], service_name: Optional[str] = None, **kwargs) -> Any:
        jwt_token = credentials.get("jwt") if credentials else None
        if hasattr(request_obj, "headers"):
            request_obj.headers["Authorization"] = f"JWT {jwt_token}"
        self.manager.log_auth_event(service_name or "", "jwt_applied", jwt_token is not None)
        return request_obj

class RequestSigningAuthProvider:
    """
    RequestSigningAuthProvider supports HMAC-SHA256 and Ed25519 request signing.
    Signs the request using a secret key and adds the signature to a header or query parameter.
    Config/credentials must provide:
      - 'signing_key': secret key (base64 or hex encoded for HMAC, base64 Ed25519 private key for Ed25519)
      - 'signing_algorithm': 'hmac-sha256' (default) or 'ed25519'
      - 'signature_header': header name to use (default: 'X-Signature')
      - 'signature_param': query param name (optional, if signature should be in URL)
      - 'signing_payload': 'body', 'headers', or 'custom' (default: 'body')
    """
    def __init__(self, manager: AuthenticationManager):
        self.manager = manager
    def authenticate(self, request_obj: Any, credentials: Any, config: Dict[str, Any], service_name: Optional[str] = None, **kwargs) -> Any:
        key = credentials.get("signing_key") or config.get("signing_key")
        algorithm = (credentials.get("signing_algorithm") or config.get("signing_algorithm") or "hmac-sha256").lower()
        header_name = credentials.get("signature_header") or config.get("signature_header") or "X-Signature"
        param_name = credentials.get("signature_param") or config.get("signature_param")
        payload_type = credentials.get("signing_payload") or config.get("signing_payload") or "body"
        if not key:
            self.manager.log_auth_event(service_name or "", "request_signing", False, {"reason": "Missing signing_key"})
            raise AuthenticationError("Missing signing_key for request signing auth.")
        # Prepare payload to sign
        if payload_type == "body" and hasattr(request_obj, "body") and request_obj.body:
            payload = request_obj.body if isinstance(request_obj.body, bytes) else request_obj.body.encode()
        elif payload_type == "headers" and hasattr(request_obj, "headers"):
            headers = request_obj.headers
            payload = "\n".join(f"{k.lower()}:{v}" for k, v in sorted(headers.items())).encode()
        elif payload_type == "custom" and hasattr(request_obj, "get_signing_payload"):
            payload = request_obj.get_signing_payload()
            if isinstance(payload, str):
                payload = payload.encode()
        else:
            payload = b""
        # Signature
        try:
            if algorithm == "hmac-sha256":
                # Decode key
                try:
                    if all(c in "0123456789abcdefABCDEF" for c in key) and len(key) % 2 == 0:
                        key_bytes = bytes.fromhex(key)
                    else:
                        key_bytes = base64.b64decode(key)
                except Exception:
                    key_bytes = key.encode()
                signature = hmac.new(key_bytes, payload, hashlib.sha256).digest()
                signature_b64 = base64.b64encode(signature).decode()
            elif algorithm == "ed25519":
                key_bytes = base64.b64decode(key)
                private_key = Ed25519PrivateKey.from_private_bytes(key_bytes)
                signature = private_key.sign(payload)
                signature_b64 = base64.b64encode(signature).decode()
            else:
                self.manager.log_auth_event(service_name or "", "request_signing", False, {"reason": f"Unsupported algorithm: {algorithm}"})
                raise AuthenticationError(f"Unsupported signing algorithm: {algorithm}")
            # Add signature to header or query param
            if hasattr(request_obj, "headers"):
                request_obj.headers[header_name] = signature_b64
            if param_name and hasattr(request_obj, "url"):
                from urllib.parse import urlencode, urlparse, urlunparse, parse_qsl
                url = request_obj.url
                parsed = urlparse(url)
                q = dict(parse_qsl(parsed.query))
                q[param_name] = signature_b64
                new_query = urlencode(q)
                request_obj.url = urlunparse(parsed._replace(query=new_query))
            self.manager.log_auth_event(service_name or "", "request_signing", True, {"algorithm": algorithm})
            return request_obj
        except Exception as e:
            self.manager.log_auth_event(service_name or "", "request_signing", False, {"error": str(e)})
            raise

# Usage example (in docstring):
"""
Example usage:
    from xenocomm.authentication_manager import AuthenticationManager
    import base64
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    
    # Generate Ed25519 private key (for demonstration)
    private_key = Ed25519PrivateKey.generate()
    private_bytes = private_key.private_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PrivateFormat.Raw,
        encryption_algorithm=serialization.NoEncryption()
    )
    signing_key_b64 = base64.b64encode(private_bytes).decode()
    
    config = {
        "my_signed_service": {
            "auth_type": "request_signing",
            "signing_key": signing_key_b64,
            "signing_algorithm": "ed25519",
            "signature_header": "X-Signature",
            "signing_payload": "body"
        }
    }
    auth_mgr = AuthenticationManager(config)
    request = SomeRequestObject()
    request.body = b"important data"
    authed_request = auth_mgr.authenticate("my_signed_service", request)
    print(authed_request.headers["X-Signature"])
""" 