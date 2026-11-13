ALTER TABLE download_sessions
    ADD COLUMN is_subscription BOOLEAN NOT NULL DEFAULT false,
    ADD COLUMN subscription_id INTEGER REFERENCES subscriptions (subscription_id) ON DELETE SET NULL;

UPDATE download_sessions sessions
SET is_subscription= true,
    subscription_id=subscriptions.subscription_id
FROM subscriptions
WHERE sessions.download_session_id = subscriptions.last_session_id
   OR sessions.download_session_id = subscriptions.active_session_id
   OR (sessions.name = 'Subscription ' || subscriptions.subscription_id || ' / ' || sessions.download_session_id
    AND sessions.options #> '{parsers,gelbooru,knownLimit}' IS NOT NULL);

CREATE INDEX download_sessions_subscription_idx ON download_sessions (subscription_id) WHERE is_subscription;
