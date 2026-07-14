# REST API

A minimal REST API example demonstrating JSON responses, CORS headers, and route handling.

## Run

```sh
pt server.pt
```

Server starts on http://localhost:3001

## Routes

| Route | Method | Description |
|-------|--------|-------------|
| `/api/users` | GET | List all users |
| `/api/users` | POST | Add a new user |
| `/api/health` | GET | Health check |
| `*` | OPTIONS | CORS preflight |

## Files

| File | Purpose |
|------|---------|
| `server.pt` | Server and API handlers |

## Example Requests

```sh
# List users
curl http://localhost:3001/api/users

# Add user
curl -X POST http://localhost:3001/api/users

# Health check
curl http://localhost:3001/api/health
```

## Features

- JSON responses with proper Content-Type headers
- CORS support (Access-Control-Allow-Origin)
- HTTP status codes (200, 201, 204, 404)
- Route + method matching
