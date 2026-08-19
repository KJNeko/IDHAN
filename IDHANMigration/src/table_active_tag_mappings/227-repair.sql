-- Mappings aliased before the trigger fix kept a NULL ideal_tag_id, so they still resolve to the tag
-- they were given. Reading effective_tag_id through a query rather than a trigger record gets the value.
UPDATE active_tag_mappings atm
SET ideal_tag_id = ta.effective_tag_id
FROM tag_aliases ta
WHERE ta.aliased_id = atm.tag_id
  AND ta.tag_domain_id = atm.tag_domain_id
  AND atm.ideal_tag_id IS DISTINCT FROM ta.effective_tag_id;
