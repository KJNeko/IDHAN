ALTER TABLE subscriptions
    ADD COLUMN subscription_group_id INTEGER REFERENCES subscription_groups (subscription_group_id) ON DELETE CASCADE;

UPDATE subscriptions
SET subscription_group_id=subscription_id,
    enabled= true;

ALTER TABLE subscriptions
    ALTER COLUMN subscription_group_id SET NOT NULL,
    DROP COLUMN name,
    DROP COLUMN parser_key,
    DROP COLUMN interval_seconds,
    DROP COLUMN known_limit;

CREATE UNIQUE INDEX subscriptions_group_parameters_idx ON subscriptions (subscription_group_id, (values::jsonb));
