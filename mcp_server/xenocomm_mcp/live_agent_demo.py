#!/usr/bin/env python3
"""
Live Agent Demo for XenoComm Flow Observatory

This script runs actual Claude-style agents that coordinate through XenoComm,
with all activity visible in the Flow Observatory dashboard.

Usage:
    python -m xenocomm_mcp.live_agent_demo

    Or with options:
    python -m xenocomm_mcp.live_agent_demo --mode text --task "Your research task"
"""

import argparse
import asyncio
import random
import time
import threading
from datetime import datetime
from typing import Optional

from .instrumented import create_instrumented_system
from .observation import FlowType, EventSeverity, get_observation_manager
from .claude_bridge import get_claude_bridge, AgentType
from .alignment import AgentContext


class ResearchAgent:
    """A simulated research agent that coordinates through XenoComm."""

    def __init__(self, agent_id: str, specialty: str, bridge, orchestrator):
        self.agent_id = agent_id
        self.specialty = specialty
        self.bridge = bridge
        self.orchestrator = orchestrator
        self.session = None
        self.findings = []

    def connect(self):
        """Register with XenoComm and get a session."""
        self.session = self.bridge.register_agent(
            agent_id=self.agent_id,
            agent_type=AgentType.CUSTOM,  # Research agents use CUSTOM type
            capabilities={
                "research": True,
                "analysis": True,
                "synthesis": True,
                "specialty": self.specialty,
            },
            knowledge_domains=[self.specialty, "research_methods", "data_analysis"],
        )

        # Also register with the orchestrator for alignment
        context = AgentContext(
            agent_id=self.agent_id,
            capabilities={"research": 1.0, "analysis": 0.8},
            knowledge_domains=[self.specialty, "research_methods"],
            goals=[{"type": "research", "description": f"Research {self.specialty}"}],
        )
        self.orchestrator.register_agent(context)
        return self.session

    def send_message(self, to_agent: str, intent: str, content: str):
        """Send a message to another agent."""
        if not self.session:
            return None
        return self.bridge.send_message(
            session_id=self.session.session_id,
            to_agent=to_agent,
            message_type="coordination",
            intent=intent,
            content=content,
        )

    def add_finding(self, finding: str):
        """Record a research finding."""
        self.findings.append({
            "timestamp": datetime.now().isoformat(),
            "content": finding,
        })

    def get_findings(self):
        """Get all findings."""
        return self.findings


