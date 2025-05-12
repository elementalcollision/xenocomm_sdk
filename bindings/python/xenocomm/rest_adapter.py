import requests
from typing import Dict, Any, Optional
from .api_adapter import ApiAdapter
from .exceptions import ApiConnectionError, ApiTimeoutError, ApiResponseError
import time
from .authentication_manager import AuthenticationManager

class RestApiAdapter(ApiAdapter):
    """
    Adapter for RESTful APIs.

    Configuration options:
    - base_url: The base endpoint URL for the API.
    - default_headers: Default headers to include with every request.
    - timeout: Timeout (in seconds) for requests.
    - rate_limiter: Optional RateLimiter instance for request throttling.
    - cache_manager: Optional CacheManager instance for response caching (GET only).
    - auth_manager: Optional AuthenticationManager for request authentication.
    - auth_service: Optional service name for authentication context.
    - Serialization: JSON by default, extensible for other formats.

    Request/Response Serialization:
    - serialize_request(data): Converts request data to the format expected by the API (default: JSON).
    - deserialize_response(response): Converts the API response to a Python object (default: JSON). Raises ApiResponseError if deserialization fails.

    Example usage:
        from xenocomm import RestApiAdapter, AuthenticationManager
        
        config = {
            "my_service": {
                "auth_type": "api_key",
                "credentials": {"api_key": "secret123"},
                "api_key_header": "X-API-Key"
            }
        }
        auth_mgr = AuthenticationManager(config)
        rest = RestApiAdapter(auth_manager=auth_mgr, auth_service="my_service")
        rest.configure("https://jsonplaceholder.typicode.com")
        users = rest.execute("users", "GET")
        print(users)
    """
    def __init__(self, rate_limiter=None, cache_manager=None, auth_manager: Optional[AuthenticationManager] = None, auth_service: Optional[str] = None):
        self.base_url = ""
        self.default_headers = {"Content-Type": "application/json"}
        self.timeout = 30
        self.rate_limiter = rate_limiter
        self.cache_manager = cache_manager
        self.auth_manager = auth_manager
        self.auth_service = auth_service

    def configure(self, base_url: str, default_headers: Optional[Dict[str, str]] = None, timeout: int = 30) -> None:
        """
        Configure the REST API adapter.

        Args:
            base_url: The base endpoint URL for the API.
            default_headers: Default headers to include with every request.
            timeout: Timeout (in seconds) for requests.
        """
        self.base_url = base_url.rstrip('/')
        if default_headers:
            self.default_headers.update(default_headers)
        self.timeout = timeout

    def get_config(self) -> Dict[str, Any]:
        """Return the current configuration as a dictionary."""
        return {
            "base_url": self.base_url,
            "default_headers": self.default_headers.copy(),
            "timeout": self.timeout
        }

    def serialize_request(self, data: Optional[Dict[str, Any]]) -> Any:
        """
        Serialize the request data before sending to the API.
        By default, returns the data as-is for JSON serialization by requests.
        Override this method to support other formats.
        """
        return data

    def deserialize_response(self, response: requests.Response) -> Any:
        """
        Deserialize the API response to a Python object.
        By default, parses JSON. Raises ApiResponseError if deserialization fails.
        Override this method to support other formats.
        """
        try:
            return response.json()
        except Exception as e:
            raise ApiResponseError(f"Failed to deserialize response: {str(e)}", response.status_code, {"raw": response.text})

    def execute(self, endpoint: str, method: str, data: Optional[Dict[str, Any]] = None, headers: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        if not self.base_url:
            raise ValueError("Base URL is not set. Please configure the adapter before making requests.")
        url = f"{self.base_url}/{endpoint.lstrip('/')}"
        request_headers = self.default_headers.copy()
        if headers:
            request_headers.update(headers)
        if self.auth_manager and self.auth_service:
            class ReqObj:
                def __init__(self, headers):
                    self.headers = headers
            req_obj = ReqObj(request_headers)
            self.auth_manager.authenticate(self.auth_service, req_obj)
            request_headers = req_obj.headers
        cache_key = None
        is_get = method.upper() == "GET"
        if is_get and self.cache_manager:
            cache_key = f"{url}:{hash(frozenset((headers or {}).items()))}:{hash(frozenset((data or {}).items()))}"
            cached = self.cache_manager.get(cache_key)
            if cached is not None:
                return cached
        if self.rate_limiter:
            while not self.rate_limiter.acquire():
                time.sleep(0.05)
        try:
            serialized_data = self.serialize_request(data)
            response = requests.request(
                method=method.upper(),
                url=url,
                json=serialized_data if serialized_data else None,
                headers=request_headers,
                timeout=self.timeout
            )
            response.raise_for_status()
            result = self.deserialize_response(response)
            if is_get and self.cache_manager and cache_key:
                self.cache_manager.set(cache_key, result)
            return result
        except requests.exceptions.Timeout as e:
            raise ApiTimeoutError(f"Request timed out: {str(e)}")
        except requests.exceptions.ConnectionError as e:
            raise ApiConnectionError(f"Connection error: {str(e)}")
        except requests.exceptions.HTTPError as e:
            self.handle_error(e)

    def handle_error(self, error: Exception) -> None:
        if isinstance(error, requests.exceptions.HTTPError):
            status_code = error.response.status_code
            try:
                error_data = error.response.json()
            except ValueError:
                error_data = {"message": error.response.text}
            raise ApiResponseError(
                message=f"API error (HTTP {status_code}): {error_data.get('message', 'Unknown error')}",
                status_code=status_code,
                response_data=error_data
            )
        raise error 