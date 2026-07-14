# Blog Engine

A simple blog server built with PT's built-in HTTP server. Renders HTML pages from templates and serves an RSS feed.

## Run

```sh
pt server.pt
```

Open http://localhost:3000

## Routes

| Route | Description |
|-------|-------------|
| `GET /` | Blog homepage with post list |
| `GET /rss` | RSS feed (XML) |

## Files

| File | Purpose |
|------|---------|
| `server.pt` | Server and route handlers |
| `templates/header.html` | HTML header partial |
| `templates/footer.html` | HTML footer partial |

## Features

- Template composition (header + content + footer)
- Dynamic HTML rendering
- RSS/XML feed generation
- Static string-based data store
