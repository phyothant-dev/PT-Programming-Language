# Portfolio Website

A personal portfolio site with project showcase and contact API endpoint. Uses template variable replacement.

## Run

```sh
pt server.pt
```

Open http://localhost:3000

## Routes

| Route | Description |
|-------|-------------|
| `GET /` | Portfolio homepage |
| `POST /api/contact` | Contact form endpoint |

## Files

| File | Purpose |
|------|---------|
| `server.pt` | Server and route handlers |
| `templates/portfolio.html` | Page template with `{{PROJECTS}}`, `{{NAME}}`, `{{BIO}}` placeholders |

## Features

- Template variable replacement with `replace()`
- JSON API endpoint
- Dynamic project listing
