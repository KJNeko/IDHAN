CREATE TABLE subscription_groups
(
    subscription_group_id SERIAL PRIMARY KEY,
    name                  TEXT    NOT NULL CHECK (length(name) BETWEEN 1 AND 200),
    generator             TEXT    NOT NULL,
    interval_seconds      BIGINT  NOT NULL DEFAULT 86400 CHECK (interval_seconds BETWEEN 60 AND 31536000),
    known_limit           INTEGER NOT NULL DEFAULT 20 CHECK (known_limit BETWEEN 0 AND 10000),
    enabled               BOOLEAN NOT NULL DEFAULT true
);

-- Preserve each existing subscription and its settings as a one-item group.
INSERT INTO subscription_groups (subscription_group_id, name, generator, interval_seconds, known_limit, enabled)
SELECT subscription_id,
       COALESCE(NULLIF(name, ''), 'Subscription ' || subscription_id),
       parser_key,
       interval_seconds,
       known_limit,
       enabled
FROM subscriptions;

SELECT setval(pg_get_serial_sequence('subscription_groups', 'subscription_group_id'),
              COALESCE((SELECT max(subscription_group_id) FROM subscription_groups), 1),
              EXISTS(SELECT 1 FROM subscription_groups));
