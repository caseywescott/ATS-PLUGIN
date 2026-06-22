"""Shared pytest fixtures and helpers for ATS MCP test suite."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

REPO = Path(__file__).parent.parent.parent
TOOLS = REPO / "build-plugin" / "tools"
CROTALES = REPO / "crotales.ats"


def built(name: str) -> bool:
    return (TOOLS / name).exists()


@pytest.fixture
def crotales_path():
    if not CROTALES.exists():
        pytest.skip(f"Fixture missing: {CROTALES}")
    return str(CROTALES)
