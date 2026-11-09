-- Canonical catalog, independent of OIDs, creation/column order and sequence values.
-- Compare actual backend-created schemas, never replay init.sql over the upgraded DB.
SET search_path = public, pg_catalog;
WITH objects(kind, name, definition) AS (
    SELECT 'relation', c.relname, c.relkind::text
    FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
    WHERE n.nspname = 'public' AND c.relkind IN ('r', 'p', 'v', 'm', 'S')
    UNION ALL
    SELECT 'column', c.relname || '.' || a.attname,
        jsonb_build_array(format_type(a.atttypid, a.atttypmod), a.attnotnull,
            pg_get_expr(d.adbin, d.adrelid), a.attidentity, a.attgenerated,
            coll.collname)::text
    FROM pg_attribute a JOIN pg_class c ON c.oid = a.attrelid
    JOIN pg_namespace n ON n.oid = c.relnamespace
    LEFT JOIN pg_attrdef d ON d.adrelid = c.oid AND d.adnum = a.attnum
    LEFT JOIN pg_collation coll ON coll.oid = a.attcollation
    WHERE n.nspname = 'public' AND a.attnum > 0 AND NOT a.attisdropped
      AND c.relkind IN ('r', 'p')
    UNION ALL
    SELECT 'constraint', c.relname || '.' || con.conname,
        pg_get_constraintdef(con.oid) || ' validated=' || con.convalidated::text
    FROM pg_constraint con JOIN pg_class c ON c.oid = con.conrelid
    JOIN pg_namespace n ON n.oid = c.relnamespace WHERE n.nspname = 'public'
    UNION ALL
    SELECT 'index', c.relname, pg_get_indexdef(i.indexrelid)
        || ' valid=' || i.indisvalid::text
    FROM pg_index i JOIN pg_class c ON c.oid = i.indexrelid
    JOIN pg_namespace n ON n.oid = c.relnamespace WHERE n.nspname = 'public'
    UNION ALL
    SELECT 'view', c.relname, pg_get_viewdef(c.oid)
    FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
    WHERE n.nspname = 'public' AND c.relkind IN ('v', 'm')
    UNION ALL
    SELECT 'trigger', c.relname || '.' || t.tgname,
        pg_get_triggerdef(t.oid) || ' enabled=' || t.tgenabled::text
    FROM pg_trigger t JOIN pg_class c ON c.oid = t.tgrelid
    JOIN pg_namespace n ON n.oid = c.relnamespace
    WHERE n.nspname = 'public' AND NOT t.tgisinternal
    UNION ALL
    SELECT 'function', p.proname || '(' || pg_get_function_identity_arguments(p.oid) || ')',
        pg_get_functiondef(p.oid)
    FROM pg_proc p JOIN pg_namespace n ON n.oid = p.pronamespace
    WHERE n.nspname = 'public' AND p.prokind IN ('f', 'p')
      AND NOT EXISTS (SELECT 1 FROM pg_depend d
          WHERE d.classid = 'pg_proc'::regclass AND d.objid = p.oid AND d.deptype = 'e')
    UNION ALL
    SELECT 'enum', t.typname, jsonb_agg(e.enumlabel ORDER BY e.enumsortorder)::text
    FROM pg_type t JOIN pg_enum e ON e.enumtypid = t.oid
    JOIN pg_namespace n ON n.oid = t.typnamespace
    WHERE n.nspname = 'public' GROUP BY t.typname
    UNION ALL
    SELECT 'sequence', sequencename,
        jsonb_build_array(data_type, start_value, min_value, max_value, increment_by, cycle)::text
    FROM pg_sequences WHERE schemaname = 'public'
)
SELECT jsonb_build_array(kind, name, definition)::text FROM objects
ORDER BY kind COLLATE "C", name COLLATE "C", definition COLLATE "C";
