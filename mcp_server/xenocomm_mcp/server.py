"""
XenoComm MCP Server
===================

The main MCP server that exposes XenoComm's alignment, negotiation, and
emergence capabilities as MCP tools.

Usage:
    # As a module
    python -m xenocomm_mcp

    # Or via the run_server function
    from xenocomm_mcp import run_server
    run_server(transport="stdio")  # or "streamable-http"
"""

from typing import Any
from mcp.server.fastmcp import FastMCP

from .alignment import AlignmentEngine, AgentContext, AlignmentResult
from .negotiation import NegotiationEngine, NegotiableParams, NegotiationState
from .emergence import EmergenceEngine, PerformanceMetrics, VariantStatus


# Initialize the MCP server
mcp = FastMCP(
    "XenoComm",
    json_response=True,
)

# Initialize engines (shared state across tool calls)
alignment_engine = AlignmentEngine()
negotiation_engine = NegotiationEngine()
emergence_engine = EmergenceEngine()


# =============================================================================
# ALIGNMENT TOOLS
# =============================================================================

@mcp.tool()
def register_agent(
    agent_id: str,
    capabilities: dict[str, Any] | None = None,
    knowledge_domains: list[str] | None = None,
    goals: list[dict[str, Any]] | None = None,
    terminology: dict[str, str] | None = None,
    assumptions: list[str] | None = None,
    context_params: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """
    Register an agent's context for alignment verification.

    This should be called before any alignment checks to establish
    what an agent knows, wants, and assumes.

    Args:
        agent_id: Unique identifier for the agent
        capabilities: Dict of capability names to values/descriptions
        knowledge_domains: List of domains the agent has knowledge in
        goals: List of goal objects with 'type' and 'description' fields
        terminology: Dict mapping terms to their definitions
        assumptions: List of assumptions the agent makes
        context_params: Dict of contextual parameters (environment, constraints)

    Returns:
        Confirmation of registration with agent summary
    """
    context = AgentContext(
        agent_id=agent_id,
        capabilities=capabilities or {},
        knowledge_domains=knowledge_domains or [],
        goals=goals or [],
        terminology=terminology or {},
        assumptions=assumptions or [],
        context_params=context_params or {},
    )

    alignment_engine.register_agent(context)

    return {
        "status": "registered",
        "agent_id": agent_id,
        "summary": {
            "capabilities_count": len(context.capabilities),
            "knowledge_domains": context.knowledge_domains,
            "goals_count": len(context.goals),
            "terminology_count": len(context.terminology),
            "assumptions_count": len(context.assumptions),
        },
    }


@mcp.tool()
def verify_knowledge_alignment(
    agent_a_id: str,
    agent_b_id: str,
    required_domains: list[str] | None = None,
) -> dict[str, Any]:
    """
    Verify shared knowledge between two agents.

    Checks if agents have overlapping knowledge domains and can
    communicate about required topics effectively.

    Args:
        agent_a_id: ID of the first agent (must be registered)
        agent_b_id: ID of the second agent (must be registered)
        required_domains: Optional list of domains that must be shared

    Returns:
        Alignment result with status, confidence, details, and recommendations
    """
    agent_a = alignment_engine.registered_agents.get(agent_a_id)
    agent_b = alignment_engine.registered_agents.get(agent_b_id)

    if not agent_a:
        return {"error": f"Agent {agent_a_id} not registered"}
    if not agent_b:
        return {"error": f"Agent {agent_b_id} not registered"}

    result = alignment_engine.verify_knowledge(agent_a, agent_b, required_domains)
    return result.to_dict()


@mcp.tool()
def verify_goal_alignment(
    agent_a_id: str,
    agent_b_id: str,
) -> dict[str, Any]:
    """
    Verify goal compatibility between two agents.

    Checks if agents' goals can coexist or if there are conflicts
    that need resolution before collaboration.

    Args:
        agent_a_id: ID of the first agent
        agent_b_id: ID of the second agent

    Returns:
        Alignment result with conflicts, alignments, and recommendations
    """
    agent_a = alignment_engine.registered_agents.get(agent_a_id)
    agent_b = alignment_engine.registered_agents.get(agent_b_id)

    if not agent_a:
        return {"error": f"Agent {agent_a_id} not registered"}
    if not agent_b:
        return {"error": f"Agent {agent_b_id} not registered"}

    result = alignment_engine.verify_goals(agent_a, agent_b)
    return result.to_dict()


@mcp.tool()
def align_terminology(
    agent_a_id: str,
    agent_b_id: str,
) -> dict[str, Any]:
    """
    Ensure consistent terminology between two agents.

    Creates mappings between terms used by different agents to ensure
    they understand each other correctly. Identifies conflicts and
    suggests resolutions.

    Args:
        agent_a_id: ID of the first agent
        agent_b_id: ID of the second agent

    Returns:
        Alignment result with shared terms, conflicts, and suggested mappings
    """
    agent_a = alignment_engine.registered_agents.get(agent_a_id)
    agent_b = alignment_engine.registered_agents.get(agent_b_id)

    if not agent_a:
        return {"error": f"Agent {agent_a_id} not registered"}
    if not agent_b:
        return {"error": f"Agent {agent_b_id} not registered"}

    result = alignment_engine.align_terminology(agent_a, agent_b)
    return result.to_dict()


@mcp.tool()
def verify_assumptions(
    agent_a_id: str,
    agent_b_id: str,
) -> dict[str, Any]:
    """
    Surface and validate assumptions between agents.

    Identifies assumptions that one agent makes which the other
    may not share, potentially leading to miscommunication.

    Args:
        agent_a_id: ID of the first agent
        agent_b_id: ID of the second agent

    Returns:
        Alignment result with shared/unique assumptions and conflicts
    """
    agent_a = alignment_engine.registered_agents.get(agent_a_id)
    agent_b = alignment_engine.registered_agents.get(agent_b_id)

    if not agent_a:
        return {"error": f"Agent {agent_a_id} not registered"}
    if not agent_b:
        return {"error": f"Agent {agent_b_id} not registered"}

    result = alignment_engine.verify_assumptions(agent_a, agent_b)
    return result.to_dict()


@mcp.tool()
def sync_context(
    agent_a_id: str,
    agent_b_id: str,
    required_params: list[str] | None = None,
) -> dict[str, Any]:
    """
    Align contextual parameters between two agents.

    Ensures both agents have compatible context for the interaction,
    including environment settings, constraints, and preferences.

    Args:
        agent_a_id: ID of the first agent
        agent_b_id: ID of the second agent
        required_params: Optional list of parameters that must be present

    Returns:
        Alignment result with matched/mismatched params and recommendations
    """
    agent_a = alignment_engine.registered_agents.get(agent_a_id)
    agent_b = alignment_engine.registered_agents.get(agent_b_id)

    if not agent_a:
        return {"error": f"Agent {agent_a_id} not registered"}
    if not agent_b:
        return {"error": f"Agent {agent_b_id} not registered"}

    result = alignment_engine.sync_context(agent_a, agent_b, required_params)
    return result.to_dict()


@mcp.tool()
def full_alignment_check(
    agent_a_id: str,
    agent_b_id: str,
    required_domains: list[str] | None = None,
    required_params: list[str] | None = None,
) -> dict[str, Any]:
    """
    Run all alignment strategies for comprehensive verification.

    This is the recommended tool for establishing whether two agents
    can effectively collaborate. It runs all five alignment strategies:
    knowledge, goals, terminology, assumptions, and context.

    Args:
        agent_a_id: ID of the first agent
        agent_b_id: ID of the second agent
        required_domains: Optional knowledge domains that must be shared
        required_params: Optional context parameters that must be present

    Returns:
        Comprehensive alignment report with all strategy results
    """
    agent_a = alignment_engine.registered_agents.get(agent_a_id)
    agent_b = alignment_engine.registered_agents.get(agent_b_id)

    if not agent_a:
        return {"error": f"Agent {agent_a_id} not registered"}
    if not agent_b:
        return {"error": f"Agent {agent_b_id} not registered"}

    results = alignment_engine.full_alignment_check(
        agent_a, agent_b, required_domains, required_params
    )

    # Calculate overall alignment score
    statuses = [r.status.value for r in results.values()]
    aligned_count = statuses.count("aligned")
    partial_count = statuses.count("partial")

    if aligned_count >= 4:
        overall_status = "aligned"
    elif aligned_count + partial_count >= 3:
        overall_status = "partial"
    else:
        overall_status = "misaligned"

    avg_confidence = sum(r.confidence for r in results.values()) / len(results)

    return {
        "overall_status": overall_status,
        "overall_confidence": avg_confidence,
        "strategy_results": {
            name: result.to_dict()
            for name, result in results.items()
        },
        "summary": {
            "aligned_strategies": aligned_count,
            "partial_strategies": partial_count,
            "misaligned_strategies": len(results) - aligned_count - partial_count,
        },
    }


# =============================================================================
# NEGOTIATION TOOLS
# =============================================================================

@mcp.tool()
def initiate_negotiation(
    initiator_id: str,
    responder_id: str,
    proposed_params: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """
    Initiate a protocol negotiation session with another agent.

    Starts the negotiation state machine by proposing communication
    parameters to the responder agent.

    Args:
        initiator_id: ID of the agent initiating the negotiation
        responder_id: ID of the target agent
        proposed_params: Optional dict of proposed parameters:
            - protocol_version: Version string (default "1.0")
            - data_format: "json", "msgpack", "protobuf", etc.
            - compression: "gzip", "lz4", "zstd", or None
            - error_correction: "none", "checksum", "reed_solomon"
            - max_message_size: Max bytes per message
            - timeout_ms: Operation timeout
            - encryption: "none", "tls", "aes256"

    Returns:
        Session details including session_id and current state
    """
    params = NegotiableParams.from_dict(proposed_params or {})
    session = negotiation_engine.initiate_session(initiator_id, responder_id, params)
    return session.to_dict()


@mcp.tool()
def respond_to_negotiation(
    session_id: str,
    responder_id: str,
    response: str,
    counter_params: dict[str, Any] | None = None,
    reason: str | None = None,
) -> dict[str, Any]:
    """
    Respond to a negotiation proposal.

    The responder can accept, counter, or reject the proposal.

    Args:
        session_id: The negotiation session ID
        responder_id: ID of the responding agent
        response: One of "accept", "counter", or "reject"
        counter_params: Required if response is "counter"
        reason: Optional reason for rejection

    Returns:
        Updated session state
    """
    if response == "accept":
        session = negotiation_engine.respond_accept(session_id, responder_id)
    elif response == "counter":
        if not counter_params:
            return {"error": "counter_params required for counter response"}
        session = negotiation_engine.respond_counter(session_id, responder_id, counter_params)
    elif response == "reject":
        session = negotiation_engine.respond_reject(session_id, responder_id, reason)
    else:
        return {"error": f"Invalid response: {response}. Use 'accept', 'counter', or 'reject'"}

    return session.to_dict()


@mcp.tool()
def accept_counter_proposal(
    session_id: str,
    initiator_id: str,
) -> dict[str, Any]:
    """
    Accept a counter-proposal from the responder.

    Called by the initiator when they receive a counter-proposal
    and want to accept it.

    Args:
        session_id: The negotiation session ID
        initiator_id: ID of the initiating agent

    Returns:
        Updated session state
    """
    session = negotiation_engine.accept_counter(session_id, initiator_id)
    return session.to_dict()


@mcp.tool()
def finalize_negotiation(
    session_id: str,
    initiator_id: str,
) -> dict[str, Any]:
    """
    Finalize the negotiation and lock in agreed parameters.

    This completes the negotiation state machine and returns
    the final agreed-upon parameters.

    Args:
        session_id: The negotiation session ID
        initiator_id: ID of the initiating agent

    Returns:
        Finalized session with agreed parameters
    """
    session = negotiation_engine.finalize_session(session_id, initiator_id)
    return session.to_dict()


@mcp.tool()
def get_negotiation_status(
    session_id: str,
) -> dict[str, Any]:
    """
    Get the current status of a negotiation session.

    Args:
        session_id: The negotiation session ID

    Returns:
        Current session state and parameters
    """
    session = negotiation_engine.get_session_status(session_id)
    return session.to_dict()


@mcp.tool()
def list_negotiations(
    agent_id: str | None = None,
    state: str | None = None,
) -> dict[str, Any]:
    """
    List negotiation sessions.

    Args:
        agent_id: Optional filter by agent (as initiator or responder)
        state: Optional filter by state (e.g., "finalized", "awaiting_response")

    Returns:
        List of matching sessions
    """
    state_enum = NegotiationState(state) if state else None
    sessions = negotiation_engine.list_sessions(agent_id, state_enum)
    return {
        "sessions": [s.to_dict() for s in sessions],
        "total": len(sessions),
    }


# =============================================================================
# EMERGENCE TOOLS
# =============================================================================

@mcp.tool()
def propose_protocol_variant(
    description: str,
    changes: dict[str, Any],
) -> dict[str, Any]:
    """
    Propose a new protocol variant for evolution.

    This starts the variant through the emergence pipeline:
    proposed -> testing -> canary -> active.

    Args:
        description: Human-readable description of the variant
        changes: Dict of protocol changes being proposed

    Returns:
        Created variant with ID and status
    """
    variant = emergence_engine.propose_variant(description, changes)
    return variant.to_dict()


@mcp.tool()
def start_variant_testing(
    variant_id: str,
) -> dict[str, Any]:
    """
    Move a variant from proposed to testing status.

    Args:
        variant_id: ID of the variant to test

    Returns:
        Updated variant status
    """
    variant = emergence_engine.start_testing(variant_id)
    return variant.to_dict()


@mcp.tool()
def start_canary_deployment(
    variant_id: str,
    initial_percentage: float | None = None,
) -> dict[str, Any]:
    """
    Start canary deployment for a variant.

    Routes a small percentage of traffic to the new variant while
    monitoring for issues.

    Args:
        variant_id: ID of the variant to deploy
        initial_percentage: Initial traffic percentage (default 0.1 = 10%)

    Returns:
        Updated variant with canary status
    """
    variant = emergence_engine.start_canary(variant_id, initial_percentage)
    return variant.to_dict()


@mcp.tool()
def ramp_canary(
    variant_id: str,
) -> dict[str, Any]:
    """
    Increase canary deployment percentage.

    Ramps up traffic to the variant by one step. When percentage
    reaches 100%, the variant becomes active.

    Args:
        variant_id: ID of the variant

    Returns:
        Updated variant with new percentage
    """
    variant = emergence_engine.ramp_canary(variant_id)
    return variant.to_dict()


@mcp.tool()
def track_variant_performance(
    variant_id: str,
    success_rate: float,
    latency_ms: float,
    throughput: float = 0.0,
    error_count: int = 0,
    total_requests: int = 0,
) -> dict[str, Any]:
    """
    Record performance metrics for a protocol variant.

    This data is used to determine if the variant should be
    rolled back or promoted.

    Args:
        variant_id: ID of the variant
        success_rate: Success rate from 0.0 to 1.0
        latency_ms: Average latency in milliseconds
        throughput: Operations per second
        error_count: Number of errors
        total_requests: Total number of requests

    Returns:
        Updated variant with metrics and rollback recommendation
    """
    metrics = PerformanceMetrics(
        success_rate=success_rate,
        latency_ms=latency_ms,
        throughput=throughput,
        error_count=error_count,
        total_requests=total_requests,
    )

    variant = emergence_engine.track_performance(variant_id, metrics)
    should_rollback = emergence_engine.should_rollback(variant_id)

    return {
        "variant": variant.to_dict(),
        "should_rollback": should_rollback,
        "metrics_recorded": metrics.to_dict(),
    }


@mcp.tool()
def get_variant_status(
    variant_id: str,
) -> dict[str, Any]:
    """
    Get comprehensive status for a protocol variant.

    Includes variant details, circuit breaker state, and rollback
    recommendations.

    Args:
        variant_id: ID of the variant

    Returns:
        Complete variant status
    """
    return emergence_engine.get_variant_status(variant_id)


@mcp.tool()
def rollback_variant(
    variant_id: str,
) -> dict[str, Any]:
    """
    Roll back a variant to the last stable state.

    Used when a variant is causing issues and needs to be reverted.

    Args:
        variant_id: ID of the variant to roll back

    Returns:
        Rollback result with point used (if any)
    """
    point = emergence_engine.rollback(variant_id)
    variant = emergence_engine.variants[variant_id]

    return {
        "status": "rolled_back",
        "variant": variant.to_dict(),
        "rollback_point": {
            "point_id": point.point_id,
            "created_at": point.created_at.isoformat(),
            "state_snapshot": point.state_snapshot,
        } if point else None,
    }


@mcp.tool()
def list_variants(
    status: str | None = None,
) -> dict[str, Any]:
    """
    List all protocol variants.

    Args:
        status: Optional filter by status (proposed, testing, canary, active, etc.)

    Returns:
        List of variants
    """
    status_enum = VariantStatus(status) if status else None
    variants = emergence_engine.list_variants(status_enum)

    return {
        "variants": [v.to_dict() for v in variants],
        "total": len(variants),
    }


@mcp.tool()
def get_canary_status() -> dict[str, Any]:
    """
    Get status of all canary deployments.

    Returns:
        Active canaries and current active variant
    """
    return emergence_engine.get_canary_status()


# =============================================================================
# SERVER ENTRY POINT
# =============================================================================

def run_server(transport: str = "stdio", port: int = 8000):
    """
    Run the XenoComm MCP server.

    Args:
        transport: "stdio" or "streamable-http"
        port: Port for HTTP transport (default 8000)
    """
    if transport == "streamable-http":
        mcp.run(transport="streamable-http", port=port)
    else:
        mcp.run()


if __name__ == "__main__":
    run_server()
