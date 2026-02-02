#!/bin/bash
# XenoComm MCP Setup Script for macOS
# Run this script to install and configure XenoComm MCP for Claude Desktop

set -e

echo "🚀 XenoComm MCP Setup for Claude Desktop"
echo "=========================================="

# Get the user's home directory
HOME_DIR="$HOME"
USERNAME=$(whoami)

# Define paths
XENOCOMM_DIR="$HOME_DIR/xenocomm_sdk"
CLAUDE_CONFIG_DIR="$HOME_DIR/Library/Application Support/Claude"
CLAUDE_CONFIG_FILE="$CLAUDE_CONFIG_DIR/claude_desktop_config.json"

# Step 1: Clone the repository (if not already present)
echo ""
echo "📦 Step 1: Cloning XenoComm SDK..."
if [ -d "$XENOCOMM_DIR" ]; then
    echo "   Repository already exists at $XENOCOMM_DIR"
    echo "   Pulling latest changes..."
    cd "$XENOCOMM_DIR"
    git pull
else
    git clone https://github.com/elementalcollision/xenocomm_sdk.git "$XENOCOMM_DIR"
fi

# Step 2: Install Python dependencies
echo ""
echo "📚 Step 2: Installing Python dependencies..."
cd "$XENOCOMM_DIR/mcp_server"
pip3 install mcp fastmcp

# Step 3: Verify the installation
echo ""
echo "✅ Step 3: Verifying installation..."
python3 -c "from xenocomm_mcp import XenoCommOrchestrator; print('   XenoComm MCP modules loaded successfully!')"

# Step 4: Create Claude Desktop config directory (if needed)
echo ""
echo "⚙️  Step 4: Configuring Claude Desktop..."
mkdir -p "$CLAUDE_CONFIG_DIR"

# Step 5: Create or update Claude Desktop config
if [ -f "$CLAUDE_CONFIG_FILE" ]; then
    echo "   Existing config found. Creating backup..."
    cp "$CLAUDE_CONFIG_FILE" "$CLAUDE_CONFIG_FILE.backup.$(date +%Y%m%d_%H%M%S)"

    # Check if xenocomm is already configured
    if grep -q "xenocomm" "$CLAUDE_CONFIG_FILE"; then
        echo "   XenoComm already configured in Claude Desktop!"
    else
        echo "   Adding XenoComm to existing config..."
        # Use Python to merge the configs safely
        python3 << EOF
import json

config_path = "$CLAUDE_CONFIG_FILE"
xenocomm_dir = "$XENOCOMM_DIR"

# Read existing config
with open(config_path, 'r') as f:
    config = json.load(f)

# Ensure mcpServers exists
if 'mcpServers' not in config:
    config['mcpServers'] = {}

# Add xenocomm
config['mcpServers']['xenocomm'] = {
    "command": "python3",
    "args": ["-m", "xenocomm_mcp"],
    "cwd": f"{xenocomm_dir}/mcp_server",
    "env": {
        "PYTHONPATH": f"{xenocomm_dir}/mcp_server"
    }
}

# Write updated config
with open(config_path, 'w') as f:
    json.dump(config, f, indent=2)

print("   Config updated successfully!")
EOF
    fi
else
    echo "   Creating new Claude Desktop config..."
    cat > "$CLAUDE_CONFIG_FILE" << EOF
{
  "mcpServers": {
    "xenocomm": {
      "command": "python3",
      "args": ["-m", "xenocomm_mcp"],
      "cwd": "$XENOCOMM_DIR/mcp_server",
      "env": {
        "PYTHONPATH": "$XENOCOMM_DIR/mcp_server"
      }
    }
  }
}
EOF
    echo "   Config created successfully!"
fi

echo ""
echo "=========================================="
echo "✨ XenoComm MCP Setup Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. Restart Claude Desktop App"
echo "2. You should see XenoComm tools available in Claude"
echo ""
echo "Available tools include:"
echo "  • register_agent - Register agents for coordination"
echo "  • full_alignment_check - Verify agent compatibility"
echo "  • initiate_negotiation - Start protocol negotiation"
echo "  • propose_protocol_variant - Evolve protocols safely"
echo "  • start_ab_experiment - A/B test protocol changes"
echo "  • initiate_collaboration - Full orchestrated workflow"
echo "  • And 30+ more tools!"
echo ""
echo "Config location: $CLAUDE_CONFIG_FILE"
echo "XenoComm location: $XENOCOMM_DIR"
