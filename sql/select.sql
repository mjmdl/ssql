-- Comment
-- Another Comment
WITH permissions AS (
    /* Agregate the permissions */
    SELECT
        account_permission.account_id,
	ARRAY_AGG(DISTINCT permission.name ORDER BY permission.name ASC) AS names
    FROM auth.account_permission
    INNER JOIN auth.permission ON permission.id = account_permission.permission_id
    	AND permission.deleted_at IS NULL
    WHERE account_permission.deleted_at IS NULL
    	AND account_permission.some_random_field >= 3.2e-435
	OR account_permission.some_random_field < 1e34
    GROUP BY account_permission.account_id
)
SELECT
    account.id AS "ID",
    account.username AS "Username",
    --account.email AS "E-mail",
    permissions.names AS "Permissions"
FROM auth.account
LEFT JOIN permissions ON permissions.account_id = account.id
WHERE account.id = ANY (ARRAY[1, 2, 3, 4]::BIGINT[])
    OR account.username ILIKE 'sailor'
    /* OR account.email = 'sailor@mail.com' */
ORDER BY account.username ASC
LIMIT 20
OFFSET 2 * 20;

SELECT "Something" FROM "Somewhere"
UNION ALL
SELECT "Otherthing" FROM "Otherwhere";

/* comment that does not close
