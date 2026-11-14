"""Benchmark successful, everyday tag workflows using functional assertions."""

import inspect
from uuid import uuid4

import pytest

import test_tags as tags
import test_tags_relationships as relationships

SCENARIOS = [
    pytest.param(function, {}, id=function.__name__.removeprefix("test_"))
    for function in (
        tags.test_create_and_read_tag,
        tags.test_creating_existing_tag_returns_same_id,
        tags.test_search_existing_tag,
        tags.test_search_missing_tag,
        tags.test_batch_preserves_order_and_duplicates,
        tags.test_concurrent_creation_returns_one_id,
        tags.test_autocomplete_includes_unused_tag,
    )
]
SCENARIOS += [
    pytest.param(tags.test_normalized_tags_share_id, {"first": first, "second": second}, id=name)
    for name, first, second in (
        ("ascii-case", "Example", "example"),
        ("unicode-composition", "caf\u00e9", "cafe\u0301"),
        ("unicode-case", "CAFÉ", "café"),
    )
]
for kind, source, target, forward, reverse in (
        ("parents", "child_id", "parent_id", "parents", "children"),
        ("alias", "aliased_id", "alias_id", "aliases", "aliased"),
):
    parameters = {"kind": kind, "source_key": source, "target_key": target}
    SCENARIOS.append(pytest.param(
        relationships.test_relationship_create_duplicate_and_remove,
        {**parameters, "forward": forward, "reverse": reverse},
        id=f"{kind}-create-duplicate-remove",
    ))


class TagBenchmarkScenario:
    def __init__(self, client, function, parameters):
        self.client = client
        self.function = function
        self.parameters = parameters
        self.domain = None

    def setup(self):
        self.arguments = {"api_client": self.client, **self.parameters}
        required = inspect.signature(self.function).parameters
        pair = {"namespace": "pytest", "subtag": f"benchmark_{uuid4().hex}"}
        if "tag_pair" in required:
            self.arguments["tag_pair"] = pair
        if "created_tag" in required:
            self.arguments["created_tag"] = self.create_tags([pair])[0]
        if "relationship_tags" in required:
            namespace = f"benchmark_{uuid4().hex}"
            self.arguments["relationship_tags"] = self.create_tags([
                {"namespace": namespace, "subtag": name} for name in ("a", "b", "c")
            ])
        if "tag_domain" in required:
            response = self.client.post("/tags/domain/create", json={"name": f"benchmark_{uuid4().hex}"})
            assert response.status_code == 200, response.text
            self.domain = response.json()["tag_domain_id"]
            self.arguments["tag_domain"] = self.domain

    def create_tags(self, pairs):
        response = self.client.post("/tags/create", json=pairs)
        assert response.status_code == 200, response.text
        return [tag["tag_id"] for tag in response.json()]

    def run(self):
        self.function(**self.arguments)

    def teardown(self):
        if self.domain is not None:
            response = self.client.delete(f"/tags/domain/{self.domain}/delete")
            assert response.status_code == 200, response.text
            self.domain = None


@pytest.mark.parametrize("function,parameters", SCENARIOS)
def test_tag_workflow(benchmark, api_client, pytestconfig, function, parameters):
    scenario = TagBenchmarkScenario(api_client, function, parameters)
    benchmark.extra_info["measurement"] = "HTTP workflow including assertions; fixture setup excluded"
    try:
        benchmark.pedantic(
            scenario.run,
            setup=scenario.setup,
            teardown=scenario.teardown,
            rounds=pytestconfig.getoption("--api-benchmark-rounds"),
            warmup_rounds=1,
        )
    finally:
        scenario.teardown()


@pytest.mark.parametrize("batch_size", [1, 10, 100, 1_000, 10_000], ids=lambda size: f"{size}-tags")
@pytest.mark.benchmark(group="tag-batch-creation")
def test_create_tag_batches(benchmark, api_client, pytestconfig, batch_size):
    payload = None
    response = None

    def setup():
        nonlocal payload
        # A fresh namespace per round guarantees these are all new additions.
        namespace = f"benchmark_{uuid4().hex}"
        payload = [{"namespace": namespace, "subtag": f"tag_{index}"} for index in range(batch_size)]

    def create_batch():
        nonlocal response
        response = api_client.post("/tags/create", json=payload, timeout=120.0)

    def validate_batch():
        assert response.status_code == 200, response.text
        created = response.json()
        assert len(created) == batch_size
        ids = [tag["tag_id"] for tag in created]
        assert all(type(tag_id) is int and tag_id > 0 for tag_id in ids)
        assert len(set(ids)) == batch_size

    benchmark.extra_info["tags_per_request"] = batch_size
    benchmark.extra_info["measurement"] = "Single batch HTTP request; data generation and validation excluded"
    benchmark.pedantic(
        create_batch,
        setup=setup,
        teardown=validate_batch,
        rounds=pytestconfig.getoption("--api-benchmark-rounds"),
        warmup_rounds=1,
    )
