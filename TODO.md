# TODO

- [ ] Return import results to downloader parsers. Have `idhan.import()` and
  `importDirect().finish()` return structured results with the record ID and import
  outcome (such as newly imported or reused), plus an optional human-readable note.
  Parsers currently receive only success or failure through `Promise<void>`.
