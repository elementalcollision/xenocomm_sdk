#!/usr/bin/env python3
"""
XenoComm Observation Platform Demo

Demonstrates the flow observation system with simulated agent activities.
Shows how to capture, visualize, and monitor agent communication flows.
"""

import time
import random
import threading
from datetime import datetime

from .observation import (
    ObservationManager,
    get_observation_manager,
    reset_observation_manager,
    FlowType,
    EventSeverity,
)
from .alignment import AgentContext
from .instrumented import create_instrumented_system


def simulate_agent_activity(orchestrator, obs, duration: float = 30.0):
    """Simulate various agent activities to generate flow events."""
    print("Starting agent activity simulation...")

    agents = [
        AgentContext(
            agent_id="research_agent",
            capabilities={"search": True, "summarize": True},
            knowledge_domains=["academic", "research", "data_analysis"],
            context_params={"name": "Research Agent"},
        ),
        AgentContext(
            agent_id="coding_agent",
            capabilities={"code_generation": True, "debugging": True},
            knowledge_domains=["python", "javascript", "algorithms"],
            context_params={"name": "Coding Agent"},
        ),
        AgentContext(
            agent_id="data_agent",
            capabilities={"etl": True, "visualization": True},
            knowledge_domains=["databases", "analytics", "ml"],
            context_params={"name": "Data Agent"},
        ),
        AgentContext(
            agent_id="assistant_agent",
            capabilities={"conversation": True, "planning": True},
            knowledge_domains=["general", "task_management"],
            context_params={"name": "Assistant Agent"},
        ),
    ]

    # Register all agents
    for agent in agents:
        orchestrator.register_agent(agent)
        time.sleep(0.3)

    start_time = time.time()
    iteration = 0

    while (time.time() - start_time) < duration:
        iteration += 1

        # Randomly trigger different activities
        activity = random.choice([
            "alignment_check",
            "workflow_start",
            "variant_proposal",
            "collaboration",
        ])

        if activity == "alignment_check":
            agent_a, agent_b = random.sample(agents, 2)
            try:
                orchestrator.run_full_alignment_check(
                    agent_a.agent_id,
                    agent_b.agent_id,
                )
            except Exception:
                pass

        elif activity == "workflow_start":
            wf_types = ["onboarding", "evolution", "recovery", "conflict"]
            wf_type = random.choice(wf_types)

            obs.workflow_sensor.emit(
                f"workflow_{wf_type}_simulated",
                f"Simulated {wf_type} workflow started",
                metrics={"step_count": random.randint(3, 7)},
            )

        elif activity == "variant_proposal":
            variant_id = f"variant_v{random.randint(1, 99)}"
            obs.emergence_sensor.variant_proposed(
                variant_id=variant_id,
                description=f"Protocol optimization {variant_id}",
                change_count=random.randint(1, 5),
            )

            # Sometimes start canary
            if random.random() > 0.5:
                obs.emergence_sensor.canary_started(
                    variant_id=variant_id,
                    percentage=0.1,
                )

        elif activity == "collaboration":
            agent_a, agent_b = random.sample(agents, 2)
            session_id = f"collab_{iteration}"

            obs.collaboration_sensor.session_created(
                session_id=session_id,
                agent_a=agent_a.agent_id,
                agent_b=agent_b.agent_id,
            )

            # Simulate negotiation
            obs.negotiation_sensor.emit(
                "negotiation_round",
                f"Round {random.randint(1, 3)} between {agent_a.agent_id} and {agent_b.agent_id}",
                source_agent=agent_a.agent_id,
                target_agent=agent_b.agent_id,
                session_id=session_id,
            )

        time.sleep(random.uniform(0.5, 1.5))

    print("Simulation complete.")


