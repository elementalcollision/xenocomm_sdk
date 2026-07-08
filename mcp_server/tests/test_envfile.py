"""Tests for the dependency-free .env loader (xenocomm_mcp.envfile)."""

import os

import pytest

from xenocomm_mcp.envfile import parse_env, load_env_files


def test_parse_basic():
    d = parse_env("OPENROUTER_API_KEY=sk-or-abc\nOPENROUTER_MODEL=deepseek/deepseek-v4-flash")
    assert d["OPENROUTER_API_KEY"] == "sk-or-abc"
    assert d["OPENROUTER_MODEL"] == "deepseek/deepseek-v4-flash"


def test_parse_ignores_comments_and_blanks():
    d = parse_env("# a comment\n\n   \nKEY=value\n")
    assert d == {"KEY": "value"}


def test_parse_strips_quotes_and_export_and_spaces():
    d = parse_env('export FOO = "bar baz"\nQUX =\'q\'\n')
    assert d["FOO"] == "bar baz"
    assert d["QUX"] == "q"


def test_parse_keeps_equals_in_value():
    d = parse_env("URL=https://x/api?a=1&b=2")
    assert d["URL"] == "https://x/api?a=1&b=2"


def test_parse_skips_malformed_lines():
    d = parse_env("no_equals_here\nGOOD=1")
    assert d == {"GOOD": "1"}


def test_load_reads_file_and_respects_override(tmp_path, monkeypatch):
    env = tmp_path / ".env"
    env.write_text("XENO_TEST_A=fromfile\nXENO_TEST_B=fromfile\n")
    monkeypatch.setenv("XENOCOMM_ENV_FILE", str(env))
    monkeypatch.delenv("XENO_TEST_A", raising=False)
    monkeypatch.setenv("XENO_TEST_B", "fromenv")  # pre-existing -> must win

    loaded = load_env_files()
    assert str(env.resolve()) in loaded
    assert os.environ["XENO_TEST_A"] == "fromfile"       # newly set
    assert os.environ["XENO_TEST_B"] == "fromenv"        # real env wins (override=False)


def test_load_override_true_replaces(tmp_path, monkeypatch):
    env = tmp_path / ".env"
    env.write_text("XENO_TEST_C=fromfile\n")
    monkeypatch.setenv("XENOCOMM_ENV_FILE", str(env))
    monkeypatch.setenv("XENO_TEST_C", "fromenv")
    load_env_files(override=True)
    assert os.environ["XENO_TEST_C"] == "fromfile"


def test_inline_comment_stripped_on_unquoted_value():
    assert parse_env("KEY=value  # trailing comment")["KEY"] == "value"


def test_inline_comment_preserved_in_quoted_value():
    assert parse_env('KEY="value # kept"')["KEY"] == "value # kept"


def test_hash_without_leading_space_is_kept():
    assert parse_env("KEY=ab#cd")["KEY"] == "ab#cd"


def test_load_never_raises_on_embedded_nul(tmp_path, monkeypatch):
    env = tmp_path / ".env"
    env.write_bytes(b"XENO_NUL=val\x00ue\n")  # NUL survives utf-8 read/parse
    monkeypatch.setenv("XENOCOMM_ENV_FILE", str(env))
    # os.environ rejects a NUL value; load must skip the file, not raise.
    assert isinstance(load_env_files(), list)


def test_load_missing_file_is_noop(tmp_path, monkeypatch):
    monkeypatch.setenv("XENOCOMM_ENV_FILE", str(tmp_path / "does-not-exist.env"))
    monkeypatch.chdir(tmp_path)  # no .env in cwd either
    # must not raise, and must not invent variables
    assert isinstance(load_env_files(), list)
