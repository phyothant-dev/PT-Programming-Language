# Chat App

A real-time style chat application with a web frontend and message API.

## Run

```sh
pt server.pt
```

Open http://localhost:3000

## Routes

| Route | Method | Description |
|-------|--------|-------------|
| `/` | GET | Chat UI |
| `/api/messages` | GET | Get all messages (JSON) |
| `/api/messages` | POST | Send a message |

## Files

| File | Purpose |
|------|---------|
| `server.pt` | Server and message handlers |
| `chat.html` | Chat interface with auto-refresh |

## Features

- In-memory message store
- JSON message API
- HTML chat interface
- Polling-based message refresh
