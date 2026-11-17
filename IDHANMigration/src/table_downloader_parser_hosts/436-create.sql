CREATE TABLE downloader_parser_hosts
(
    host            text PRIMARY KEY CHECK (host <> '' AND host = lower(host)),
    parser          text    NOT NULL CHECK (parser <> ''),
    auto_discovered boolean NOT NULL DEFAULT false
);
