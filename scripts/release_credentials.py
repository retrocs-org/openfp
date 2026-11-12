# SPDX-License-Identifier: MIT
"""Resolve release API credentials only when publication is explicitly requested.

The provider workflow is adapted from BitSkim's MIT-licensed release tool.
Copyright (c) 2026 BitSkim contributors. See THIRD-PARTY-NOTICES.md.
"""
import json
import os
import re
import subprocess

from release_common import ReleaseError, SECRETS, require


def validate_credentials(specs):
    require(isinstance(specs, dict) and not (set(specs) - {"github", "gitlab"}), "Unknown credential service")
    for service, spec in specs.items():
        require(isinstance(spec, dict) and all(isinstance(v, str) for v in spec.values()), "Invalid credential lookup for " + service)
        require(not any(re.search(r"github_pat_|gh[pousr]_|glpat-|://[^/\s]+:[^/@\s]+@", v) for v in spec.values()),
                "Credential lookup settings must not contain token values")
        provider = spec.get("provider", "env")
        if provider == "env":
            require(not (set(spec) - {"provider", "variable"}) and
                    re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", spec.get("variable", service.upper() + "_TOKEN")),
                    "Configure an environment variable name for " + service)
        elif provider == "1password":
            require(not (set(spec) - {"provider", "reference", "vault", "item", "field", "account"}), "Unknown 1Password lookup setting")
            ref = spec.get("reference", "")
            named = [spec.get(k, "") for k in ("vault", "item", "field")]
            require((ref.startswith("op://") and len(ref.split("/")) >= 5 and not any(named)) or
                    (not ref and all(named)), "Choose a 1Password reference OR vault/item/field for " + service)
            require(all(not v.startswith("-") and not any(ord(c) < 32 for c in v) for v in spec.values()), "Invalid 1Password lookup setting")
        else:
            raise ReleaseError("Unknown credential provider for " + service)


def credential(service, cfg):
    specs = cfg.get("credentials", {})
    validate_credentials(specs)
    spec = specs.get(service, {})
    if spec.get("provider", "env") == "env":
        variable = spec.get("variable", service.upper() + "_TOKEN")
        token = os.environ.get(variable, "")
        require(token, "Missing " + variable)
    else:
        if spec.get("reference"):
            command = ["op", "read", "--no-newline", spec["reference"]]
        else:
            command = ["op", "item", "get", spec["item"], "--vault", spec["vault"],
                       "--fields", "label=" + spec["field"], "--format", "json", "--reveal"]
        if spec.get("account"):
            command += ["--account", spec["account"]]
        try:
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
        except (OSError, subprocess.TimeoutExpired):
            raise ReleaseError("Cannot read " + service + " credential with 1Password; no secret output displayed") from None
        require(result.returncode == 0, "1Password lookup failed or was cancelled for " + service + "; no secret output displayed")
        if spec.get("reference"):
            token = result.stdout
        else:
            try:
                fields = json.loads(result.stdout)
                fields = fields if isinstance(fields, list) else fields.get("fields", [fields])
                matches = [f["value"] for f in fields if f.get("label") == spec["field"] and f.get("value")]
                require(len(matches) == 1, "1Password field missing or ambiguous for " + service)
                token = matches[0]
            except (ValueError, KeyError, TypeError, AttributeError):
                raise ReleaseError("1Password field missing or ambiguous for " + service + "; use an exact reference") from None
    require(isinstance(token, str), "Invalid credential value for " + service)
    token = token.strip()
    require(token and not token.startswith("op://") and not any(c.isspace() for c in token), "Missing/unresolved credential for " + service)
    SECRETS.add(token)
    return token


def build_environment(cfg, extra=None):
    """Do not pass publication credentials to compilers or packaging subprocesses."""
    result = {**os.environ, **cfg.get("env", {}), **(extra or {})}
    variables = {"GITHUB_TOKEN", "GITLAB_TOKEN", "GH_TOKEN", "GL_TOKEN", "OP_SERVICE_ACCOUNT_TOKEN", "OP_CONNECT_TOKEN"}
    variables.update(spec.get("variable", service.upper() + "_TOKEN")
                     for service, spec in cfg.get("credentials", {}).items() if spec.get("provider", "env") == "env")
    for name in list(result):
        if name in variables or name.startswith("OP_SESSION_"):
            del result[name]
    return result
