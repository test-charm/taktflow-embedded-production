"""Failure-contract tests for the DBC -> ARXML -> C pipeline.

These tests intentionally specify P3 behavior before the production
diagnostic implementation exists.  A hard failure must be one structured
ERROR on stderr, return exit status 1, and leave no generated file behind.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

import pytest
import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
CONVERTER = REPO_ROOT / "tools" / "arxml" / "dbc2arxml.py"
FIXTURES = Path(__file__).with_name("fixtures")

DIAGNOSTIC_RE = re.compile(
    r"^\[(?P<severity>ERROR|WARNING)\]"
    r"\[(?P<code>[A-Z][A-Z0-9_]+)\] "
    r"(?P<source>[^:\r\n]+):(?P<line>[1-9]\d*):"
    r"(?P<column>[1-9]\d*): (?P<message>.+)$"
)

# Stable P3 contract.  Message assertions deliberately target semantic facts,
# not library exception wording.
EXPECTED_DIAGNOSTICS = {
    "DBC_ATTR_INVALID": {
        "severity": "ERROR",
        "source": "malformed_attributes.dbc",
        "line": 15,
        "column": 1,
        "message": (
            "MalformedAttributes",
            "GenMsgCycleTime",
            "integer milliseconds",
            "fast",
        ),
    },
    "DBC_SEND_TYPE_UNSUPPORTED": {
        "severity": "ERROR",
        "source": "unsupported_send_type.dbc",
        "line": 17,
        "column": 1,
        "message": (
            "UnsupportedSendType",
            "GenMsgSendType",
            "OnWriteWithRepetition",
            "unsupported",
        ),
    },
    "DBC_NAME_COLLISION": {
        "severity": "ERROR",
        "source": "normalized_name_collision.dbc",
        "line": 19,
        "column": 1,
        "message": (
            "Collision-A",
            "Collision A",
            "Collision_A",
            "SHORT-NAME",
        ),
    },
    "ARXML_REF_UNRESOLVED": {
        "severity": "ERROR",
        "message": ("does not resolve",),
    },
    "ARXML_CONSTRUCT_UNSUPPORTED": {
        "severity": "ERROR",
        "message": ("MULTIPLEXED-I-PDU", "unsupported", "arxmlgen"),
    },
}


def _run(command: list[str], *, cwd: Path = REPO_ROOT) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    return subprocess.run(
        command,
        cwd=cwd,
        env=env,
        text=True,
        capture_output=True,
        timeout=30,
        check=False,
    )


def _run_converter(dbc: Path, output_dir: Path) -> subprocess.CompletedProcess[str]:
    return _run([sys.executable, str(CONVERTER), str(dbc), str(output_dir)])


def _assert_diagnostic(
    result: subprocess.CompletedProcess[str],
    code: str,
    source_file: Path,
    *,
    message_parts: tuple[str, ...] = (),
) -> None:
    assert result.returncode == 1, result.stdout + result.stderr
    assert "Traceback (most recent call last)" not in result.stderr
    assert str(source_file.parent).casefold() not in result.stderr.casefold()

    errors = []
    for line in result.stderr.splitlines():
        match = DIAGNOSTIC_RE.fullmatch(line)
        if match and match.group("severity") == "ERROR":
            errors.append(match)

    assert len(errors) == 1, result.stderr
    actual = errors[0]
    expected = EXPECTED_DIAGNOSTICS[code]
    assert actual.group("code") == code
    assert actual.group("severity") == expected["severity"]
    assert actual.group("source").endswith(
        str(expected.get("source", source_file.name)).replace("\\", "/")
    )
    if "line" in expected:
        assert int(actual.group("line")) == expected["line"]
    if "column" in expected:
        assert int(actual.group("column")) == expected["column"]

    for part in (*expected["message"], *message_parts):
        assert part.casefold() in actual.group("message").casefold()


def _assert_no_output(output_dir: Path) -> None:
    files = [] if not output_dir.exists() else [
        path for path in output_dir.rglob("*") if path.is_file()
    ]
    assert files == [], f"hard failure left partial output: {files}"


@pytest.mark.parametrize(
    ("fixture_name", "code"),
    [
        ("malformed_attributes.dbc", "DBC_ATTR_INVALID"),
        ("unsupported_send_type.dbc", "DBC_SEND_TYPE_UNSUPPORTED"),
        ("normalized_name_collision.dbc", "DBC_NAME_COLLISION"),
    ],
)
def test_converter_rejects_invalid_dbc_without_partial_output(
    tmp_path: Path,
    fixture_name: str,
    code: str,
) -> None:
    source = FIXTURES / fixture_name
    output_dir = tmp_path / "arxml-output"

    result = _run_converter(source, output_dir)

    _assert_diagnostic(result, code, source)
    _assert_no_output(output_dir)


@pytest.mark.parametrize(
    "fixture_name",
    [
        "malformed_attributes.dbc",
        "unsupported_send_type.dbc",
        "normalized_name_collision.dbc",
    ],
)
def test_converter_hard_failures_are_transactional(
    tmp_path: Path,
    fixture_name: str,
) -> None:
    output_dir = tmp_path / "arxml-output"

    result = _run_converter(FIXTURES / fixture_name, output_dir)
    files = [] if not output_dir.exists() else [
        path for path in output_dir.rglob("*") if path.is_file()
    ]

    assert (result.returncode, files) == (1, []), (
        "conversion failure must exit 1 and commit no output; "
        f"exit={result.returncode}, files={files}"
    )


def _build_valid_arxml(tmp_path: Path) -> tuple[Path, Path]:
    dbc = FIXTURES / "valid_minimal.dbc"
    arxml_dir = tmp_path / "valid-arxml"
    result = _run_converter(dbc, arxml_dir)
    assert result.returncode == 0, result.stdout + result.stderr
    arxml = arxml_dir / "TaktflowSystem.arxml"
    assert arxml.is_file()
    return dbc, arxml


def _replace_reference(arxml: Path, tag: str, target: str, destination: Path) -> None:
    content = arxml.read_text(encoding="utf-8")
    pattern = re.compile(rf"(<{tag}\b[^>]*>)[^<]+(</{tag}>)")
    mutated, count = pattern.subn(rf"\g<1>{target}\g<2>", content, count=1)
    assert count == 1, f"fixture has no {tag} to mutate"
    destination.write_text(mutated, encoding="utf-8", newline="\n")


def _replace_with_foreign_construct(arxml: Path, destination: Path) -> None:
    content = arxml.read_text(encoding="utf-8")
    foreign = (
        "<MULTIPLEXED-I-PDU>\n"
        "  <SHORT-NAME>ForeignMux</SHORT-NAME>\n"
        "  <LENGTH>8</LENGTH>\n"
        "</MULTIPLEXED-I-PDU>\n"
    )
    assert "</ELEMENTS>" in content
    content = content.replace("</ELEMENTS>", foreign + "</ELEMENTS>", 1)
    destination.write_text(content, encoding="utf-8", newline="\n")


def _write_project_config(
    tmp_path: Path,
    arxml: Path,
    dbc: Path,
    output_dir: Path,
) -> Path:
    config = {
        "project": {"name": "p3_failure_contract", "version": "1"},
        "input": {"arxml": [str(arxml)], "dbc": str(dbc), "e2e_source": "arxml"},
        "output": {"base_dir": str(output_dir)},
        "ecus": {
            "ecu_a": {"prefix": "ECU_A"},
            "ecu_b": {"prefix": "ECU_B"},
        },
        "generators": {"com": {"enabled": True}},
    }
    path = tmp_path / "project.yaml"
    path.write_text(yaml.safe_dump(config, sort_keys=True), encoding="utf-8", newline="\n")
    return path


def _run_arxmlgen(config: Path, output_dir: Path) -> subprocess.CompletedProcess[str]:
    return _run(
        [
            sys.executable,
            "-m",
            "tools.arxmlgen",
            "--config",
            str(config),
            "--output-dir",
            str(output_dir),
            "--generator",
            "com",
            "--quiet",
        ]
    )


def _prepare_reader_failure(
    tmp_path: Path,
    case: str,
) -> tuple[subprocess.CompletedProcess[str], Path, Path]:
    dbc, valid_arxml = _build_valid_arxml(tmp_path)
    output_dir = tmp_path / "c-output"
    if case == "signal-ref":
        source = tmp_path / "broken_i_signal_ref.arxml"
        _replace_reference(valid_arxml, "I-SIGNAL-REF", "/Foreign/MissingSignal", source)
    elif case == "pdu-ref":
        source = tmp_path / "broken_pdu_ref.arxml"
        _replace_reference(valid_arxml, "PDU-REF", "/Foreign/MissingPdu", source)
    elif case == "foreign-construct":
        source = tmp_path / "unsupported_foreign_construct.arxml"
        _replace_with_foreign_construct(valid_arxml, source)
    else:
        raise AssertionError(f"unknown reader failure case: {case}")
    config = _write_project_config(tmp_path, source, dbc, output_dir)
    return _run_arxmlgen(config, output_dir), source, output_dir


@pytest.mark.parametrize(
    ("tag", "target"),
    [
        ("I-SIGNAL-REF", "/Foreign/MissingSignal"),
        ("PDU-REF", "/Foreign/MissingPdu"),
    ],
)
def test_reader_rejects_broken_signal_and_pdu_mappings_without_output(
    tmp_path: Path,
    tag: str,
    target: str,
) -> None:
    dbc, valid_arxml = _build_valid_arxml(tmp_path)
    broken = tmp_path / f"broken_{tag.lower().replace('-', '_')}.arxml"
    _replace_reference(valid_arxml, tag, target, broken)
    output_dir = tmp_path / "c-output"
    config = _write_project_config(tmp_path, broken, dbc, output_dir)

    result = _run_arxmlgen(config, output_dir)

    _assert_diagnostic(
        result,
        "ARXML_REF_UNRESOLVED",
        broken,
        message_parts=(tag, target),
    )
    _assert_no_output(output_dir)


def test_reader_rejects_unsupported_foreign_construct_without_output(
    tmp_path: Path,
) -> None:
    dbc, valid_arxml = _build_valid_arxml(tmp_path)
    foreign = tmp_path / "unsupported_foreign_construct.arxml"
    _replace_with_foreign_construct(valid_arxml, foreign)
    output_dir = tmp_path / "c-output"
    config = _write_project_config(tmp_path, foreign, dbc, output_dir)

    result = _run_arxmlgen(config, output_dir)

    _assert_diagnostic(result, "ARXML_CONSTRUCT_UNSUPPORTED", foreign)
    _assert_no_output(output_dir)


@pytest.mark.parametrize(
    "case",
    ["signal-ref", "pdu-ref", "foreign-construct"],
)
def test_reader_hard_failures_are_transactional(
    tmp_path: Path,
    case: str,
) -> None:
    result, _source, output_dir = _prepare_reader_failure(tmp_path, case)
    files = [] if not output_dir.exists() else [
        path for path in output_dir.rglob("*") if path.is_file()
    ]

    assert (result.returncode, files) == (1, []), (
        "reader failure must exit 1 before C generation; "
        f"exit={result.returncode}, files={files}"
    )
