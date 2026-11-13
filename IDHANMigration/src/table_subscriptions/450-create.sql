CREATE TABLE subscriptions
(
    subscription_id SERIAL PRIMARY KEY,
    parser_key      TEXT NOT NULL, -- Example: 'gelbooru:search'
    values          JSON NOT NULL
);