def run_demo_with_dashboard():
    """Run the demo with the visual dashboard."""
    try:
        from .dashboard import FlowDashboard, RICH_AVAILABLE
    except ImportError:
        RICH_AVAILABLE = False

    if not RICH_AVAILABLE:
        print("Rich library not available. Install with: pip install rich")
        run_demo_text_only()
        return

    # Create instrumented system
    orchestrator, workflow_manager, obs = create_instrumented_system()

    # Create dashboard
    dashboard = FlowDashboard(obs)
    dashboard.update_from_orchestrator(orchestrator)

    # Start simulation in background
    sim_thread = threading.Thread(
        target=simulate_agent_activity,
        args=(orchestrator, obs, 60.0),
        daemon=True,
    )
    sim_thread.start()

    # Run dashboard
    print("\n🌐 Starting XenoComm Flow Observatory...")
    print("   Press Ctrl+C to exit\n")

    try:
        dashboard.run(refresh_rate=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        obs.stop()


def run_demo_text_only():
    """Run the demo with text output only."""
    from .dashboard import SimpleTextDashboard

    # Create instrumented system
    orchestrator, workflow_manager, obs = create_instrumented_system()

    # Create simple dashboard
    dashboard = SimpleTextDashboard(obs)

    # Start simulation in background
    sim_thread = threading.Thread(
        target=simulate_agent_activity,
        args=(orchestrator, obs, 30.0),
        daemon=True,
    )
    sim_thread.start()

    # Run dashboard
    print("\n📡 Starting XenoComm Flow Observatory (Text Mode)...")
    print("   Press Ctrl+C to exit\n")

    try:
        dashboard.run(refresh_rate=1.0)
    except KeyboardInterrupt:
        pass
    finally:
        obs.stop()


def run_demo_headless():
    """Run demo without dashboard, just print events."""
    obs = reset_observation_manager()
    orchestrator, workflow_manager, _ = create_instrumented_system()

    # Subscribe to events
    def print_event(event):
        icon = {
            FlowType.AGENT_LIFECYCLE: "👤",
            FlowType.ALIGNMENT: "🎯",
            FlowType.NEGOTIATION: "🤝",
            FlowType.EMERGENCE: "🧬",
            FlowType.WORKFLOW: "⚙️",
            FlowType.COLLABORATION: "💬",
            FlowType.SYSTEM: "🖥️",
        }.get(event.flow_type, "•")

        time_str = event.timestamp.strftime("%H:%M:%S")
        print(f"{time_str} {icon} [{event.flow_type.value}] {event.summary}")

    obs.event_bus.subscribe("console", print_event)

    print("\n📡 XenoComm Flow Observer (Headless)")
    print("=" * 50)
    print("Streaming events... (Ctrl+C to stop)\n")

    # Run simulation
    try:
        simulate_agent_activity(orchestrator, obs, duration=20.0)
    except KeyboardInterrupt:
        pass
    finally:
        obs.stop()

    # Print final stats
    stats = obs.event_bus.get_stats()
    print("\n" + "=" * 50)
    print("📊 Final Statistics:")
    print(f"   Total Events: {stats['total_events']}")
    print(f"   Events/sec: {stats['events_per_second']:.2f}")
    print(f"   Error Rate: {stats['error_rate']:.1%}")
    print(f"   By Type: {stats['type_counts']}")


def print_demo_help():
    """Print help for demo modes."""
    print("""
XenoComm Observation Platform Demo
==================================

Usage: python -m xenocomm_mcp.demo_observation [mode]

Modes:
  dashboard  - Run with full TUI dashboard (requires 'rich' library)
  text       - Run with simple text dashboard
  headless   - Run without dashboard, just print events
  help       - Show this help

Example:
  python -m xenocomm_mcp.demo_observation dashboard
""")


if __name__ == "__main__":
    import sys

    mode = sys.argv[1] if len(sys.argv) > 1 else "headless"

    if mode == "dashboard":
        run_demo_with_dashboard()
    elif mode == "text":
        run_demo_text_only()
    elif mode == "headless":
        run_demo_headless()
    elif mode == "help":
        print_demo_help()
    else:
        print(f"Unknown mode: {mode}")
        print_demo_help()
