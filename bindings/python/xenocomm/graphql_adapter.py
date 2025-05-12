import requests
import time
from typing import Dict, Any, Optional, List
from .api_adapter import ApiAdapter
from .exceptions import ApiConnectionError, ApiTimeoutError, ApiResponseError
from .authentication_manager import AuthenticationManager

class GraphQLAdapter(ApiAdapter):
    """
    Adapter for GraphQL APIs.

    Configuration options:
    - endpoint: The GraphQL endpoint URL.
    - default_headers: Default headers to include with every request.
    - timeout: Timeout (in seconds) for requests.
    - rate_limiter: Optional RateLimiter instance for request throttling.
    - cache_manager: Optional CacheManager instance for response caching (query only).
    - auth_manager: Optional AuthenticationManager for request authentication.
    - auth_service: Optional service name for authentication context.
    - Serialization: JSON by default, extensible for other formats.

    Request/Response Serialization:
    - serialize_request(query, variables): Converts query and variables to the format expected by the API (default: JSON).
    - deserialize_response(response): Converts the API response to a Python object (default: JSON). Raises ApiResponseError if deserialization fails.

    Example usage:
        from xenocomm import GraphQLAdapter, AuthenticationManager
        
        config = {
            "my_graphql": {
                "auth_type": "oauth2",
                "credentials": {"access_token": "token123"}
            }
        }
        auth_mgr = AuthenticationManager(config)
        graphql = GraphQLAdapter(auth_manager=auth_mgr, auth_service="my_graphql")
        graphql.configure("https://api.spacex.land/graphql/")
        query = """
        query LaunchesPast($limit: Int!) {
          launchesPast(limit: $limit) {
            mission_name
            launch_date_utc
          }
        }
        """
        data = graphql.query(query, variables={"limit": 2})
        print(data)
    """
    def __init__(self, rate_limiter=None, cache_manager=None, auth_manager: Optional[AuthenticationManager] = None, auth_service: Optional[str] = None):
        self.endpoint = ""
        self.default_headers = {"Content-Type": "application/json"}
        self.timeout = 30
        self.rate_limiter = rate_limiter
        self.cache_manager = cache_manager
        self.auth_manager = auth_manager
        self.auth_service = auth_service

    def configure(self, base_url: str, default_headers: Optional[Dict[str, str]] = None, timeout: int = 30) -> None:
        """
        Configure the GraphQL adapter.

        Args:
            base_url: The GraphQL endpoint URL.
            default_headers: Default headers to include with every request.
            timeout: Timeout (in seconds) for requests.
        """
        self.endpoint = base_url
        if default_headers:
            self.default_headers.update(default_headers)
        self.timeout = timeout

    def get_config(self) -> Dict[str, Any]:
        """Return the current configuration as a dictionary."""
        return {
            "endpoint": self.endpoint,
            "default_headers": self.default_headers.copy(),
            "timeout": self.timeout
        }

    def serialize_request(self, query: str, variables: Optional[Dict[str, Any]]) -> Any:
        """
        Serialize the GraphQL query and variables before sending to the API.
        By default, returns a dict suitable for JSON serialization by requests.
        Override this method to support other formats.
        """
        return {
            "query": query,
            "variables": variables if variables else {}
        }

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

    def execute(self, query: str, method: str = "POST", data: Optional[Dict[str, Any]] = None, headers: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        if not self.endpoint:
            raise ValueError("Endpoint is not set. Please configure the adapter before making requests.")
        request_headers = self.default_headers.copy()
        if headers:
            request_headers.update(headers)
        # --- Authentication integration ---
        if self.auth_manager and self.auth_service:
            class ReqObj:
                def __init__(self, headers):
                    self.headers = headers
            req_obj = ReqObj(request_headers)
            self.auth_manager.authenticate(self.auth_service, req_obj)
            request_headers = req_obj.headers
        payload = self.serialize_request(query, data)
        cache_key = None
        is_query = method.upper() == "POST" and self.cache_manager is not None and data is not None
        if is_query:
            cache_key = f"{self.endpoint}:{hash(query)}:{hash(frozenset((headers or {}).items()))}:{hash(frozenset((data or {}).items()))}"
            cached = self.cache_manager.get(cache_key)
            if cached is not None:
                return cached
        if self.rate_limiter:
            while not self.rate_limiter.acquire():
                time.sleep(0.05)
        try:
            response = requests.post(
                url=self.endpoint,
                json=payload,
                headers=request_headers,
                timeout=self.timeout
            )
            response.raise_for_status()
            result = self.deserialize_response(response)
            if "errors" in result and result["errors"]:
                self.handle_graphql_errors(result["errors"])
            data_result = result.get("data", {})
            if is_query and self.cache_manager and cache_key:
                self.cache_manager.set(cache_key, data_result)
            return data_result
        except requests.exceptions.Timeout as e:
            raise ApiTimeoutError(f"GraphQL request timed out: {str(e)}")
        except requests.exceptions.ConnectionError as e:
            raise ApiConnectionError(f"GraphQL connection error: {str(e)}")
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
                message=f"GraphQL API error (HTTP {status_code}): {error_data.get('message', 'Unknown error')}",
                status_code=status_code,
                response_data=error_data
            )
        raise error

    def handle_graphql_errors(self, errors: List[Dict[str, Any]]) -> None:
        error_messages = [error.get("message", "Unknown GraphQL error") for error in errors]
        raise ApiResponseError(
            message=f"GraphQL errors: {', '.join(error_messages)}",
            status_code=200,  # GraphQL often returns 200 even with errors
            response_data={"errors": errors}
        )

    def query(self, query_string: str, variables: Optional[Dict[str, Any]] = None, headers: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        return self.execute(query_string, data=variables, headers=headers)

    def mutate(self, mutation_string: str, variables: Optional[Dict[str, Any]] = None, headers: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        return self.execute(mutation_string, data=variables, headers=headers) 