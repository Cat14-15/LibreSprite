#!/usr/bin/env python3
"""Local stdio MCP server for controlling a running LibreSprite instance."""

from __future__ import annotations

import json
import os
import sys
import time
import uuid
from pathlib import Path

PROTOCOL_VERSION = "2025-06-18"

API_REFERENCE = """LibreSprite JavaScript API (zero-based indices):
app.activeSprite, app.activeImage, app.pixelColor.rgba(r,g,b,a)
app.createSprite(width,height,colorMode) where colorMode: 0 RGBA, 1 grayscale, 2 indexed
Sprite: layer(i), addLayer(name), duplicateLayer(i), removeLayer(i), moveLayer(i,newPosition),
  addFrame(copyPrevious), duplicateFrame(source,insertAt), removeFrame(i), moveFrame(i,before),
  ensureCel(layer,frame), ensureImage(layer,frame), frameDuration(i), setFrameDuration(i,ms), createTag(name,from,to),
  removeTag(name), resize(w,h), saveTo(absolutePath)
Layer: name, isVisible, isEditable, opacity (0..255), blendMode (0..15), cel(frame)
Cel: x, y, image, frame, setPosition(x,y)
Image: width, height, getPixel(x,y), putPixel(x,y,color), clear(color), drawLine,
  fillRect, drawRect, drawEllipse, fillEllipse, drawCircle, fillCircle, floodFill,
  copyRect(sourceX,sourceY,width,height,destinationX,destinationY), flipHorizontal(),
  flipVertical(), fillGradient(startColor,endColor,vertical)
Palette: getColor(index), setColor(index,color), setColors(startIndex,...colors)
All mutations in one tool call are one Undo step. Any JavaScript error rolls the whole call back.
Do not call app.launch or storage.fetch. Check app.activeSprite/app.activeImage before using them.
For drawing in a newly created sprite, use sprite.ensureImage(layer,frame) directly.

Saved Python scripts use snake_case and start with `from libresprite import app`.
Common mappings: create_sprite, active_sprite, active_image, pixel_color.rgba,
ensure_image, put_pixel, fill_rect, draw_line, draw_rect, fill_circle,
fill_gradient, add_layer, add_frame, create_tag, and save_to.
"""


def bridge_dir() -> Path:
    configured = os.environ.get("LIBRESPRITE_MCP_DIR")
    if configured:
        return Path(configured)
    if os.name == "nt":
        return Path(os.environ.get("APPDATA", Path.home())) / "LibreSprite" / "mcp"
    xdg = os.environ.get("XDG_CONFIG_HOME")
    return (Path(xdg) if xdg else Path.home() / ".config") / "libresprite" / "mcp"


def app_running() -> bool:
    heartbeat = bridge_dir() / "libresprite.ready"
    try:
        return time.time() - heartbeat.stat().st_mtime < 3.0
    except OSError:
        return False


def execute_script(source: str, timeout: float = 35.0,
                   language: str = "js") -> tuple[bool, str]:
    if not app_running():
        return False, "LibreSprite is not running or its MCP bridge is unavailable."
    directory = bridge_dir()
    directory.mkdir(parents=True, exist_ok=True)
    request_id = uuid.uuid4().hex
    temporary = directory / f"request-{request_id}.tmp"
    extension = "py" if language == "py" else "js"
    request = directory / f"request-{request_id}.{extension}"
    result = directory / f"request-{request_id}.result"
    temporary.write_text(source, encoding="utf-8")
    os.replace(temporary, request)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = result.read_text(encoding="utf-8").strip()
            result.unlink(missing_ok=True)
            return value == "ok", (
                "Script completed as one undoable operation."
                if value == "ok"
                else "Script failed or was stopped; all changes were rolled back."
            )
        except FileNotFoundError:
            time.sleep(0.05)
    request.unlink(missing_ok=True)
    return False, "Timed out waiting for LibreSprite. The script may have been stopped."


