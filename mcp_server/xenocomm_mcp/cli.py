#!/usr/bin/env python3
"""
XenoComm CLI

Command-line interface for XenoComm tools including the observation dashboard.
"""

import argparse
import sys
from pathlib import Path


def run_dashboard(args):
    """Launch the observation dashboard."""
    from .observation import get_observation_manager
    from .instrumented import create_instrumented_system

    # Create instrumented system
    orchestrator, workflow_manager, obs = create_instrumented_system()

    if args.mode == "rich" or args.mode == "dashboard":
        try:
            from .dashboard import FlowDashboard, RICH_AVAILABLE
            if not RICH_AVAILABLE:
                print("Rich library not installed. Install with: pip install rich")
                print("Falling back to text mode...")
                args.mode = "text"
        except ImportError:
            print("Dashboard module not available. Falling back to text mode...")
            args.mode = "text"

    if args.mode == "text":
        from .dashboard import SimpleTextDashboard
        dashboard = SimpleTextDashboard(obs)
        print("\nStarting XenoComm Flow Observatory (Text Mode)...")
        print("Press Ctrl+C to exit\n")
        dashboard.run(refresh_rate=args.refresh)
    elif args.mode == "headless":
        from .observation import FlowType

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
        print("\nXenoComm Flow Observer (Headless Mode)")
        print("=" * 50)
        print("Streaming events... Press Ctrl+C to stop\n")

        import time
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
    else:  # rich/dashboard mode
        from .dashboard import FlowDashboard
        dashboard = FlowDashboard(obs)
        dashboard.update_from_orchestrator(orchestrator)
        print("\nStarting XenoComm Flow Observatory...")
        print("Press Ctrl+C to exit\n")
        dashboard.run(refresh_rate=args.refresh)

    obs.stop()


def run_demo(args):
    """Run the observation demo with simulated activity."""
    from .demo_observation import (
        run_demo_with_dashboard,
        run_demo_text_only,
        run_demo_headless,
    )

    if args.mode == "dashboard" or args.mode == "rich":
        run_demo_with_dashboard()
    elif args.mode == "text":
        run_demo_text_only()
    else:
        run_demo_headless()


def show_stats(args):
    """Show current observation stats."""
    from .observation import get_observation_manager

    obs = get_observation_manager()
    stats = obs.event_bus.get_stats()

    print("\nXenoComm Observation Stats")
    print("=" * 40)
    print(f"  Total Events: {stats['total_events']:,}")
    print(f"  Events/sec: {stats['events_per_second']:.2f}")
    print(f"  Error Rate: {stats['error_rate']:.1%}")
    print(f"  Uptime: {stats['uptime_seconds']:.1f}s")
    print(f"  Subscribers: {stats['subscriber_count']}")
    print("\n  Events by Type:")
    for flow_type, count in sorted(stats['type_counts'].items()):
        print(f"    {flow_type}: {count}")
    print()


def run_analytics(args):
    """Run analytics on the observation data."""
    from .observation import get_observation_manager
    from .analytics import FlowAnalytics, TimeWindow

    obs = get_observation_manager()
    analytics = FlowAnalytics(obs.event_bus)

    if args.window:
        window = TimeWindow.last_minutes(args.window)
        print(f"\nAnalytics for last {args.window} minutes")
    else:
        window = None
        print("\nAnalytics for all events")

    print("=" * 50)

    # Flow metrics
    flow_metrics = analytics.get_flow_metrics(window)
    print("\nFlow Metrics:")
    for ft, metrics in flow_metrics.items():
        print(f"\n  {ft.value}:")
        print(f"    Events: {metrics.event_count}")
        print(f"    Errors: {metrics.error_count}")
        print(f"    Rate: {metrics.events_per_minute:.2f}/min")
        if metrics.avg_duration_ms:
            print(f"    Avg Duration: {metrics.avg_duration_ms:.1f}ms")

    # Error summary
    error_summary = analytics.get_error_summary(window)
    print(f"\nError Summary:")
    print(f"  Total Errors: {error_summary['total_errors']}")
    print(f"  Error Rate: {error_summary['error_rate']:.1%}")

    # Anomalies
    anomalies = analytics.detect_anomalies(window)
    if anomalies:
        print(f"\nAnomalies Detected: {len(anomalies)}")
        for anomaly in anomalies:
            print(f"  - [{anomaly['severity']}] {anomaly['description']}")
    else:
        print("\nNo anomalies detected.")

    print()


def run_research(args):
    """Run multi-agent research with live dashboard."""
    from .live_agent_demo import run_with_dashboard

    run_with_dashboard(
        task=args.task,
        num_agents=args.agents,
        mode=args.mode,
        duration=args.duration,
        refresh=args.refresh,
    )


def main():
    """Main CLI entry point."""
    parser = argparse.ArgumentParser(
        prog="xenocomm",
        description="XenoComm SDK Command Line Tools",
    )
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # Dashboard command
    dash_parser = subparsers.add_parser(
        "dashboard",
        help="Launch the observation dashboard",
        aliases=["dash", "observe"],
    )
    dash_parser.add_argument(
        "-m", "--mode",
        choices=["rich", "text", "headless", "dashboard"],
        default="dashboard",
        help="Dashboard mode (default: dashboard/rich)",
    )
    dash_parser.add_argument(
        "-r", "--refresh",
        type=float,
        default=0.5,
        help="Refresh rate in seconds (default: 0.5)",
    )
    dash_parser.set_defaults(func=run_dashboard)

    # Demo command
    demo_parser = subparsers.add_parser(
        "demo",
        help="Run the observation demo with simulated activity",
    )
    demo_parser.add_argument(
        "-m", "--mode",
        choices=["dashboard", "text", "headless"],
        default="headless",
        help="Demo mode (default: headless)",
    )
    demo_parser.set_defaults(func=run_demo)

    # Stats command
    stats_parser = subparsers.add_parser(
        "stats",
        help="Show observation statistics",
    )
    stats_parser.set_defaults(func=show_stats)

    # Analytics command
    analytics_parser = subparsers.add_parser(
        "analytics",
        help="Run analytics on observation data",
    )
    analytics_parser.add_argument(
        "-w", "--window",
        type=int,
        help="Time window in minutes (default: all events)",
    )
    analytics_parser.set_defaults(func=run_analytics)

    # Research command - multi-agent coordination with live dashboard
    research_parser = subparsers.add_parser(
        "research",
        help="Run multi-agent research with live Flow Observatory",
        aliases=["agents", "coordinate"],
    )
    research_parser.add_argument(
        "-t", "--task",
        default="Evaluate and compare how artificial intelligence has been used over the last 30 years in assistive technology",
        help="Research task for agents to work on",
    )
    research_parser.add_argument(
        "-n", "--agents",
        type=int,
        default=3,
        help="Number of research agents (default: 3)",
    )
    research_parser.add_argument(
        "-m", "--mode",
        choices=["text", "rich", "dashboard", "headless"],
        default="text",
        help="Dashboard mode (default: text)",
    )
    research_parser.add_argument(
        "-d", "--duration",
        type=int,
        default=30,
        help="Research phase duration in seconds (default: 30)",
    )
    research_parser.add_argument(
        "-r", "--refresh",
        type=float,
        default=0.5,
        help="Dashboard refresh rate (default: 0.5)",
    )
    research_parser.set_defaults(func=run_research)

    # Parse and execute
    args = parser.parse_args()

    if hasattr(args, "func"):
        try:
            args.func(args)
        except KeyboardInterrupt:
            print("\nInterrupted.")
            sys.exit(0)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
