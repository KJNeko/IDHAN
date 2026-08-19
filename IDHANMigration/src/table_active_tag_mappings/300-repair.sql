UPDATE active_tag_mappings atm
SET ideal_tag_id = (SELECT atm.effective_tag_id
                    FROM tag_aliases ta
                    WHERE atm.tag_id = ta.aliased_id
                      AND atm.tag_domain_id = ta.tag_domain_id);