TOOLS = [
    {
        "name": "libresprite_status",
        "title": "LibreSprite Status",
        "description": "Check whether a compatible LibreSprite window is running.",
        "inputSchema": {"type": "object", "properties": {}},
        "annotations": {"readOnlyHint": True, "openWorldHint": False},
    },
    {
        "name": "libresprite_api_reference",
        "title": "LibreSprite Scripting Reference",
        "description": "Return the supported pixel art scripting API before composing an operation.",
        "inputSchema": {"type": "object", "properties": {}},
        "annotations": {"readOnlyHint": True, "openWorldHint": False},
    },
    {
        "name": "libresprite_execute",
        "title": "Execute LibreSprite Operation",
        "description": (
            "Execute JavaScript in the running LibreSprite on its UI thread. "
            "Use the API reference first. The whole call is one Undo step and rolls back on error."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {
                    "type": "string",
                    "description": "LibreSprite JavaScript to execute. Do not include Markdown fences.",
                }
            },
            "required": ["code"],
        },
        "annotations": {
            "readOnlyHint": False,
            "destructiveHint": True,
            "idempotentHint": False,
            "openWorldHint": False,
        },
    },
    {
        "name": "libresprite_run_script",
        "title": "Run Saved LibreSprite Script",
        "description": (
            "Run a saved .js or .py LibreSprite script in the open application. "
            "Python scripts use the bundled from libresprite import app API."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": "Absolute path to a saved .js or .py script.",
                }
            },
            "required": ["path"],
        },
        "annotations": {
            "readOnlyHint": False,
            "destructiveHint": True,
            "idempotentHint": False,
            "openWorldHint": False,
        },
    },
]


def text_result(text: str, error: bool = False) -> dict:
    return {"content": [{"type": "text", "text": text}], "isError": error}


def dispatch(message: dict) -> dict | None:
    method = message.get("method")
    request_id = message.get("id")
    if method == "notifications/initialized":
        return None
    if request_id is None:
        return None
    if method == "initialize":
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "libresprite-local", "version": "0.1.0"},
                "instructions": "Use libresprite_api_reference before executing pixel art operations.",
            },
        }
    if method == "ping":
        result = {}
    elif method == "tools/list":
        result = {"tools": TOOLS}
    elif method == "tools/call":
        params = message.get("params") or {}
        name = params.get("name")
        arguments = params.get("arguments") or {}
        if name == "libresprite_status":
            result = text_result("LibreSprite MCP bridge is ready." if app_running()
                                 else "LibreSprite MCP bridge is not available.",
                                 not app_running())
        elif name == "libresprite_api_reference":
            result = text_result(API_REFERENCE)
        elif name == "libresprite_execute":
            code = arguments.get("code")
            if not isinstance(code, str) or not code.strip():
                result = text_result("The code argument must be a non-empty string.", True)
            else:
                ok, detail = execute_script(code)
                result = text_result(detail, not ok)
        elif name == "libresprite_run_script":
            path_value = arguments.get("path")
            path = Path(path_value) if isinstance(path_value, str) else None
            if path is None or path.suffix.lower() not in (".js", ".py"):
                result = text_result("Path must be an absolute .js or .py file.", True)
            elif not path.is_absolute() or not path.is_file():
                result = text_result("The script file does not exist.", True)
            else:
                try:
                    source = path.read_text(encoding="utf-8")
                    ok, detail = execute_script(
                        source, language="py" if path.suffix.lower() == ".py" else "js")
                    result = text_result(detail, not ok)
                except OSError as exc:
                    result = text_result(f"Could not read script: {exc}", True)
        else:
            return {
                "jsonrpc": "2.0", "id": request_id,
                "error": {"code": -32602, "message": f"Unknown tool: {name}"},
            }
    else:
        return {
            "jsonrpc": "2.0", "id": request_id,
            "error": {"code": -32601, "message": f"Method not found: {method}"},
        }
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def main() -> int:
    for line in sys.stdin:
        try:
            message = json.loads(line)
            response = dispatch(message)
            if response is not None:
                sys.stdout.write(json.dumps(response, ensure_ascii=False, separators=(",", ":")) + "\n")
                sys.stdout.flush()
        except Exception as exc:
            request_id = message.get("id") if isinstance(locals().get("message"), dict) else None
            if request_id is not None:
                response = {"jsonrpc": "2.0", "id": request_id,
                            "error": {"code": -32603, "message": str(exc)}}
                sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
                sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
