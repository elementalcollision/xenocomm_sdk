"""
XenoComm Flow Dashboard

Terminal-based dashboard for observing agent communication flows.
Uses the Rich library for beautiful terminal rendering.
"""

from __future__ import annotations

import time
import threading
from datetime import datetime
from typing import Any
from collections import deque

try:
    from rich.console import Console, Group
    from rich.live import Live
    from rich.table import Table
    from rich.panel import Panel
    from rich.layout import Layout
    from rich.text import Text
    from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn
    from rich.style import Style
    from rich import box
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

from .observation import (
    ObservationManager,
    FlowEvent,
    FlowType,
    EventSeverity,
    get_observation_manager,
)


# ==================== Color Schemes ====================

FLOW_COLORS = {
    FlowType.AGENT_LIFECYCLE: "cyan",
    FlowType.ALIGNMENT: "green",
    FlowType.NEGOTIATION: "yellow",
    FlowType.EMERGENCE: "magenta",
    FlowType.WORKFLOW: "blue",
    FlowType.COLLABORATION: "bright_cyan",
    FlowType.SYSTEM: "white",
}

SEVERITY_COLORS = {
    EventSeverity.DEBUG: "dim",
    EventSeverity.INFO: "white",
    EventSeverity.WARNING: "yellow",
    EventSeverity.ERROR: "red",
    EventSeverity.CRITICAL: "bold red",
}

FLOW_ICONS = {
    FlowType.AGENT_LIFECYCLE: "👤",
    FlowType.ALIGNMENT: "🎯",
    FlowType.NEGOTIATION: "🤝",
    FlowType.EMERGENCE: "🧬",
    FlowType.WORKFLOW: "⚙️",
    FlowType.COLLABORATION: "💬",
    FlowType.SYSTEM: "🖥️",
}


# ==================== Dashboard Panels ====================

class DashboardPanel:
    """Base class for dashboard panels."""

    def __init__(self, title: str, observation_manager: ObservationManager):
        self.title = title
        self.obs = observation_manager

    def render(self) -> Panel:
        """Render the panel. Override in subclasses."""
        return Panel("Empty panel", title=self.title)


class EventFeedPanel(DashboardPanel):
    """Real-time event feed panel."""

    def __init__(self, observation_manager: ObservationManager, max_events: int = 12):
        super().__init__("📡 Live Event Feed", observation_manager)
        self.max_events = max_events

    def render(self) -> Panel:
        events = self.obs.event_bus.get_recent_events(self.max_events)

        if not events:
            content = Text("Waiting for events...", style="dim italic")
        else:
            lines = []
            for event in reversed(events):
                icon = FLOW_ICONS.get(event.flow_type, "•")
                color = FLOW_COLORS.get(event.flow_type, "white")
                sev_color = SEVERITY_COLORS.get(event.severity, "white")

                time_str = event.timestamp.strftime("%H:%M:%S")
                line = Text()
                line.append(f"{time_str} ", style="dim")
                line.append(f"{icon} ", style=color)
                line.append(f"{event.summary[:50]}", style=sev_color)
                if len(event.summary) > 50:
                    line.append("...", style="dim")
                lines.append(line)

            content = Group(*lines)

        return Panel(
            content,
            title=self.title,
            border_style="blue",
            box=box.ROUNDED,
        )


class AgentStatusPanel(DashboardPanel):
    """Panel showing active agents."""

    def __init__(self, observation_manager: ObservationManager):
        super().__init__("👥 Agents", observation_manager)
        self._agents: dict[str, dict] = {}

    def update_agents(self, agents: dict[str, Any]) -> None:
        """Update agent data from orchestrator."""
        self._agents = agents

    def render(self) -> Panel:
        table = Table(show_header=True, header_style="bold cyan", box=box.SIMPLE)
        table.add_column("Agent", style="cyan")
        table.add_column("Status", justify="center")
        table.add_column("Domains")

        if self._agents:
            for agent_id, info in list(self._agents.items())[:8]:
                domains = ", ".join(info.get("domains", [])[:2])
                if len(info.get("domains", [])) > 2:
                    domains += "..."
                table.add_row(
                    agent_id[:15],
                    "🟢 Active",
                    domains[:20]
                )
        else:
            table.add_row("No agents", "-", "-")

        return Panel(
            table,
            title=self.title,
            border_style="cyan",
            box=box.ROUNDED,
        )


