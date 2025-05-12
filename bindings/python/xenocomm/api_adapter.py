from abc import ABC, abstractmethod
from typing import Dict, Any, Optional

class ApiAdapter(ABC):
    """Base interface for all API adapters."""

    @abstractmethod
    def configure(self, base_url: str, default_headers: Optional[Dict[str, str]] = None, timeout: int = 30) -> None:
        """Configure the adapter with connection parameters."""
        pass

    @abstractmethod
    def execute(self, endpoint: str, method: str, data: Optional[Dict[str, Any]] = None, headers: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
        """Execute a request and return the response."""
        pass

    @abstractmethod
    def handle_error(self, error: Exception) -> None:
        """Process and standardize error responses."""
        pass 