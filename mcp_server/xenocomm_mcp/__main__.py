"""
XenoComm MCP Server - Entry Point

Run with:
    python -m xenocomm_mcp                    # stdio transport (default)
    python -m xenocomm_mcp --http             # HTTP transport on port 8000
    python -m xenocomm_mcp --http --port 3000 # HTTP transport on custom port
"""

import argparse
from .server import run_server


def main():
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
