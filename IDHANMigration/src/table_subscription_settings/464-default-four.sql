ALTER TABLE subscription_settings
    ALTER COLUMN max_running SET DEFAULT 4;

-- Move the previous default to four while keeping other configured limits.
UPDATE subscription_settings
SET max_running = 4
WHERE max_running = 3;
