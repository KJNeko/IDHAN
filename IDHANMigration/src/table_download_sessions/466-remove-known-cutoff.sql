-- Old parser options must not prevent saved sessions from resuming with the new parser.
UPDATE download_sessions
SET options = (options - 'subscription_known_limit') #- '{parsers,gelbooru,knownLimit}'
WHERE options ? 'subscription_known_limit'
   OR options #> '{parsers,gelbooru,knownLimit}' IS NOT NULL;