class MultiAgentResearchCoordinator:
    """Coordinates multiple research agents on a shared task."""

    def __init__(self, task: str, num_agents: int = 3):
        self.task = task
        self.num_agents = num_agents
        self.agents = []
        self.obs = None
        self.orchestrator = None
        self.bridge = None

        # Specialties for research agents
        self.specialties = [
            "historical_analysis",
            "current_applications",
            "future_trends",
            "technical_implementation",
            "user_impact_studies",
        ]

    def setup(self):
        """Initialize the XenoComm system and create agents."""
        # Create instrumented system (this sets up observation)
        self.orchestrator, workflow_mgr, self.obs = create_instrumented_system()

        # Get the Claude bridge
        self.bridge = get_claude_bridge()

        # Create research agents with different specialties
        for i in range(self.num_agents):
            specialty = self.specialties[i % len(self.specialties)]
            agent = ResearchAgent(
                agent_id=f"research_agent_{i+1}",
                specialty=specialty,
                bridge=self.bridge,
                orchestrator=self.orchestrator,
            )
            self.agents.append(agent)

        return self.obs

    def run_coordination(self, duration_seconds: int = 30):
        """Run the multi-agent coordination for the specified duration."""
        # Connect all agents
        for agent in self.agents:
            agent.connect()
            time.sleep(0.3)  # Stagger connections

        # Emit task start event
        self.obs.collaboration_sensor.emit(
            event_name="task_initiated",
            summary=f"Research task initiated: {self.task[:50]}...",
            severity=EventSeverity.INFO,
        )

        # Run alignment checks between agents
        self._run_alignment_phase()

        # Simulate negotiation on research approach
        self._run_negotiation_phase()

        # Simulate research activity
        self._run_research_phase(duration_seconds)

        # Synthesize findings
        self._run_synthesis_phase()

    def _run_alignment_phase(self):
        """Run alignment verification between agents."""
        self.obs.alignment_sensor.emit(
            event_name="phase_started",
            summary="Starting multi-agent alignment verification",
            severity=EventSeverity.INFO,
        )

        # Check alignment between agent pairs
        for i, agent_a in enumerate(self.agents):
            for agent_b in self.agents[i+1:]:
                # Get AgentContext objects from registry
                ctx_a = self.orchestrator.alignment.registered_agents.get(agent_a.agent_id)
                ctx_b = self.orchestrator.alignment.registered_agents.get(agent_b.agent_id)

                if not ctx_a or not ctx_b:
                    continue

                results = self.orchestrator.alignment.full_alignment_check(
                    ctx_a,
                    ctx_b,
                )

                # Calculate overall score from strategy results
                scores = [r.confidence for r in results.values()]
                overall_score = sum(scores) / len(scores) if scores else 0.5

                status = "aligned" if overall_score > 0.7 else "misaligned"
                self.obs.alignment_sensor.emit(
                    event_name="alignment_check",
                    summary=f"Alignment check: {agent_a.agent_id} <-> {agent_b.agent_id}: {status}",
                    source_agent=agent_a.agent_id,
                    target_agent=agent_b.agent_id,
                    metrics={"score": overall_score},
                    severity=EventSeverity.INFO if overall_score > 0.7 else EventSeverity.WARNING,
                )
                time.sleep(0.2)

    def _run_negotiation_phase(self):
        """Negotiate research approach parameters."""
        self.obs.negotiation_sensor.emit(
            event_name="phase_started",
            summary="Initiating research approach negotiation",
            severity=EventSeverity.INFO,
        )

        # Lead agent initiates negotiation
        lead = self.agents[0]
        follower = self.agents[1] if len(self.agents) > 1 else None

        if follower:
            session = self.orchestrator.negotiation.initiate(
                initiator_id=lead.agent_id,
                responder_id=follower.agent_id,
                proposed_params={
                    "protocol_version": "1.0",
                    "data_format": "json",
                    "compression": "gzip",
                },
            )

            self.obs.negotiation_sensor.emit(
                event_name="session_started",
                summary=f"Negotiation session started: {session.session_id[:8]}",
                session_id=session.session_id,
                source_agent=lead.agent_id,
                target_agent=follower.agent_id,
                severity=EventSeverity.INFO,
            )

            # Responder receives proposal
            time.sleep(0.2)
            self.orchestrator.negotiation.receive_proposal(
                session.session_id,
                follower.agent_id,
            )

            # Simulate acceptance
            time.sleep(0.2)
            self.orchestrator.negotiation.respond(
                session.session_id,
                follower.agent_id,
                "accept",
            )

            self.obs.negotiation_sensor.emit(
                event_name="agreement_reached",
                summary="Research parameters agreed upon",
                session_id=session.session_id,
                severity=EventSeverity.INFO,
            )

    def _run_research_phase(self, duration: int):
        """Simulate active research with inter-agent communication."""
        self.obs.workflow_sensor.emit(
            event_name="research_phase_started",
            summary="Research phase started",
            metrics={"duration": duration},
            severity=EventSeverity.INFO,
        )

        # Research topics based on the task
        research_actions = [
            ("query", "Searching historical databases"),
            ("analyze", "Analyzing temporal patterns"),
            ("compare", "Comparing approaches across eras"),
            ("synthesize", "Synthesizing cross-domain insights"),
            ("validate", "Validating findings against sources"),
            ("clarify", "Requesting clarification on methodology"),
            ("confirm", "Confirming shared understanding"),
            ("propose", "Proposing new research direction"),
        ]

        start_time = time.time()
        iteration = 0

        while time.time() - start_time < duration:
            iteration += 1

            # Pick a random agent to act
            agent = random.choice(self.agents)
            action, description = random.choice(research_actions)

            # Send message to another agent
            other_agents = [a for a in self.agents if a != agent]
            if other_agents:
                target = random.choice(other_agents)

                msg = agent.send_message(
                    to_agent=target.agent_id,
                    intent=action,
                    content=f"{description} for {agent.specialty}",
                )

                if msg:
                    self.obs.collaboration_sensor.emit(
                        event_name="message_sent",
                        summary=f"{agent.agent_id} -> {target.agent_id}: {action}",
                        source_agent=agent.agent_id,
                        target_agent=target.agent_id,
                        severity=EventSeverity.DEBUG,
                    )

            # Occasionally generate a finding
            if random.random() < 0.3:
                finding = f"Finding #{iteration}: {description} revealed insights in {agent.specialty}"
                agent.add_finding(finding)

                self.obs.workflow_sensor.emit(
                    event_name="finding_recorded",
                    summary=f"Research finding recorded by {agent.agent_id}",
                    source_agent=agent.agent_id,
                    metrics={"finding_count": len(agent.findings)},
                    severity=EventSeverity.INFO,
                )

            # Check for pattern emergence
            patterns = self.bridge.language_engine.get_patterns()
            if patterns and random.random() < 0.2:
                pattern = random.choice(patterns)
                self.obs.emergence_sensor.emit(
                    event_name="pattern_detected",
                    summary=f"Communication pattern detected: {pattern.name}",
                    metrics={
                        "occurrences": pattern.occurrences,
                        "success_rate": pattern.success_rate,
                    },
                    severity=EventSeverity.INFO,
                )

            time.sleep(random.uniform(0.3, 0.8))

    def _run_synthesis_phase(self):
        """Synthesize findings from all agents."""
        self.obs.workflow_sensor.emit(
            event_name="synthesis_phase_started",
            summary="Synthesis phase started",
            severity=EventSeverity.INFO,
        )

        total_findings = sum(len(a.findings) for a in self.agents)

        # Mark collaboration outcomes
        for agent in self.agents:
            self.bridge.mark_collaboration_outcome(
                session_id=agent.session.session_id,
                success=True,
                notes=f"Completed with {len(agent.findings)} findings",
            )

        self.obs.collaboration_sensor.emit(
            event_name="research_complete",
            summary=f"Research complete: {total_findings} findings across {len(self.agents)} agents",
            metrics={"total_findings": total_findings, "agent_count": len(self.agents)},
            severity=EventSeverity.INFO,
        )

        # Get final patterns
        patterns = self.bridge.language_engine.get_patterns()
        if patterns:
            self.obs.emergence_sensor.emit(
                event_name="patterns_finalized",
                summary=f"Final language patterns: {len(patterns)} emerged",
                metrics={"pattern_count": len(patterns)},
                severity=EventSeverity.INFO,
            )

    def get_summary(self) -> dict:
        """Get a summary of the research coordination."""
        return {
            "task": self.task,
            "agents": len(self.agents),
            "total_findings": sum(len(a.findings) for a in self.agents),
            "findings_by_agent": {
                a.agent_id: {
                    "specialty": a.specialty,
                    "findings": a.findings,
                }
                for a in self.agents
            },
            "patterns": [
                {"name": p.name, "occurrences": p.occurrences}
                for p in self.bridge.language_engine.get_patterns()
            ] if self.bridge else [],
        }


