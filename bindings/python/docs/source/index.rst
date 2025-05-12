.. xenocomm documentation master file, created by
   sphinx-quickstart on Wed Apr 30 14:13:19 2025.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

xenocomm documentation
======================

Quickstart Example
------------------

.. code-block:: python

    from xenocomm import ConnectionManager

    # Create a connection manager and connect to a server
    with ConnectionManager("localhost", 8080) as conn:
        if conn.is_connected():
            print("Connected!")
        # ... perform operations ...
    # Connection is automatically closed on exit

API Reference
-------------

.. automodule:: xenocomm
    :members:
    :undoc-members:
    :show-inheritance:

Data Translation & Validation (Python)
--------------------------------------

The Python bindings provide a `TranslationService` for converting between internal and external data formats, validating schemas, and generating human-readable outputs.

**Features:**

- JSON and XML serialization/deserialization (requires `xmltodict` for XML)
- JSON schema validation (requires `jsonschema`)
- Template-based formatting (requires `jinja2`)
- Type mapping registry for bidirectional translation between internal and external types
- Custom pre/post hooks for translation pipelines
- Comprehensive error handling with context

**Example:**

.. code-block:: python

    from xenocomm.translation_service import TranslationService
    svc = TranslationService()
    obj = {"foo": 1, "bar": [2, 3]}
    json_str = svc.to_json(obj)
    assert svc.from_json(json_str) == obj

    # See the Python API for more details on advanced features.

Rate Limiting & Caching (Python)
-------------------------------

The Python bindings provide built-in support for rate limiting and response caching via the `RateLimiter` and `CacheManager` classes. These can be used with the REST and GraphQL adapters to control request rates and improve performance.

**Features:**

- Thread-safe token bucket rate limiting (per-endpoint or shared)
- TTL-based response caching with manual/conditional invalidation
- Metrics for monitoring rate limit and cache usage

**Example:**

.. code-block:: python

    from xenocomm.rest_adapter import RestApiAdapter
    from xenocomm.rate_limiter import RateLimiter
    from xenocomm.cache_manager import CacheManager

    rate_limiter = RateLimiter(capacity=5, refill_rate=1)
    cache_manager = CacheManager()
    rest = RestApiAdapter(rate_limiter=rate_limiter, cache_manager=cache_manager)
    rest.configure("https://api.example.com")
    data = rest.execute("users", "GET")
    print("Rate limiter:", rate_limiter.get_metrics())
    print("Cache:", cache_manager.get_metrics())

See the Python API for more details on advanced configuration and integration.

.. toctree::
   :maxdepth: 2
   :caption: Contents:

