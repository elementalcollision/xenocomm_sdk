"""
XenoComm MCP Server
===================

An MCP (Model Context Protocol) server that brings sophisticated agent-to-agent
coordination capabilities to the agentic AI ecosystem.

This server exposes XenoComm's alignment, negotiation, and emergence capabilities
as MCP tools that can be used by any MCP-compatible client (Claude Code, Moltbot,
Cursor, etc.).

Key Features (v2.0):
- Alignment verification with weighted strategy scoring
- Protocol negotiation with analytics and auto-resolution
- Emergence/evolution with A/B testing and learning
- Orchestration layer for coordinated workflows
- Pre-built workflows for common scenarios

Usage:
    # Run as stdio server (for Claude Code, etc.)
    python -m xenocomm_mcp

    # Or run as HTTP server
    python -m xenocomm_mcp --transport http --port 8000
"""

# Core engines (no external dependencies)
from .alignment import (
    AlignmentEngine,
    AgentContext,
    AlignmentResult,
    AlignmentStatus,
    StrategyWeight,
)
from .negotiation import (
    NegotiationEngine,
    NegotiableParams,
    NegotiationSession,
    NegotiationState,
    NegotiationConfig,
    NegotiationAnalytics,
)
from .emergence import (
    EmergenceEngine,
    EmergenceConfig,
    ProtocolVariant,
    PerformanceMetrics,
    VariantStatus,
    MetricTrend,
    ABTestExperiment,
)
from .orchestrator import (
    XenoCommOrchestrator,
    OrchestratorConfig,
    CollaborationSession,
)
from .workflows import (
    WorkflowManager,
    WorkflowExecution,
    WorkflowStatus,
)
from .observation import (
    ObservationManager,
    EventBus,
    FlowEvent,
    FlowType,
    EventSeverity,
    FlowSensor,
    AgentSensor,
    AlignmentSensor,
    NegotiationSensor,
    EmergenceSensor,
    WorkflowSensor,
    CollaborationSensor,
    get_observation_manager,
)
from .instrumented import (
    InstrumentedOrchestrator,
    InstrumentedNegotiationEngine,
    InstrumentedEmergenceEngine,
    InstrumentedWorkflowManager,
    create_instrumented_system,
)
from .analytics import (
    EventPersistence,
    FlowAnalytics,
    FlowMetrics,
    AgentMetrics,
    TimeWindow,
    AlertingSystem,
    Alert,
    AlertSeverity,
    EnhancedObservationManager,
)

__version__ = "2.2.0"
__all__ = [
    # Alignment
    "AlignmentEngine",
    "AgentContext",
    "AlignmentResult",
    "AlignmentStatus",
    "StrategyWeight",
    # Negotiation
    "NegotiationEngine",
    "NegotiableParams",
    "NegotiationSession",
    "NegotiationState",
    "NegotiationConfig",
    "NegotiationAnalytics",
    # Emergence
    "EmergenceEngine",
    "EmergenceConfig",
    "ProtocolVariant",
    "PerformanceMetrics",
    "VariantStatus",
    "MetricTrend",
    "ABTestExperiment",
    # Orchestration
    "XenoCommOrchestrator",
    "OrchestratorConfig",
    "CollaborationSession",
    # Workflows
    "WorkflowManager",
    "WorkflowExecution",
    "WorkflowStatus",
    # Observation
    "ObservationManager",
    "EventBus",
    "FlowEvent",
    "FlowType",
    "EventSeverity",
    "FlowSensor",
    "AgentSensor",
    "AlignmentSensor",
    "NegotiationSensor",
    "EmergenceSensor",
    "WorkflowSensor",
    "CollaborationSensor",
    "get_observation_manager",
    # Instrumented
    "InstrumentedOrchestrator",
    "InstrumentedNegotiationEngine",
    "InstrumentedEmergenceEngine",
    "InstrumentedWorkflowManager",
    "create_instrumented_system",
    # Analytics
    "EventPersistence",
    "FlowAnalytics",
    "FlowMetrics",
    "AgentMetrics",
    "TimeWindow",
    "AlertingSystem",
    "Alert",
    "AlertSeverity",
    "EnhancedObservationManager",
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
