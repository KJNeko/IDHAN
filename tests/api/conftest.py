import os

import httpx
import pytest

from local_network import install_network_guard, local_api_url


def pytest_addoption(parser):
    parser.addoption("--run-benchmarks", action="store_true", help="Include all API benchmarks")
    parser.addoption("--api-benchmark-rounds", "--tag-benchmark-rounds", dest="api_benchmark_rounds", type=int,
                     default=10, help="Measured rounds per API benchmark")
    parser.addoption(
        "--api-url",
        default=os.environ.get("IDHAN_TEST_API_URL", "http://127.0.0.1:16609"),
        help="HTTP loopback URL of the disposable test server",
    )


@pytest.fixture(scope="session")
def api_client(pytestconfig):
    """Connect to an externally managed server without starting or resetting it."""
    with httpx.Client(
            base_url=local_api_url(pytestconfig.getoption("--api-url")),
            follow_redirects=False,
            timeout=30.0,
            trust_env=False,
    ) as client:
        yield client


def pytest_collection_modifyitems(config, items):
    if config.getoption("--api-benchmark-rounds") < 1:
        raise pytest.UsageError("--api-benchmark-rounds must be positive")
    if config.getoption("--run-benchmarks"):
        return
    skip = pytest.mark.skip(reason="Use --run-benchmarks to enable benchmarks")
    for item in items:
        if "benchmark" in item.fixturenames:
            item.add_marker(skip)


@pytest.fixture
def record(api_client):
    import hashlib
    from uuid import uuid4
    from api_test_helpers import json_response

    digest = hashlib.sha256(uuid4().bytes).hexdigest()
    created = json_response(api_client.post("/records/create", json={"sha256": digest}))
    return {"id": created["record_id"], "sha256": digest}


@pytest.fixture
def isolated_tag_domain(api_client):
    from uuid import uuid4
    from api_test_helpers import json_response

    created = json_response(api_client.post("/tags/domain/create", json={"name": f"api_{uuid4().hex}"}))
    domain = created["tag_domain_id"]
    try:
        yield domain
    finally:
        json_response(api_client.delete(f"/tags/domain/{domain}/delete"))


@pytest.fixture(scope="session")
def writable_cluster(api_client):
    from uuid import uuid4
    from api_test_helpers import json_response

    name = f"api_{uuid4().hex}"
    cluster = json_response(api_client.post("/clusters/add", json={
        "name": name, "path": f"/media/{name}", "readonly": False, "ratio": 1,
    }), 201)
    try:
        yield cluster
    finally:
        json_response(api_client.delete(f"/clusters/{cluster['cluster_id']}/remove"))


@pytest.fixture
def imported_record(api_client, writable_cluster):
    import hashlib
    from api_test_helpers import json_response, make_test_png

    payload = make_test_png()
    imported = json_response(
        api_client.post("/file/import", content=payload, headers={"content-type": "application/octet-stream"}))
    record_id = imported["record_id"]
    try:
        yield {"id": record_id, "sha256": hashlib.sha256(payload).hexdigest(), "bytes": payload}
    finally:
        json_response(api_client.delete(f"/records/{record_id}/file"))


@pytest.fixture
def searchable_record(api_client, imported_record, isolated_tag_domain):
    from uuid import uuid4
    from api_test_helpers import json_response

    tag_id = \
    json_response(api_client.post("/tags/create", json=[{"namespace": "api", "subtag": f"search_{uuid4().hex}"}]))[0][
        "tag_id"]
    json_response(
        api_client.post(f"/records/{imported_record['id']}/tags/add", params={"tag_domain_id": isolated_tag_domain},
                        json=[tag_id]))
    return {**imported_record, "tag_id": tag_id, "domain_id": isolated_tag_domain}


@pytest.fixture(scope="session", autouse=True)
def local_network_only():
    with pytest.MonkeyPatch.context() as patch:
        install_network_guard(patch)
        yield
