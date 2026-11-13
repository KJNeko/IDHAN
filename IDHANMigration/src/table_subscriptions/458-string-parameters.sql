UPDATE subscriptions
SET values=(values::jsonb || jsonb_build_object('tags', COALESCE((SELECT string_agg(tag, ' ' ORDER BY position)
                                                                  FROM json_array_elements_text(values -> 'tags') WITH ORDINALITY AS entry(tag, position)),
                                                                 '')))::json
WHERE json_typeof(values -> 'tags') = 'array';
