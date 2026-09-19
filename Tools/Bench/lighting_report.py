"""Strict schema-4 local-light plan and exact-frame observation validation."""
import math

DEFAULT_PLAN = dict(localLightMode="clustered", localLightRig=False, labLights=256,
                    labLightPile=0, lightCheck=False, lightDebugView="off")
COUNTERS = ("candidates", "assigned", "droppedPerCluster", "droppedGlobal", "truncatedFroxels", "maxCount")


def comparable_plan(schema, expected):
    provided = {key: expected[key] for key in DEFAULT_PLAN if key in expected}
    if provided and provided.keys() != DEFAULT_PLAN.keys():
        raise ValueError("incomplete requested lighting plan")
    if schema == 4:
        return {**expected, **(provided or DEFAULT_PLAN)}
    if (expected.get("scene") == "light-lab" or provided.get("localLightRig", False) or
            provided.get("lightCheck", False) or provided.get("lightDebugView", "off") != "off"):
        raise ValueError("legacy report cannot describe a local-light workload")
    return {key: value for key, value in expected.items() if key not in DEFAULT_PLAN}


def validate_sample(sample, plan):
    def count(value, key):
        if type(value) is not int or value < 0:
            raise ValueError("invalid lighting integer: " + key)
    lighting = sample.get("lighting")
    if not isinstance(lighting, dict):
        raise ValueError("missing exact-frame lighting observation")
    for key in ("frameId", "sceneGeneration", "liveLightCount", "listBytes", "allocatedListBytes",
                "gridMismatches", "indexMismatches", "counterMismatches"):
        count(lighting.get(key), key)
    if lighting["frameId"] != sample["frameId"] or lighting.get("retired") is not True:
        raise ValueError("missing or mismatched lighting retirement")
    requested = plan["localLightMode"]
    if requested not in ("off", "direct", "clustered") or lighting.get("requestedMode") != requested:
        raise ValueError("lighting request differs from frozen plan")
    live = lighting["liveLightCount"]
    if sample.get("localLightMode") != requested or sample.get("liveLightCount") != live:
        raise ValueError("declaration lighting summary differs from retirement")
    effective = requested if live else "off"
    if live > 4096 or lighting.get("effectiveMode") != effective:
        raise ValueError("invalid effective lighting mode/count")
    if plan["scene"] == "light-lab":
        if live != plan["labLights"] + plan["labLightPile"]:
            raise ValueError("LightLab live count differs from authored plan")
    elif plan["localLightRig"]:
        if plan["scene"] != "sponza" or not 1 <= live <= 32:
            raise ValueError("invalid Sponza local-light rig observation")
    elif live:
        raise ValueError("zero-light workload has local lights")
    if lighting.get("checkEnabled") is not plan["lightCheck"]:
        raise ValueError("lighting check request differs from frozen plan")
    if any(lighting[key] for key in ("gridMismatches", "indexMismatches", "counterMismatches")):
        raise ValueError("GPU light-list oracle mismatch")
    counters = lighting.get("counters")
    if not isinstance(counters, dict) or set(counters) != set(COUNTERS):
        raise ValueError("missing or unknown light-cluster counters")
    for key, value in counters.items():
        count(value, key)
    if counters["assigned"] + counters["droppedPerCluster"] + counters["droppedGlobal"] != counters["candidates"]:
        raise ValueError("light-cluster counters do not reconcile")
    if (counters["assigned"] > 65536 or counters["maxCount"] > 128 or
            counters["truncatedFroxels"] > 3456 or counters["candidates"] > live * 3456):
        raise ValueError("light-cluster counters exceed frozen bounds")
    if lighting["listBytes"] != counters["assigned"] * 4 or lighting["allocatedListBytes"] < lighting["listBytes"]:
        raise ValueError("light-list bytes differ from retired prefix/allocation")
    if effective != "clustered" and (any(counters.values()) or lighting["listBytes"]):
        raise ValueError("non-clustered mode reports cluster work")
    metric = sample.get("lightingGpuMs")
    scope = sum(p["gpuMs"] for p in sample["passes"] if p["label"].startswith("lmx.pass.light."))
    if (type(metric) not in (int, float) or not math.isfinite(metric) or metric < 0 or
            not math.isclose(metric, scope, rel_tol=1e-12, abs_tol=1e-12)):
        raise ValueError("lighting GPU scope differs from matched raw passes")
    if effective != "clustered" and any(p["label"].startswith("lmx.pass.light.") for p in sample["passes"]):
        raise ValueError("non-clustered mode declares lighting passes")
