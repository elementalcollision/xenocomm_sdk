"""
XenoComm SDK Python Bindings
===========================

This package provides Python bindings for the XenoComm SDK, enabling efficient
communication between AI agents with features like capability discovery,
protocol negotiation, and adaptive feedback.
"""

from ._core import *
from .boundary_gateway import BoundaryGateway, BoundaryGatewayError
from .rest_adapter import RestApiAdapter
from .graphql_adapter import GraphQLAdapter
from .authentication_manager import AuthenticationManager, AuthenticationError

__version__ = '0.1.0' 