"""
XenoComm MCP Server
===================

An MCP (Model Context Protocol) server that brings sophisticated agent-to-agent
coordination capabilities to the agentic AI ecosystem.

This server exposes XenoComm's alignment, negotiation, and emergence capabilities
as MCP tools that can be used by any MCP-compatible client (Claude Code, Moltbot,
Cursor, etc.).

Key Features:
- Alignment verification between heterogeneous agents
- Protocol negotiation state machine
- Emergence/evolution of communication protocols
- Capability discovery and matching

Usage:
    # Run as stdio server (for Claude Code, etc.)
    python -m xenocomm_mcp

    # Or run as HTTP server
    python -m xenocomm_mcp --transport http --port 8000
"""

# Core engines (no external dependencies)
from .alignment import AlignmentEngine, AgentContext, AlignmentResult, AlignmentStatus
from .negotiation import NegotiationEngine, NegotiableParams, NegotiationSession, NegotiationState
from .emergence import EmergenceEngine, ProtocolVariant, PerformanceMetrics, VariantStatus

__version__ = "2.0.0"
__all__ = [
    # Alignment
    "AlignmentEngine",
    "AgentContext",
    "AlignmentResult",
    "AlignmentStatus",
    # Negotiation
    "NegotiationEngine",
    "NegotiableParams",
    "NegotiationSession",
    "NegotiationState",
    # Emergence
    "EmergenceEngine",
    "ProtocolVariant",
    "PerformanceMetrics",
    "VariantStatus",
]


def get_mcp_server():
    """
    Get the MCP server instance.

    This is a lazy import to avoid requiring the 'mcp' package
    unless the server is actually being run.
    """
    from .server import mcp
    return mcp


def run_server(transport: str = "stdio", port: int = 8000):
    """
    Run the XenoComm MCP server.

    Args:
        transport: "stdio" or "streamable-http"
        port: Port for HTTP transport (default 8000)
    """
    from .server import run_server as _run_server
    _run_server(transport=transport, port=port)