class NegotiationFlowPanel(DashboardPanel):
    """Panel showing negotiation flows."""

    def __init__(self, observation_manager: ObservationManager):
        super().__init__("🤝 Negotiations", observation_manager)
        self._negotiations: list[dict] = []

    def update_negotiations(self, negotiations: list[dict]) -> None:
        """Update negotiation data."""
        self._negotiations = negotiations

    def render(self) -> Panel:
        events = self.obs.event_bus.get_recent_events(20, FlowType.NEGOTIATION)

        lines = []
        active_sessions = set()

        for event in reversed(events[-6:]):
            if event.session_id:
                active_sessions.add(event.session_id[:8])

            icon = "📤" if "proposal" in event.event_name else "📥"
            if "complete" in event.event_name:
                icon = "✅"
            elif "counter" in event.event_name:
                icon = "🔄"

            line = Text()
            line.append(f"{icon} ", style="yellow")
            line.append(event.summary[:40], style="white")
            lines.append(line)

        if not lines:
            lines.append(Text("No active negotiations", style="dim"))

        # Add session count
        header = Text()
        header.append(f"Active Sessions: {len(active_sessions)}", style="bold yellow")
        lines.insert(0, header)
        lines.insert(1, Text("─" * 30, style="dim"))

        return Panel(
            Group(*lines),
            title=self.title,
            border_style="yellow",
            box=box.ROUNDED,
        )


class EmergencePanel(DashboardPanel):
    """Panel showing protocol emergence status."""

    def __init__(self, observation_manager: ObservationManager):
        super().__init__("🧬 Protocol Evolution", observation_manager)
        self._variants: list[dict] = []
        self._experiments: list[dict] = []

    def update_emergence(self, variants: list[dict], experiments: list[dict]) -> None:
        """Update emergence data."""
        self._variants = variants
        self._experiments = experiments

    def render(self) -> Panel:
        lines = []

        # Show variant status
        events = self.obs.event_bus.get_recent_events(15, FlowType.EMERGENCE)

        variant_states = {}
        for event in events:
            if "variant" in event.event_name:
                vid = event.metrics.get("variant_id", event.summary.split("'")[1] if "'" in event.summary else "unknown")
                if "activated" in event.event_name:
                    variant_states[vid[:10]] = ("🟢", "active")
                elif "canary" in event.event_name:
                    pct = event.metrics.get("new_percentage", event.metrics.get("percentage", 0))
                    variant_states[vid[:10]] = ("🟡", f"canary {pct:.0%}")
                elif "proposed" in event.event_name:
                    variant_states[vid[:10]] = ("🔵", "proposed")
                elif "rolled_back" in event.event_name:
                    variant_states[vid[:10]] = ("🔴", "rolled back")

        if variant_states:
            lines.append(Text("Variants:", style="bold magenta"))
            for vid, (icon, status) in list(variant_states.items())[:4]:
                line = Text()
                line.append(f"  {icon} {vid}: ", style="magenta")
                line.append(status, style="white")
                lines.append(line)
        else:
            lines.append(Text("No variants tracked", style="dim"))

        # Show experiment status
        exp_events = [e for e in events if "experiment" in e.event_name]
        if exp_events:
            lines.append(Text(""))
            lines.append(Text("Experiments:", style="bold magenta"))
            for event in exp_events[-2:]:
                line = Text()
                line.append(f"  🧪 ", style="magenta")
                line.append(event.summary[:35], style="white")
                lines.append(line)

        return Panel(
            Group(*lines),
            title=self.title,
            border_style="magenta",
            box=box.ROUNDED,
        )


