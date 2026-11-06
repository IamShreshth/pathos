"""pathos API server.

Run from the repository root:
    .venv/bin/uvicorn app.main:app --app-dir backend --port 8000
"""

from __future__ import annotations

import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from app.config import Settings
from app.routes.api import router
from app.routes.mobility import router as mobility_router
from app.services.graph_manager import GraphManager

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger("pathos")


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_env()

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        log.info("loading graph %s", settings.graph_path)
        app.state.settings = settings
        app.state.graphs = GraphManager(settings)
        app.state.graphs.load(settings.graph_path)
        try:
            yield
        finally:
            app.state.graphs.shutdown()

    app = FastAPI(
        title="pathos",
        description="Urban routing, multi-stop tours and (Pathos 2.0) adaptive, uncertainty-aware multi-vehicle "
        "routing on OpenStreetMap roads.",
        version="2.0.0",
        lifespan=lifespan,
    )
    app.add_middleware(
        CORSMiddleware, allow_origins=settings.cors_origins, allow_methods=["*"], allow_headers=["*"]
    )
    app.include_router(router)
    app.include_router(mobility_router)

    # Serve the built React app (frontend/dist) when present, so one process runs everything.
    dist = settings.frontend_dist
    if (dist / "index.html").exists():
        app.mount("/assets", StaticFiles(directory=dist / "assets"), name="assets")

        @app.get("/", include_in_schema=False)
        def index() -> FileResponse:
            return FileResponse(dist / "index.html")

    return app


app = create_app()
