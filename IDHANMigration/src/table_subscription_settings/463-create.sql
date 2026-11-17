CREATE TABLE subscription_settings
(
    singleton   BOOLEAN PRIMARY KEY DEFAULT TRUE CHECK (singleton),
    max_running INTEGER NOT NULL    DEFAULT 3 CHECK (max_running BETWEEN 1 AND 100)
);

INSERT INTO subscription_settings (singleton)
VALUES (TRUE);
