-- Every record in a duplicate chain paired with the best record its chain ends at.
-- `insert_duplicate_pair` only re-points the pairs that ended at the record it was handed, so a
-- chain can stay deeper than one hop and the walk has to be recursive. UNION, not UNION ALL, so a
-- cycle terminates instead of spinning; records inside one reach no root and are absent here.
CREATE OR REPLACE VIEW flattened_duplicates AS
WITH RECURSIVE duplicate_chain(record_id, root_id) AS (SELECT better_record_id, better_record_id
                                                       FROM duplicate_pairs
                                                       WHERE better_record_id NOT IN
                                                             (SELECT worse_record_id FROM duplicate_pairs)
                                                       UNION
                                                       SELECT pair.worse_record_id, chain.root_id
                                                       FROM duplicate_pairs pair
                                                                JOIN duplicate_chain chain
                                                                     ON pair.better_record_id = chain.record_id)
SELECT record_id, root_id
FROM duplicate_chain;
