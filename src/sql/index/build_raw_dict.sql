CREATE TEMP TABLE {{build_raw_dict_table}} AS
WITH grouped_raw_terms AS (
    SELECT raw_term,
           term,
           min(docid) AS first_docid
    FROM temp.{{build_terms_table}}
    GROUP BY raw_term,
             term
)
SELECT row_number() OVER (
           ORDER BY grouped_raw_terms.first_docid,
                    grouped_raw_terms.raw_term,
                    grouped_raw_terms.term
       ) - 1 AS rawtermid,
       grouped_raw_terms.raw_term,
       build_dict.termid
FROM grouped_raw_terms
JOIN temp.{{build_dict_table}} AS build_dict
  ON build_dict.term = grouped_raw_terms.term;
