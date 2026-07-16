# Todo App

A full-stack todo application with a REST API backend and HTML frontend. Supports adding and listing todos.

## Run

```sh
pt server.pt
```

Open http://localhost:3000

## Routes

| Route | Method | Description |
|-------|--------|-------------|
| `/` | GET | Todo app frontend |
| `/api/todos` | GET | List all todos |
| `/api/todos` | POST | Add a new todo |
| `/api/todos` | DELETE | Clear all todos |

## Files

| File | Purpose |
|------|---------|
| `server.pt` | Server, API handlers, in-memory data store |
| `index.html` | Frontend with JavaScript for API calls |

## Features

- RESTful JSON API
- In-memory data store
- HTML + JS frontend
- CRUD operations
