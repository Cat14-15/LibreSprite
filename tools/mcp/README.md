# LibreSprite local MCP server

This stdio MCP server lets Codex or Claude Code control the currently running
LibreSprite window. It does not call an AI API and does not need an API key.

The client launches `libresprite_mcp.py`. The server exchanges local request
files with LibreSprite through `%APPDATA%/LibreSprite/mcp` on Windows. Set the
same `LIBRESPRITE_MCP_DIR` environment variable for both processes to override
that location.

Exposed tools:

- `libresprite_status`
- `libresprite_api_reference`
- `libresprite_execute`
- `libresprite_run_script` for reusable `.js` and `.py` scripts

Each execute call is committed as one Undo operation. A JavaScript error,
Escape cancellation, or timeout rolls the operation back.
