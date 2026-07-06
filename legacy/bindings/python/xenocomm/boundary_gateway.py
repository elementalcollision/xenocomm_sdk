"""
XenoComm SDK Python Bindings
===========================

This package provides Python bindings for the XenoComm SDK, enabling efficient
communication between AI agents with features like capability discovery,
protocol negotiation, and adaptive feedback.

Example usage:

    from xenocomm import BoundaryGateway, RestApiAdapter, GraphQLAdapter

    gateway = BoundaryGateway()
    gateway.register_adapter("rest", RestApiAdapter())
    gateway.register_adapter("graphql", GraphQLAdapter())

    # Configure and use REST adapter
    rest = gateway.get_adapter("rest")
    rest.configure("https://jsonplaceholder.typicode.com")
    users = rest.execute("users", "GET")
    print(users)

    # Configure and use GraphQL adapter
    graphql = gateway.get_adapter("graphql")
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

import abc
import logging
from typing import Any, Dict, Optional
from .translation_service import TranslationService
from .rate_limiter import RateLimiter
from .cache_manager import CacheManager

class BoundaryGatewayError(Exception):
    """Custom exception for BoundaryGateway-related errors."""
    pass

class BoundaryGateway(abc.ABC):
    """
    Abstract base class for external communication gateways.
    Provides core interface, configuration, logging, error handling, adapter registry, and data translation.

    Attributes:
        translation_service: Instance of TranslationService for data format conversion, schema validation, and template formatting.
        rate_limiter: Optional shared RateLimiter for all adapters.
        cache_manager: Optional shared CacheManager for all adapters.
    """
    def __init__(self, config: Optional[Dict[str, Any]] = None, rate_limiter: Optional[Any] = None, cache_manager: Optional[Any] = None):
        self.config = config or {}
        self.logger = logging.getLogger(self.__class__.__name__)
        self.adapters: Dict[str, Any] = {}
        self.initialized = False
        self.translation_service = TranslationService()
        self.rate_limiter = rate_limiter
        self.cache_manager = cache_manager

    @abc.abstractmethod
    def send_request(self, *args, **kwargs) -> Any:
        """Send a request to an external system. Must be implemented by subclasses."""
        pass

    @abc.abstractmethod
    def receive_response(self, *args, **kwargs) -> Any:
        """Receive a response from an external system. Must be implemented by subclasses."""
        pass

    def load_config(self, config: Dict[str, Any]):
        """Load or update configuration settings for the gateway."""
        self.config.update(config)
        self.log(f"Configuration loaded: {config}", logging.DEBUG)

    def log(self, message: str, level: int = logging.INFO):
        """Log a message with the specified logging level."""
        if self.logger:
            self.logger.log(level, message)

    def register_adapter(self, name: str, adapter: Any):
        """Register an adapter for external communication."""
        if name in self.adapters:
            self.log(f"Adapter '{name}' is already registered. Overwriting.", logging.WARNING)
        self.adapters[name] = adapter
        self.log(f"Adapter '{name}' registered.", logging.DEBUG)

    def get_adapter(self, name: str) -> Any:
        """Retrieve a registered adapter by name."""
        try:
            return self.adapters[name]
        except KeyError:
            self.log(f"Adapter '{name}' not found.", logging.ERROR)
            raise BoundaryGatewayError(f"Adapter '{name}' not found.")

    def initialize(self):
        """Initialize the gateway (e.g., establish connections, prepare resources)."""
        self.initialized = True
        self.log("BoundaryGateway initialized.", logging.INFO)

    def shutdown(self):
        """Shutdown the gateway (e.g., close connections, cleanup resources)."""
        self.initialized = False
        self.log("BoundaryGateway shutdown.", logging.INFO)

    def __enter__(self):
        self.initialize()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.shutdown()

    def create_rest_adapter(self):
        from .rest_adapter import RestApiAdapter
        return RestApiAdapter(rate_limiter=self.rate_limiter, cache_manager=self.cache_manager)

    def create_graphql_adapter(self):
        from .graphql_adapter import GraphQLAdapter
        return GraphQLAdapter(rate_limiter=self.rate_limiter, cache_manager=self.cache_manager) 