class WorkflowPanel(DashboardPanel):
    """Panel showing workflow progress."""

    def __init__(self, observation_manager: ObservationManager):
        super().__init__("⚙️ Workflows", observation_manager)

    def render(self) -> Panel:
        events = self.obs.event_bus.get_recent_events(20, FlowType.WORKFLOW)

        # Track workflow states
        workflows = {}
        for event in events:
            wid = event.session_id
            if not wid:
                continue
            wid_short = wid[:8]

            if "workflow_started" in event.event_name:
                wf_type = event.summary.split("'")[1] if "'" in event.summary else "unknown"
                step_count = event.metrics.get("step_count", 0)
                workflows[wid_short] = {
                    "type": wf_type,
                    "steps": step_count,
                    "current": 0,
                    "status": "running"
                }
            elif "step_completed" in event.event_name and wid_short in workflows:
                workflows[wid_short]["current"] += 1
            elif "workflow_completed" in event.event_name and wid_short in workflows:
                workflows[wid_short]["status"] = "completed"

        lines = []
        if workflows:
            for wid, info in list(workflows.items())[:4]:
                progress = info["current"] / max(info["steps"], 1)
                bar = "█" * int(progress * 10) + "░" * (10 - int(progress * 10))

                status_icon = "🔄" if info["status"] == "running" else "✅"

                line = Text()
                line.append(f"{status_icon} ", style="blue")
                line.append(f"{info['type'][:12]}: ", style="cyan")
                line.append(f"[{bar}]", style="blue")
                line.append(f" {info['current']}/{info['steps']}", style="dim")
                lines.append(line)
        else:
            lines.append(Text("No active workflows", style="dim"))

        return Panel(
            Group(*lines),
            title=self.title,
            border_style="blue",
            box=box.ROUNDED,
        )


