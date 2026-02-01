"""Tests for the XenoComm Alignment Engine."""

import pytest
from xenocomm_mcp.alignment import (
    AlignmentEngine,
    AgentContext,
    AlignmentStatus,
)


@pytest.fixture
def engine():
    return AlignmentEngine()


@pytest.fixture
def agent_a():
    return AgentContext(
        agent_id="agent-a",
        knowledge_domains=["python", "machine_learning", "web_development"],
        goals=[
            {"type": "assistance", "description": "Help with ML tasks"},
            {"type": "education", "description": "Explain concepts"},
        ],
        terminology={
            "model": "A trained ML algorithm",
            "pipeline": "A sequence of data processing steps",
        },
        assumptions=[
            "User has Python installed",
            "User prefers concise explanations",
        ],
        context_params={
            "environment": "development",
            "verbosity": "medium",
        },
    )


@pytest.fixture
def agent_b():
    return AgentContext(
        agent_id="agent-b",
        knowledge_domains=["python", "data_science", "statistics"],
        goals=[
            {"type": "assistance", "description": "Analyze data"},
            {"type": "research", "description": "Find patterns"},
        ],
        terminology={
            "model": "A statistical representation",
            "pipeline": "ETL workflow",
        },
        assumptions=[
            "User has Python installed",
            "Data is in CSV format",
        ],
        context_params={
            "environment": "development",
            "max_memory_gb": 8,
        },
    )


class TestKnowledgeVerification:
    def test_shared_domains(self, engine, agent_a, agent_b):
        result = engine.verify_knowledge(agent_a, agent_b)

        assert result.status in (AlignmentStatus.ALIGNED, AlignmentStatus.PARTIAL)
        assert "python" in result.details["shared_domains"]

    def test_required_domains_missing(self, engine, agent_a, agent_b):
        result = engine.verify_knowledge(
            agent_a, agent_b, required_domains=["quantum_computing"]
        )

        assert result.status == AlignmentStatus.MISALIGNED
        assert len(result.recommendations) > 0


class TestGoalAlignment:
    def test_goal_verification(self, engine, agent_a, agent_b):
        result = engine.verify_goals(agent_a, agent_b)

        assert result.status in (
            AlignmentStatus.ALIGNED,
            AlignmentStatus.PARTIAL,
            AlignmentStatus.UNKNOWN,
        )
        assert "conflicts" in result.details


class TestTerminologyAlignment:
    def test_terminology_conflicts(self, engine, agent_a, agent_b):
        result = engine.align_terminology(agent_a, agent_b)

        # Both agents define "model" and "pipeline" differently
        assert len(result.details.get("conflicts", [])) > 0

    def test_shared_terms(self, engine, agent_a, agent_b):
        result = engine.align_terminology(agent_a, agent_b)

        assert "model" in result.details["shared_terms"]
        assert "pipeline" in result.details["shared_terms"]


class TestAssumptionVerification:
    def test_shared_assumptions(self, engine, agent_a, agent_b):
        result = engine.verify_assumptions(agent_a, agent_b)

        # Both assume Python is installed
        shared = result.details["shared_assumptions"]
        assert any("Python" in a for a in shared)

    def test_unique_assumptions(self, engine, agent_a, agent_b):
        result = engine.verify_assumptions(agent_a, agent_b)

        unique_a = result.details["unique_to_a"]
        unique_b = result.details["unique_to_b"]

        assert len(unique_a) > 0 or len(unique_b) > 0


class TestContextSynchronization:
    def test_matched_params(self, engine, agent_a, agent_b):
        result = engine.sync_context(agent_a, agent_b)

        # Both have "environment": "development"
        assert "environment" in result.details["matched_params"]

    def test_required_params(self, engine, agent_a, agent_b):
        result = engine.sync_context(
            agent_a, agent_b, required_params=["api_key"]
        )

        assert result.status == AlignmentStatus.MISALIGNED
        assert "api_key" in result.details["required_missing"]


class TestFullAlignmentCheck:
    def test_full_check(self, engine, agent_a, agent_b):
        results = engine.full_alignment_check(agent_a, agent_b)

        assert "knowledge" in results
        assert "goals" in results
        assert "terminology" in results
        assert "assumptions" in results
        assert "context" in results

        for name, result in results.items():
            assert hasattr(result, "status")
            assert hasattr(result, "confidence")
            assert hasattr(result, "recommendations")
