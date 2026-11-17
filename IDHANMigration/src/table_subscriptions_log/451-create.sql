CREATE TABLE subscriptions_log
(
    subscription_id INTEGER REFERENCES subscriptions (subscription_id),
    record_id       INTEGER REFERENCES records (record_id)
);