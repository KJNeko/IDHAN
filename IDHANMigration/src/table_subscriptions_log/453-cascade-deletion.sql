ALTER TABLE subscriptions_log
    DROP CONSTRAINT subscriptions_log_subscription_id_fkey,
    DROP CONSTRAINT subscriptions_log_record_id_fkey,
    ADD CONSTRAINT subscriptions_log_subscription_id_fkey
        FOREIGN KEY (subscription_id) REFERENCES subscriptions (subscription_id) ON DELETE CASCADE,
    ADD CONSTRAINT subscriptions_log_record_id_fkey
        FOREIGN KEY (record_id) REFERENCES records (record_id) ON DELETE CASCADE;

CREATE INDEX subscriptions_log_lookup_idx ON subscriptions_log (subscription_id, record_id);
