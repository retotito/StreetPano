# 0009: Svelte and FastAPI for the user interface

**Status:** Accepted

## Context

The pipeline is written in Python, so the interface has to talk to Python. Its main
view is a gigantic zoomable image with drawing tools on top. OpenSeadragon already
solves deep zoom in a browser. The project also has to run on a computer with 8 GB of
RAM, while AI and rendering jobs are using most of it.

## Decision

Build the interface as a local web app:

- **Frontend:** Svelte and TypeScript, built with Vite.
- **Image view:** OpenSeadragon, with a canvas overlay for seams, outlines and lines.
- **Backend:** FastAPI, using a WebSocket for job progress. API types are generated
  for TypeScript, so that the two sides can't quietly disagree.
- **Tests:** Vitest for components, and Playwright for whole workflows on a synthetic
  street.
- **Desktop app, later:** Tauri, which wraps the same interface using the system's
  own web view.

## Alternatives considered

| Option | Why not |
|---|---|
| Qt (PySide) | One language, but deep zoom and modern interface work would be slower to build. |
| Electron | Bundles a whole browser, which uses memory this project needs. |
| Tauri from the start | Adds packaging work before the interface has proved itself. |
| React | Works equally well, but Svelte is smaller and the maintainer has used it before. |

## Consequences

- Developers need Node.js as well as Python.
- The app runs in a normal browser at first, so there's no installer yet.
- The server must accept connections only from the user's own computer.
- A frontend build step has to be added to CI.