class MetricsPanel(DashboardPanel):
    """Panel showing system metrics."""

    def __init__(self, observation_manager: ObservationManager):
        super().__init__("📊 Metrics", observation_manager)

    def render(self) -> Panel:
        stats = self.obs.event_bus.get_stats()

        lines = []

        # Total events
        line = Text()
        line.append("Events: ", style="dim")
        line.append(f"{stats['total_events']:,}", style="bold white")
        lines.append(line)

        # Throughput
        line = Text()
        line.append("Rate: ", style="dim")
        line.append(f"{stats['events_per_second']:.1f}/s", style="green")
        lines.append(line)

        # Error rate
        line = Text()
        line.append("Errors: ", style="dim")
        error_style = "red" if stats['error_rate'] > 0.05 else "green"
        line.append(f"{stats['error_rate']:.1%}", style=error_style)
        lines.append(line)

        # Uptime
        line = Text()
        line.append("Uptime: ", style="dim")
        mins = int(stats['uptime_seconds'] // 60)
        secs = int(stats['uptime_seconds'] % 60)
        line.append(f"{mins}m {secs}s", style="cyan")
        lines.append(line)

        # Type breakdown
        lines.append(Text(""))
        lines.append(Text("By Type:", style="dim"))
        for flow_type, count in list(stats['type_counts'].items())[:5]:
            short_type = flow_type.split("_")[0][:8]
            line = Text()
            line.append(f"  {short_type}: ", style="dim")
            line.append(f"{count}", style=FLOW_COLORS.get(FlowType(flow_type), "white"))
            lines.append(line)

        return Panel(
            Group(*lines),
            title=self.title,
            border_style="green",
            box=box.ROUNDED,
        )


class AlignmentPanel(DashboardPanel):
    """Panel showing alignment activity."""

    def __init__(self, observation_manager: ObservationManager):
        super().__init__("🎯 Alignment", observation_manager)

    def render(self) -> Panel:
        events = self.obs.event_bus.get_recent_events(15, FlowType.ALIGNMENT)

        lines = []

        # Recent alignment checks
        alignment_results = []
        for event in events:
            if "alignment_completed" in event.event_name:
                score = event.metrics.get("score", 0)
                alignment_results.append(score)

        if alignment_results:
            avg_score = sum(alignment_results) / len(alignment_results)
            line = Text()
            line.append("Avg Score: ", style="dim")
            score_color = "green" if avg_score > 0.7 else "yellow" if avg_score > 0.4 else "red"
            line.append(f"{avg_score:.1%}", style=f"bold {score_color}")
            lines.append(line)

            line = Text()
            line.append(f"Checks: {len(alignment_results)}", style="dim")
            lines.append(line)
            lines.append(Text(""))

        # Recent events
        for event in reversed(events[-4:]):
            icon = "✅" if "completed" in event.event_name else "🔍"
            if event.metrics.get("score", 1) < 0.5:
                icon = "⚠️"

            line = Text()
            line.append(f"{icon} ", style="green")
            summary = event.summary[:35]
            lines.append(Text(summary, style="white"))

        if not lines:
            lines.append(Text("No alignment checks", style="dim"))

        return Panel(
            Group(*lines),
            title=self.title,
            border_style="green",
            box=box.ROUNDED,
        )


# ==================== Main Dashboard ====================

class FlowDashboard:
    """
    Main dashboard for observing XenoComm communication flows.

    Provides a real-time TUI display of agent activity, negotiations,
    protocol evolution, and system metrics.
    """

    def __init__(self, observation_manager: ObservationManager | None = None):
        if not RICH_AVAILABLE:
            raise ImportError("Rich library is required. Install with: pip install rich")

        self.obs = observation_manager or get_observation_manager()
        self.console = Console()
        self._running = False

        # Initialize panels
        self.event_feed = EventFeedPanel(self.obs)
        self.agent_panel = AgentStatusPanel(self.obs)
        self.negotiation_panel = NegotiationFlowPanel(self.obs)
        self.emergence_panel = EmergencePanel(self.obs)
        self.workflow_panel = WorkflowPanel(self.obs)
        self.metrics_panel = MetricsPanel(self.obs)
        self.alignment_panel = AlignmentPanel(self.obs)

    def _create_layout(self) -> Layout:
        """Create the dashboard layout."""
        layout = Layout()

        # Header
        layout.split_column(
            Layout(name="header", size=3),
            Layout(name="body"),
            Layout(name="footer", size=3),
        )

        # Body split
        layout["body"].split_row(
            Layout(name="left", ratio=1),
            Layout(name="center", ratio=2),
            Layout(name="right", ratio=1),
        )

        # Left column
        layout["left"].split_column(
            Layout(name="agents"),
            Layout(name="alignment"),
        )

        # Center column
        layout["center"].split_column(
            Layout(name="feed", ratio=2),
            Layout(name="workflows"),
        )

        # Right column
        layout["right"].split_column(
            Layout(name="negotiations"),
            Layout(name="emergence"),
            Layout(name="metrics"),
        )

        return layout

    def _render_header(self) -> Panel:
        """Render the header."""
        title = Text()
        title.append("🌐 ", style="bold")
        title.append("XenoComm Flow Observatory", style="bold cyan")
        title.append(" │ ", style="dim")
        title.append(datetime.now().strftime("%Y-%m-%d %H:%M:%S"), style="dim")

        return Panel(
            title,
            style="bold",
            box=box.DOUBLE,
        )

    def _render_footer(self) -> Panel:
        """Render the footer."""
        footer = Text()
        footer.append("Press ", style="dim")
        footer.append("Ctrl+C", style="bold yellow")
        footer.append(" to exit │ ", style="dim")
        footer.append("Flows: ", style="dim")
        for ft, icon in list(FLOW_ICONS.items())[:6]:
            footer.append(f"{icon} {ft.value.split('_')[0]} ", style=FLOW_COLORS.get(ft, "white"))

        return Panel(footer, box=box.SIMPLE)

    def _update_layout(self, layout: Layout) -> None:
        """Update all panels in the layout."""
        layout["header"].update(self._render_header())
        layout["footer"].update(self._render_footer())

        layout["agents"].update(self.agent_panel.render())
        layout["alignment"].update(self.alignment_panel.render())
        layout["feed"].update(self.event_feed.render())
        layout["workflows"].update(self.workflow_panel.render())
        layout["negotiations"].update(self.negotiation_panel.render())
        layout["emergence"].update(self.emergence_panel.render())
        layout["metrics"].update(self.metrics_panel.render())

    def update_from_orchestrator(self, orchestrator: Any) -> None:
        """Update dashboard data from orchestrator."""
        # Update agents
        if hasattr(orchestrator, 'agent_registry'):
            agents = {}
            for aid, ctx in orchestrator.agent_registry.items():
                agents[aid] = {
                    "domains": ctx.knowledge_domains,
                    "capabilities": list(ctx.capabilities.keys()),
                }
            self.agent_panel.update_agents(agents)

        # Update emergence data
        if hasattr(orchestrator, 'emergence'):
            variants = []
            experiments = []
            if hasattr(orchestrator.emergence, 'variants'):
                variants = [v.to_dict() if hasattr(v, 'to_dict') else str(v)
                           for v in orchestrator.emergence.variants.values()]
            if hasattr(orchestrator.emergence, 'experiments'):
                experiments = [e.to_dict() if hasattr(e, 'to_dict') else str(e)
                              for e in orchestrator.emergence.experiments.values()]
            self.emergence_panel.update_emergence(variants, experiments)

    def run(self, refresh_rate: float = 0.5) -> None:
        """Run the dashboard in live mode."""
        self._running = True
        layout = self._create_layout()

        self.console.print("[bold cyan]Starting XenoComm Flow Observatory...[/]")
        self.console.print("[dim]Initializing sensors and event bus...[/]")
        time.sleep(0.5)

        try:
            with Live(layout, console=self.console, refresh_per_second=int(1/refresh_rate),
                     screen=True) as live:
                while self._running:
                    self._update_layout(layout)
                    time.sleep(refresh_rate)
        except KeyboardInterrupt:
            self._running = False
            self.console.print("\n[yellow]Dashboard stopped.[/]")

    def stop(self) -> None:
        """Stop the dashboard."""
        self._running = False

    def render_once(self) -> str:
        """Render the dashboard once and return as string."""
        layout = self._create_layout()
        self._update_layout(layout)

        with self.console.capture() as capture:
            self.console.print(layout)
        return capture.get()

    def get_snapshot(self) -> dict[str, Any]:
        """Get a data snapshot for API/export."""
        return self.obs.get_dashboard_data()


# ==================== Simple Text Dashboard (No Rich) ====================

class SimpleTextDashboard:
    """
    Simple text-based dashboard that works without Rich library.
    Provides basic ASCII visualization of flows.
    """

    def __init__(self, observation_manager: ObservationManager | None = None):
        self.obs = observation_manager or get_observation_manager()
        self._running = False

    def render(self) -> str:
        """Render dashboard as plain text."""
        lines = []
        stats = self.obs.event_bus.get_stats()
        events = self.obs.event_bus.get_recent_events(15)

        # Header
        lines.append("=" * 60)
        lines.append("        XENOCOMM FLOW OBSERVATORY")
        lines.append(f"        {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        lines.append("=" * 60)

        # Metrics
        lines.append("")
        lines.append(f"  Events: {stats['total_events']:,}  |  "
                    f"Rate: {stats['events_per_second']:.1f}/s  |  "
                    f"Errors: {stats['error_rate']:.1%}")
        lines.append("-" * 60)

        # Event feed
        lines.append("")
        lines.append("  RECENT EVENTS:")
        lines.append("")

        for event in reversed(events[-10:]):
            icon = {
                FlowType.AGENT_LIFECYCLE: "[AGT]",
                FlowType.ALIGNMENT: "[ALN]",
                FlowType.NEGOTIATION: "[NEG]",
                FlowType.EMERGENCE: "[EMG]",
                FlowType.WORKFLOW: "[WKF]",
                FlowType.COLLABORATION: "[COL]",
                FlowType.SYSTEM: "[SYS]",
            }.get(event.flow_type, "[???]")

            time_str = event.timestamp.strftime("%H:%M:%S")
            lines.append(f"  {time_str} {icon} {event.summary[:45]}")

        lines.append("")
        lines.append("=" * 60)

        return "\n".join(lines)

    def run(self, refresh_rate: float = 1.0) -> None:
        """Run the simple dashboard."""
        self._running = True
        print("\033[2J")  # Clear screen

        try:
            while self._running:
                print("\033[H")  # Move to top
                print(self.render())
                time.sleep(refresh_rate)
        except KeyboardInterrupt:
            self._running = False
            print("\nDashboard stopped.")

    def stop(self) -> None:
        """Stop the dashboard."""
        self._running = False


# ==================== CLI Entry Point ====================

def run_dashboard(use_rich: bool = True, refresh_rate: float = 0.5) -> None:
    """Run the appropriate dashboard."""
    obs = get_observation_manager()
    obs.start()

    try:
        if use_rich and RICH_AVAILABLE:
            dashboard = FlowDashboard(obs)
            dashboard.run(refresh_rate)
        else:
            dashboard = SimpleTextDashboard(obs)
            dashboard.run(refresh_rate)
    finally:
        obs.stop()


if __name__ == "__main__":
    run_dashboard()
