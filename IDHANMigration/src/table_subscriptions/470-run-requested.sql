ALTER TABLE subscriptions
    ADD COLUMN run_requested BOOLEAN NOT NULL DEFAULT false;
