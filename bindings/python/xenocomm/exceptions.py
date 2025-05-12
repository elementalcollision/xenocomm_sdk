from typing import Dict, Any, Optional

class ApiError(Exception):
    """Base exception for all API-related errors."""
    pass

class ApiConnectionError(ApiError):
    """Raised when connection to the API fails."""
    pass

class ApiTimeoutError(ApiError):
    """Raised when API request times out."""
    pass

class ApiResponseError(ApiError):
    """Raised when API returns an error response."""
    def __init__(self, message: str, status_code: int, response_data: Optional[Dict[str, Any]] = None):
        self.status_code = status_code
        self.response_data = response_data or {}
        super().__init__(message) 