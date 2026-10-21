# Download-session and rate-limit streams

`GET /download_sessions/events` and `GET /rate_limits/events` return
`text/event-stream; charset=utf-8`. Both require native API authentication.
Their OpenAPI operations contain wire examples and parsed event schemas.

| Event      | Session stream data                                              | Rate-limit stream data                                             |
|------------|------------------------------------------------------------------|--------------------------------------------------------------------|
| `snapshot` | `{"sessions":[12,15]}`: existing session IDs to fetch            | `{"limits":[...]}`: latest advertised lane states                  |
| `update`   | `{"id":12}`: refetch; `{"id":12,"removed":true}`: remove locally | Complete lane state, replacing the entry keyed by `scheduling_key` |
| `shutdown` | Empty string, followed by stream end                             | Empty string, followed by stream end                               |

Session deletion can also produce an empty `update` immediately before its JSON
removal notification. Skip JSON decoding for empty data; refresh the session list
if needed. Session updates contain invalidations, so fetch the session detail,
URL, or record endpoints for any displayed information that needs refreshing.
Repeated `session_id` query parameters restrict both opening IDs and subsequent
notifications. An omitted filter subscribes to all sessions, including new ones.

Rate-limit events use `scheduling_key` and `consecutive_limits` in place of the
REST response's `lane` and `consecutive_failures`, and additionally include
`bytes_per_second`. Preserve these wire names. All fourteen lane-state fields
are emitted, including an empty `group` string when no group is configured and
`active: false` for an inactive lane. Delays are nonnegative milliseconds rounded
up, `seconds` is an integer rate window, and zero `bytes_per_second` means no
per-transfer receive-bandwidth limit. Opening snapshots adjust stored remaining
delays for elapsed time; updates are not a continuous countdown.

## Framing and browser clients

The opening `retry: 3000` block sets a reconnect delay of 3000 milliseconds.
Blank lines terminate frames. `: keepalive` comments keep idle subscriptions
open; the server checks for them approximately every ten seconds. A stream
carrying updates does not need a separate heartbeat for each timer tick.

Every application frame includes `timestamp: <integer Unix seconds>`, captured
when the event is queued. Standard SSE parsers ignore this custom field. It is
neither part of the JSON payload nor a browser MessageEvent property. The stream
does not emit SSE `id` fields.
The [HTML event-stream parsing rules](https://html.spec.whatwg.org/multipage/server-sent-events.html#event-stream-interpretation)
also distinguish comments and retry control fields from dispatched events.

Use named listeners for `snapshot`, `update`, and `shutdown`. Browser
`onmessage` does not receive these named events. This example forwards decoded
payloads to an application callback; empty updates and shutdown use `null`:

```js
function watchState(url, onState) {
  const source = new EventSource(url);
  for (const name of ["snapshot", "update"]) {
    source.addEventListener(name, event => {
      onState(name, event.data === "" ? null : JSON.parse(event.data));
    });
  }
  source.addEventListener("shutdown", () => onState("shutdown", null));
  return source;
}

// apiKey is the raw hexadecimal native API key. EventSource has no custom-header option.
const url = new URL("/download_sessions/events", window.location.origin);
url.searchParams.set("idhan_key", apiKey);
const stream = watchState(url, (event, payload) => {
  // Apply the session rules in the table above, including the empty-update case.
});

// When leaving the view or deliberately stopping subscriptions:
// stream.close();
```

The OpenAPI `itemSchema` models parsed events. Its `data` field remains a string;
`contentMediaType: application/json` and `contentSchema` describe the decoded JSON
payload. Content annotations require explicit decoding and validation by tooling;
ordinary JSON Schema validation does not automatically check embedded JSON.
See [OpenAPI's SSE guidance](https://spec.openapis.org/oas/v3.2.1.html#special-considerations-for-server-sent-events).

## Delivery and reconnects

These streams provide current-state notifications. Pending updates for the same
session or lane can coalesce, so intermediate changes may be omitted. A fresh
connection receives an opening snapshot, then notifications. The opening database
query or stored-lane snapshot and registration are not one atomic transaction.
Refetch when local state is uncertain, and account for deleted sessions or stale
REST responses when applying asynchronous fetch results.

On reconnect, replace local state using the new snapshot instead of merging it
with every previously cached entity. There is no durable history or event-ID
replay; sending `Last-Event-ID` does not recover missed changes. Session snapshots
contain IDs, while rate-limit snapshots contain complete latest advertised states.

Shutdown emits an empty-data event and ends the response. EventSource can attempt
to reconnect after shutdown or a dropped connection. Call `close()` when the
application intends to stop; reconnecting while shutdown is already in progress
can produce another shutdown event without an opening snapshot. Closing or
disconnecting releases the server subscription.
