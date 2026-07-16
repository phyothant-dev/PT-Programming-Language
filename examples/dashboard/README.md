# Admin Dashboard

An admin dashboard with stats overview and order management. Serves a frontend and JSON API.

## Run

```sh
pt server.pt
```

Open http://localhost:3000

## Routes

| Route | Description |
|-------|-------------|
| `GET /` | Dashboard UI |
| `GET /api/stats` | User, revenue, order stats |
| `GET /api/orders` | Order list |

## Files

| File | Purpose |
|------|---------|
| `server.pt` | Server, data, API handlers |
| `dashboard.html` | Dashboard UI with charts/stats |

## Features

- Stats summary (users, revenue, orders)
- Order listing with details
- JSON API endpoints
- Responsive dashboard layout
