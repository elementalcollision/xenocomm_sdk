"""
XenoComm MCP Server - Entry Point

Run with:
    python -m xenocomm_mcp                    # stdio transport (default)
    python -m xenocomm_mcp --http             # HTTP transport on port 8000
    python -m xenocomm_mcp --http --port 3000 # HTTP transport on custom port

    python -m xenocomm_mcp dashboard          # Launch observation dashboard
    python -m xenocomm_mcp demo               # Run demo with simulated activity
    python -m xenocomm_mcp stats              # Show observation stats
    python -m xenocomm_mcp analytics          # Run analytics
"""

import sys


def main():
    # Check if a subcommand was provided
    if len(sys.argv) > 1 and sys.argv[1] in (
        "dashboard", "dash", "observe", "demo", "stats", "analytics"
    ):
        # Use the CLI module for these commands
        from .cli import main as cli_main
        cli_main()
    else:
        # Default: run the MCP server
        import argparse
        from .server import run_server

        parser = argparse.ArgumentParser(
            description="XenoComm MCP Server - Agent-to-Agent Coordination Protocol"
        )
        parser.add_argument(
            "--http",
            action="store_true",
            help="Use streamable HTTP transport instead of stdio",
        )
        parser.add_argument(
            "--port",
            type=int,
            default=8000,
            help="Port for HTTP transport (default: 8000)",
        )

        args = parser.parse_args()

        transport = "streamable-http" if args.http else "stdio"
        print(f"Starting XenoComm MCP Server (transport: {transport})")

        run_server(transport=transport, port=args.port)


if __name__ == "__main__":
    main()