def run_with_dashboard(task: str, num_agents: int = 3, mode: str = "text",
                       duration: int = 30, refresh: float = 0.5):
    """
    Run the multi-agent research with a live dashboard.

    Args:
        task: The research task for agents to work on
        num_agents: Number of research agents to spawn
        mode: Dashboard mode (text, rich, headless)
        duration: How long to run the research phase (seconds)
        refresh: Dashboard refresh rate
    """
    # Create coordinator
    coordinator = MultiAgentResearchCoordinator(task, num_agents)
    obs = coordinator.setup()

    # Start dashboard in background thread
    dashboard_thread = None
    dashboard = None

    if mode == "text":
        from .dashboard import SimpleTextDashboard
        dashboard = SimpleTextDashboard(obs)

        def run_dashboard():
            dashboard.run(refresh_rate=refresh)

        dashboard_thread = threading.Thread(target=run_dashboard, daemon=True)

    elif mode == "rich" or mode == "dashboard":
        try:
            from .dashboard import FlowDashboard, RICH_AVAILABLE
            if RICH_AVAILABLE:
                dashboard = FlowDashboard(obs)
                dashboard.update_from_orchestrator(coordinator.orchestrator)

                def run_dashboard():
                    dashboard.run(refresh_rate=refresh)

                dashboard_thread = threading.Thread(target=run_dashboard, daemon=True)
            else:
                print("Rich not available, falling back to text mode")
                mode = "text"
                from .dashboard import SimpleTextDashboard
                dashboard = SimpleTextDashboard(obs)

                def run_dashboard():
                    dashboard.run(refresh_rate=refresh)

                dashboard_thread = threading.Thread(target=run_dashboard, daemon=True)
        except ImportError:
            print("Dashboard module issue, falling back to text mode")
            mode = "text"

    # For headless mode, just print events
    if mode == "headless":
        def print_event(event):
            icons = {
                FlowType.AGENT_LIFECYCLE: "👤",
                FlowType.ALIGNMENT: "🎯",
                FlowType.NEGOTIATION: "🤝",
                FlowType.EMERGENCE: "🧬",
                FlowType.WORKFLOW: "⚙️",
                FlowType.COLLABORATION: "💬",
                FlowType.SYSTEM: "🖥️",
            }
            icon = icons.get(event.flow_type, "•")
            time_str = event.timestamp.strftime("%H:%M:%S")
            print(f"{time_str} {icon} [{event.flow_type.value}] {event.summary}")

        obs.event_bus.subscribe("console", print_event)

    print(f"\n{'='*60}")
    print("XenoComm Multi-Agent Research Coordinator")
    print(f"{'='*60}")
    print(f"Task: {task[:70]}...")
    print(f"Agents: {num_agents}")
    print(f"Mode: {mode}")
    print(f"Duration: {duration}s")
    print(f"{'='*60}\n")

    # Start dashboard
    if dashboard_thread:
        dashboard_thread.start()
        time.sleep(0.5)  # Let dashboard initialize

    # Run the coordination
    try:
        coordinator.run_coordination(duration_seconds=duration)
    except KeyboardInterrupt:
        print("\nInterrupted by user")

    # Print summary
    summary = coordinator.get_summary()
    print(f"\n{'='*60}")
    print("Research Complete!")
    print(f"{'='*60}")
    print(f"Total Findings: {summary['total_findings']}")
    print(f"Patterns Emerged: {len(summary['patterns'])}")
    for pattern in summary['patterns']:
        print(f"  - {pattern['name']} (occurrences: {pattern['occurrences']})")
    print()

    # Keep dashboard running a bit longer so user can see final state
    if mode != "headless":
        print("Dashboard will continue for 10 more seconds...")
        time.sleep(10)

    obs.stop()
    return summary


def main():
    """Main entry point for the live agent demo."""
    parser = argparse.ArgumentParser(
        description="Run multi-agent research with live Flow Observatory"
    )
    parser.add_argument(
        "-t", "--task",
        default="Evaluate and compare how artificial intelligence has been used over the last 30 years in assistive technology",
        help="Research task for agents to work on",
    )
    parser.add_argument(
        "-n", "--num-agents",
        type=int,
        default=3,
        help="Number of research agents (default: 3)",
    )
    parser.add_argument(
        "-m", "--mode",
        choices=["text", "rich", "dashboard", "headless"],
        default="text",
        help="Dashboard mode (default: text)",
    )
    parser.add_argument(
        "-d", "--duration",
        type=int,
        default=30,
        help="Research phase duration in seconds (default: 30)",
    )
    parser.add_argument(
        "-r", "--refresh",
        type=float,
        default=0.5,
        help="Dashboard refresh rate (default: 0.5)",
    )

    args = parser.parse_args()

    run_with_dashboard(
        task=args.task,
        num_agents=args.num_agents,
        mode=args.mode,
        duration=args.duration,
        refresh=args.refresh,
    )


if __name__ == "__main__":
    main()
